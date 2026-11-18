// I2R_Net: i2r 보드 공통 Wi-Fi, MQTT(TLS), 설정, 기기 ID
//
//   I2R_Net net;
//   setup(): net.onMessage(내함수); net.begin();
//   loop():  net.update();           // 기다리지 않음. PLC 제어를 멈추지 않는다
//   net.publish("{...}")             // 내 상태 보내기 (i2r/{email}/out)
//   net.publishIn("{...}")           // 같은 사용자의 다른 보드에게 (i2r/{email}/in)
//
// [stand alone] Wi-Fi와 MQTT는 ESP32 두 번째 코어(core 0)의 작업에서 돈다.
// 서버에 닿지 않아 접속이 몇 초씩 걸려도 loop()는 멈추지 않는다.
// 받은 메시지는 큐에 넣어 두었다가 update()가 loop() 안에서 내 함수로 넘겨 준다.
//
// 공식 펌웨어(v2.0)와 같게 하는 것:
//   NVS "netcfg"(ssid, password, email, mqttBroker), "certs"(root_ca, client_cert, private_key)
//   토픽 i2r/{email}/in 받기, i2r/{email}/out 보내기, 포트 8883, client id = Wi-Fi MAC(대문자)
//   keepalive 60초, TCP 5초, TLS 10초 제한, Wi-Fi 절전 끄기, 켠 뒤 5초 동안 받은 메시지 무시
//   USB로 받은 {"c":"si",...} 한 줄을 저장하고 {"c":"si","ok":true} 응답 뒤 다시 시작
// v2.0과 다르게 하는 것 (안전):
//   si는 USB에서만 받는다 (MQTT로 온 si는 무시). 비밀번호를 시리얼에 찍지 않는다.
//   통신이 계속 실패해도 기본으로는 다시 시작하지 않는다 (setRestartAfterFails로 켤 수 있음).
//   인증서와 개인키는 라이브러리에 없다. 공식 펌웨어가 NVS에 넣어 둔 것을 읽는다.

#ifndef I2R_NET_H
#define I2R_NET_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

enum I2R_Source : uint8_t {
  I2R_FROM_MQTT = 0,  // 서버(브로커)에서
  I2R_FROM_USB = 1    // USB 시리얼에서 (웹의 COM 포트 전송 등)
};

class I2R_Net {
public:
  static const size_t MSG_MAX = 1024;                // 메시지 한 개의 최대 길이
  static const unsigned long WIFI_RETRY_MS = 30000;  // Wi-Fi 다시 연결 간격
  static const unsigned long MQTT_RETRY_MS = 60000;  // MQTT 다시 접속 간격 (v2.0과 같음)
  static const unsigned long IGNORE_MS = 5000;       // 켠 뒤 받은 메시지를 무시하는 시간 (v2.0과 같음)
  static const time_t TIME_VALID_AFTER = 1770076800; // 이보다 이르면 인증서 검사를 못 해서 접속을 미룬다

  typedef void (*MessageFn)(const char *json, I2R_Source from);
  typedef void (*EventFn)();

  // ---- begin() 전에 부르는 설정 ----
  void setPort(uint16_t port) { _port = port; }        // 기본 8883
  void setTls(bool on) { _tls = on; }                  // 기본 켜짐. 로컬 서버(1883)는 setTls(false)
  void setRestartAfterFails(uint8_t n) { _restartAfter = n; }  // MQTT가 n번 연속 실패하면 다시 시작 (0 = 안 함)
  void setSerialSetup(bool on) { _serialSetup = on; }  // USB로 JSON 한 줄 받기 (기본 켜짐)
  void setLog(bool on) { _log = on; }                  // 접속 기록을 시리얼에 찍기 (기본 켜짐)
  void onMessage(MessageFn fn) { _onMsg = fn; }        // 받은 메시지 (si는 여기로 오지 않음)
  void onConnect(EventFn fn) { _onConn = fn; }         // MQTT에 접속될 때마다 (상태를 다시 보낼 때)

  // setup()에서 한 번. 설정을 읽고, 설정이 있으면 통신 작업을 시작한다. 기다리지 않는다.
  void begin() {
    if (_begun) return;
    _begun = true;
    _startMs = millis();

    Preferences p;
    if (p.begin("netcfg", true)) {
      _ssid = p.getString("ssid", "");
      _pw = p.getString("password", "");
      _email = p.getString("email", "");
      _broker = p.getString("mqttBroker", "");
      p.end();
    }
    if (p.begin("certs", true)) {
      _ca = p.getString("root_ca", "");
      _cert = p.getString("client_cert", "");
      _key = p.getString("private_key", "");
      p.end();
    }

    readMac();
    _inTopic = "i2r/" + _email + "/in";
    _outTopic = "i2r/" + _email + "/out";

    _outQ = xQueueCreate(6, sizeof(Msg));
    _inQ = xQueueCreate(6, sizeof(Msg));

    if (!configured()) {
      Serial.println("[I2R_Net] Wi-Fi 설정이 없습니다. 통신 없이 동작합니다. (USB나 BLE로 설정을 보내면 저장됩니다)");
      return;
    }
    if (_tls && !hasCerts()) {
      Serial.println("[I2R_Net] 인증서가 없어 서버에 접속할 수 없습니다. i2r.link에서 공식 펌웨어를 한 번 설치하면 인증서가 저장됩니다.");
    }

    if (_tls) {
      if (hasCerts()) {
        _secure.setCACert(_ca.c_str());
        _secure.setCertificate(_cert.c_str());
        _secure.setPrivateKey(_key.c_str());
      }
      _secure.setConnectionTimeout(5000);  // TCP 연결 대기 최대 5초
      _secure.setHandshakeTimeout(10);     // TLS 핸드셰이크 최대 10초
      _mqtt.setClient(_secure);
    } else {
      _mqtt.setClient(_plain);
    }
    _mqtt.setServer(_broker.c_str(), _port);
    _mqtt.setKeepAlive(60);
    _mqtt.setBufferSize(MSG_MAX + 128);
    _mqtt.setCallback([this](char *topic, uint8_t *payload, unsigned int len) {
      onMqtt(payload, len);
    });

    xTaskCreatePinnedToCore(taskFn, "i2r_net", 10240, this, 1, &_task, 0);
  }

  // loop()에서 매번. 받은 메시지를 내 함수로 넘기고, USB 설정 줄을 읽는다.
  void update() {
    if (!_begun) return;
    if (_connEvent) {
      _connEvent = false;
      if (_onConn) _onConn();
    }
    while (xQueueReceive(_inQ, &_rx, 0) == pdTRUE) deliver(_rx.data, I2R_FROM_MQTT);
    if (_serialSetup) readSerial();
  }

  // ---- 상태 ----
  bool configured() const { return _ssid.length() > 0 && _broker.length() > 0; }
  bool hasCerts() const { return _ca.length() > 0 && _cert.length() > 0 && _key.length() > 0; }
  bool wifiConnected() const { return WiFi.status() == WL_CONNECTED; }
  bool mqttConnected() const { return _mqttUp; }
  int lastRc() const { return _lastRc; }               // 마지막 MQTT 접속 실패 코드 (PubSubClient state)
  const char *lastSslError() const { return _sslErr; } // 마지막 TLS 오류 글
  uint32_t fails() const { return _fails; }            // MQTT 연속 실패 횟수

  const String &mac() const { return _mac; }           // "D4:8C:49:50:46:F4"
  const String &email() const { return _email; }
  const String &broker() const { return _broker; }
  const String &ssid() const { return _ssid; }
  const String &inTopic() const { return _inTopic; }
  const String &outTopic() const { return _outTopic; }

  // ---- 보내기 (MQTT에 접속돼 있을 때만, 아니면 false) ----
  bool publish(const char *json) { return enqueue(json, false); }    // i2r/{email}/out
  bool publishIn(const char *json) { return enqueue(json, true); }   // i2r/{email}/in (다른 보드에게)

private:
  struct Msg {
    uint8_t toIn;
    char data[MSG_MAX];
  };

  String _ssid, _pw, _email, _broker, _mac, _inTopic, _outTopic;
  String _ca, _cert, _key;
  uint16_t _port = 8883;
  bool _tls = true;
  bool _log = true;
  bool _serialSetup = true;
  uint8_t _restartAfter = 0;
  bool _begun = false;
  unsigned long _startMs = 0;

  WiFiClientSecure _secure;
  WiFiClient _plain;
  PubSubClient _mqtt;
  QueueHandle_t _outQ = nullptr;
  QueueHandle_t _inQ = nullptr;
  TaskHandle_t _task = nullptr;

  volatile bool _mqttUp = false;
  volatile bool _connEvent = false;
  volatile int _lastRc = 0;
  volatile uint32_t _fails = 0;
  char _sslErr[96] = "";

  MessageFn _onMsg = nullptr;
  EventFn _onConn = nullptr;

  Msg _rx;      // loop()에서만 씀
  Msg _tx;      // loop()에서만 씀
  Msg _taskTx;  // 통신 작업에서만 씀
  Msg _taskRx;  // 통신 작업에서만 씀
  String _usbBuf;

  void readMac() {
    WiFi.mode(WIFI_STA);  // Wi-Fi가 없어도 MAC을 읽을 수 있게
    delay(100);
    String m = WiFi.macAddress();
    if (m == "00:00:00:00:00:00") {
      delay(500);
      m = WiFi.macAddress();
    }
    m.toUpperCase();
    _mac = m;
  }

  bool enqueue(const char *json, bool toIn) {
    if (!_outQ || !_mqttUp) return false;
    size_t n = strlen(json);
    if (n >= MSG_MAX) {
      Serial.printf("[I2R_Net] 메시지가 너무 깁니다 (%u > %u)\n", (unsigned)n, (unsigned)(MSG_MAX - 1));
      return false;
    }
    _tx.toIn = toIn ? 1 : 0;
    memcpy(_tx.data, json, n + 1);
    return xQueueSend(_outQ, &_tx, 0) == pdTRUE;
  }

  // 통신 작업에서 불림: 복사해서 큐에 넣기만 한다
  void onMqtt(uint8_t *payload, unsigned int len) {
    if (millis() - _startMs < IGNORE_MS) return;  // 켠 직후 쌓여 있던 메시지는 무시 (v2.0과 같음)
    if (len >= MSG_MAX) return;
    memcpy(_taskRx.data, payload, len);
    _taskRx.data[len] = '\0';
    _taskRx.toIn = 0;
    xQueueSend(_inQ, &_taskRx, 0);
  }

  // loop()에서: si는 여기서 처리하고, 나머지는 내 함수로 넘긴다
  void deliver(const char *json, I2R_Source from) {
    JsonDocument filter;
    filter["c"] = true;
    JsonDocument head;
    if (deserializeJson(head, json, DeserializationOption::Filter(filter))) return;  // JSON이 아니면 버림
    const char *c = head["c"] | "";
    if (strcmp(c, "si") == 0) {
      if (from == I2R_FROM_USB) applySetup(json);
      else Serial.println("[I2R_Net] MQTT로 온 설정(si)은 받지 않습니다. USB나 BLE로 보내세요.");
      return;
    }
    if (_onMsg) _onMsg(json, from);
  }

  void applySetup(const char *json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return;
    String ssid = doc["ssid"] | "";
    String pw = doc["password"] | "";
    String email = doc["e"] | (doc["email"] | "");
    String broker = doc["mqttBroker"] | "";

    Serial.println("{\"c\":\"si\",\"ok\":true}");  // 웹의 COM 포트 전송이 기다리는 응답
    Serial.flush();

    Preferences p;
    if (p.begin("netcfg", false)) {
      p.putString("ssid", ssid);
      p.putString("password", pw);
      p.putString("email", email);
      p.putString("mqttBroker", broker);
      p.end();
    }
    Serial.printf("[I2R_Net] 설정을 저장했습니다. Wi-Fi \"%s\", 서버 %s. 다시 시작합니다.\n",
                  ssid.c_str(), broker.c_str());
    delay(1000);
    ESP.restart();
  }

  // USB에서 '{'로 시작하는 한 줄을 받는다 (v2.0의 handleUsbSerial과 같음, 600자 제한)
  void readSerial() {
    while (Serial.available()) {
      char ch = Serial.read();
      if (ch == '\n') {
        _usbBuf.trim();
        if (_usbBuf.length() > 0 && _usbBuf[0] == '{') deliver(_usbBuf.c_str(), I2R_FROM_USB);
        _usbBuf = "";
      } else if (ch != '\r') {
        if (_usbBuf.length() < 600) _usbBuf += ch;
        else _usbBuf = "";  // 너무 긴 줄은 버린다
      }
    }
  }

  static void taskFn(void *arg) { static_cast<I2R_Net *>(arg)->taskLoop(); }

  void startWifi() {
    WiFi.begin(_ssid.c_str(), _pw.c_str());
    WiFi.setSleep(false);  // 절전이 켜져 있으면 접속 직후 서버 응답을 놓치는 공유기가 있다 (v2.0에서 확인)
  }

  // ---- 통신 작업 (core 0) ----
  void taskLoop() {
    unsigned long lastWifiTry = millis();
    unsigned long nextMqtt = 0;
    uint8_t bounce = 0;
    bool waitedTimeLog = false;
    startWifi();

    for (;;) {
      unsigned long now = millis();

      if (WiFi.status() != WL_CONNECTED) {
        _mqttUp = false;
        if (now - lastWifiTry >= WIFI_RETRY_MS) {
          if (_log) Serial.println("[I2R_Net] Wi-Fi 다시 연결");
          WiFi.disconnect(true);
          vTaskDelay(pdMS_TO_TICKS(100));
          startWifi();
          lastWifiTry = millis();
        }
      } else if (!_mqtt.connected()) {
        if (_mqttUp) {
          _mqttUp = false;
          xQueueReset(_outQ);  // 끊긴 동안의 오래된 메시지는 보내지 않는다 (접속되면 상태를 다시 보냄)
          if (_log) Serial.println("[I2R_Net] MQTT 끊김");
        }
        bool timeOk = !_tls || time(nullptr) >= TIME_VALID_AFTER;
        bool certOk = !_tls || hasCerts();
        if (!timeOk && !waitedTimeLog) {
          if (_log) Serial.println("[I2R_Net] 시간을 받을 때까지 MQTT 접속을 미룹니다 (I2R_Time 필요)");
          waitedTimeLog = true;
        }
        if (timeOk && certOk && (long)(now - nextMqtt) >= 0) {
          if (_log) Serial.printf("[I2R_Net] MQTT 접속 시도: %s:%u (RSSI %d dBm)\n", _broker.c_str(), _port, WiFi.RSSI());
          unsigned long t0 = millis();
          bool ok = _mqtt.connect(_mac.c_str());
          unsigned long took = millis() - t0;
          if (ok) {
            _mqtt.subscribe(_inTopic.c_str());
            _mqttUp = true;
            _connEvent = true;
            _fails = 0;
            _lastRc = 0;
            _sslErr[0] = '\0';
            bounce = 0;
            if (_log) Serial.printf("[I2R_Net] MQTT 접속됨 (%lu ms)\n", took);
          } else {
            _lastRc = _mqtt.state();
            _fails++;
            if (_tls) _secure.lastError(_sslErr, sizeof(_sslErr));
            if (_log) Serial.printf("[I2R_Net] MQTT 접속 실패 rc=%d %s (%lu ms)\n", _lastRc, _sslErr, took);
            // TCP 제한 시간(5초) 안에 응답이 없던 경우: Wi-Fi를 다시 붙이고 5초 뒤 재시도 (v2.0과 같음, 2번까지)
            if (took >= 4500 && took < 7000 && bounce < 2) {
              bounce++;
              WiFi.disconnect(true);
              vTaskDelay(pdMS_TO_TICKS(100));
              startWifi();
              lastWifiTry = millis();
              nextMqtt = millis() + 5000;
            } else {
              nextMqtt = millis() + MQTT_RETRY_MS;
            }
            if (_restartAfter && _fails >= _restartAfter) {
              Serial.printf("[I2R_Net] MQTT %u번 연속 실패 - 다시 시작\n", (unsigned)_fails);
              vTaskDelay(pdMS_TO_TICKS(200));
              ESP.restart();
            }
          }
        }
      } else {
        _mqtt.loop();
        while (xQueueReceive(_outQ, &_taskTx, 0) == pdTRUE) {
          _mqtt.publish(_taskTx.toIn ? _inTopic.c_str() : _outTopic.c_str(), _taskTx.data);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
};

#endif
