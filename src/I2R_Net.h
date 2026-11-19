// I2R_Net: i2r 보드 공통 Wi-Fi, MQTT, 설정, 기기 ID
//
//   I2R_Net net;
//   setup(): net.onMessage(내함수); net.begin();
//   loop():  net.update();           // 기다리지 않음. PLC 제어를 멈추지 않는다
//   net.publish("{...}")             // 내 상태 보내기 (i2r/{그룹}/out)
//   net.publishIn("{...}")           // 같은 그룹의 다른 보드에게 (i2r/{그룹}/in)
//
// [stand alone] Wi-Fi와 MQTT는 ESP32 두 번째 코어(core 0)의 작업에서 돈다.
// 서버에 닿지 않아 접속이 몇 초씩 걸려도 loop()는 멈추지 않는다.
// 받은 메시지는 큐에 넣어 두었다가 update()가 loop() 안에서 내 함수로 넘겨 준다.
//
// [서버] 사용자가 고르는 MQTT 서버에 접속한다 (i2r.link는 쓰지 않는다. 공식 펌웨어 전용).
//   검증, 교육: test.mosquitto.org:1883 / 집 안: i2r-05 로컬 서버 / 아마존 개인 서버
//   서버 주소를 비워 두면 MQTT 없이 Wi-Fi만 연결한다 (보드 홈페이지용).
//
// [설정] USB로 받은 {"c":"si",...} 한 줄을 NVS "i2riot"에 저장하고 {"c":"si","ok":true} 응답 뒤 다시 시작.
//   공식 펌웨어의 설정 자리(netcfg, certs)는 읽지도 쓰지도 않는다.
//   그래서 공식 펌웨어를 다시 설치하면 원래 설정 그대로 i2r.link에 접속된다.
//
// [안전] si는 USB에서만 받는다 (MQTT로 온 si는 무시). 비밀번호를 시리얼에 찍지 않는다.
//   통신이 계속 실패해도 기본으로는 다시 시작하지 않는다 (setRestartAfterFails로 켤 수 있음).

#ifndef I2R_NET_H
#define I2R_NET_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

enum I2R_Source : uint8_t {
  I2R_FROM_MQTT = 0,  // 서버(브로커)에서
  I2R_FROM_USB = 1,   // USB 시리얼에서
  I2R_FROM_WEB = 2    // 보드 홈페이지에서 (I2R_Web, 나중에)
};

class I2R_Net {
public:
  static const size_t MSG_MAX = 1024;                // 메시지 한 개의 최대 길이
  static const unsigned long WIFI_RETRY_MS = 30000;  // Wi-Fi 다시 연결 간격
  static const unsigned long MQTT_RETRY_MS = 10000;  // MQTT 다시 접속 간격
  static const unsigned long IGNORE_MS = 5000;       // 켠 뒤 받은 메시지를 무시하는 시간 (서버에 남아 있던 옛 명령)
  static const uint16_t DEFAULT_PORT = 1883;

  typedef void (*MessageFn)(const char *json, I2R_Source from);
  typedef void (*EventFn)();

  // ---- begin() 전에 부르는 설정 ----
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
    if (p.begin(NVS_NS, true)) {
      _ssid = p.getString("ssid", "");
      _pw = p.getString("pw", "");
      _broker = p.getString("broker", "");
      _port = p.getUShort("port", DEFAULT_PORT);
      _user = p.getString("user", "");
      _mpass = p.getString("mpass", "");
      _group = p.getString("group", "");
      p.end();
    }

    readMac();
    if (_group.length() == 0) _group = defaultGroup();
    _inTopic = "i2r/" + _group + "/in";
    _outTopic = "i2r/" + _group + "/out";

    _outQ = xQueueCreate(6, sizeof(Msg));
    _inQ = xQueueCreate(6, sizeof(Msg));

    if (!configured()) {
      Serial.println("[I2R_Net] Wi-Fi 설정이 없습니다. 통신 없이 동작합니다. (USB로 si 설정을 보내면 저장됩니다)");
      return;
    }
    if (!mqttConfigured() && _log) {
      Serial.println("[I2R_Net] MQTT 서버 설정이 없습니다. Wi-Fi만 연결합니다.");
    }

    _mqtt.setClient(_plain);
    if (mqttConfigured()) _mqtt.setServer(_broker.c_str(), _port);
    _mqtt.setKeepAlive(60);
    _mqtt.setSocketTimeout(5);
    _mqtt.setBufferSize(MSG_MAX + 128);
    _mqtt.setCallback([this](char *topic, uint8_t *payload, unsigned int len) {
      onMqtt(payload, len);
    });

    xTaskCreatePinnedToCore(taskFn, "i2r_net", 8192, this, 1, &_task, 0);
  }

  // loop()에서 매번. 받은 메시지를 내 함수로 넘기고, USB 설정 줄을 읽는다.
  void update() {
    if (!_begun) return;
    if (_connEvent) {
      _connEvent = false;
      if (_onConn) _onConn();
    }
    while (_inQ && xQueueReceive(_inQ, &_rx, 0) == pdTRUE) deliver(_rx.data, I2R_FROM_MQTT);
    if (_serialSetup) readSerial();
  }

  // 다른 통로(보드 홈페이지 등)에서 받은 명령을 같은 처리 함수로 넘긴다
  void inject(const char *json, I2R_Source from) { deliver(json, from); }

  // ---- 상태 ----
  bool configured() const { return _ssid.length() > 0; }         // Wi-Fi 설정이 있는지
  bool mqttConfigured() const { return _broker.length() > 0; }   // MQTT 서버 설정이 있는지
  bool wifiConnected() const { return WiFi.status() == WL_CONNECTED; }
  bool mqttConnected() const { return _mqttUp; }
  int lastRc() const { return _lastRc; }               // 마지막 MQTT 접속 실패 코드 (PubSubClient state)
  uint32_t fails() const { return _fails; }            // MQTT 연속 실패 횟수

  const String &mac() const { return _mac; }           // "D4:8C:49:50:46:F4"
  const String &ssid() const { return _ssid; }
  const String &broker() const { return _broker; }
  uint16_t port() const { return _port; }
  const String &user() const { return _user; }         // MQTT 아이디 (비밀번호는 읽을 수 없음)
  const String &group() const { return _group; }       // 토픽 그룹
  const String &inTopic() const { return _inTopic; }
  const String &outTopic() const { return _outTopic; }
  String ip() const { return wifiConnected() ? WiFi.localIP().toString() : String(""); }

  // PubSubClient 접속 실패 코드를 학습자가 읽을 수 있는 말로
  static const char *rcText(int rc) {
    switch (rc) {
      case -4: return "서버 응답 없음 (시간 초과)";
      case -3: return "연결이 끊김";
      case -2: return "서버에 닿지 않음 (주소, 포트, 인터넷 확인)";
      case -1: return "연결 끊김";
      case 0: return "접속됨";
      case 1: return "서버가 MQTT 버전을 받지 않음";
      case 2: return "client id 거절";
      case 3: return "서버를 쓸 수 없음";
      case 4: return "아이디 또는 비밀번호가 틀림";
      case 5: return "권한 없음 (아이디·비밀번호가 필요한 서버)";
      default: return "알 수 없는 오류";
    }
  }

  // ---- 보내기 (MQTT에 접속돼 있을 때만, 아니면 false) ----
  bool publish(const char *json) { return enqueue(json, false); }    // i2r/{그룹}/out
  bool publishIn(const char *json) { return enqueue(json, true); }   // i2r/{그룹}/in (같은 그룹의 다른 보드에게)

private:
  static constexpr const char *NVS_NS = "i2riot";

  struct Msg {
    uint8_t toIn;
    char data[MSG_MAX];
  };

  String _ssid, _pw, _broker, _user, _mpass, _group, _mac, _inTopic, _outTopic;
  uint16_t _port = DEFAULT_PORT;
  bool _log = true;
  bool _serialSetup = true;
  uint8_t _restartAfter = 0;
  bool _begun = false;
  unsigned long _startMs = 0;

  WiFiClient _plain;
  PubSubClient _mqtt;
  QueueHandle_t _outQ = nullptr;
  QueueHandle_t _inQ = nullptr;
  TaskHandle_t _task = nullptr;

  volatile bool _mqttUp = false;
  volatile bool _connEvent = false;
  volatile int _lastRc = 0;
  volatile uint32_t _fails = 0;

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

  // 기본 그룹: MAC을 소문자로, ':' 없이 ("d4d4da73873c"). 공개 서버에서도 개인 정보가 드러나지 않는다.
  String defaultGroup() const {
    String g = _mac;
    g.replace(":", "");
    g.toLowerCase();
    return g;
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
    if (millis() - _startMs < IGNORE_MS) return;  // 켠 직후 서버에 남아 있던 옛 메시지는 무시
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
      else Serial.println("[I2R_Net] USB가 아닌 곳에서 온 설정(si)은 받지 않습니다.");
      return;
    }
    if (_onMsg) _onMsg(json, from);
  }

  // {"c":"si","ssid":"..","password":"..","mqttBroker":"..","mqttPort":1883,"mqttUser":"..","mqttPass":"..","g":".."}
  // 보낸 값으로 설정 전체를 바꾼다. 없는 항목은 비움 (포트는 1883, 그룹은 MAC 기반 기본값).
  // i2r.link 웹의 COM 포트 전송({"c":"si","ssid","password","e","mqttBroker"})도 받는다. 이메일(e)은 저장하지 않는다.
  void applySetup(const char *json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
      Serial.println("{\"c\":\"si\",\"ok\":false,\"err\":\"json\"}");
      return;
    }
    String ssid = doc["ssid"] | "";
    if (ssid.length() == 0) {
      Serial.println("{\"c\":\"si\",\"ok\":false,\"err\":\"ssid\"}");
      return;
    }
    String pw = doc["password"] | "";
    String broker = doc["mqttBroker"] | "";
    uint16_t port = doc["mqttPort"] | DEFAULT_PORT;
    String user = doc["mqttUser"] | "";
    String mpass = doc["mqttPass"] | "";
    String group = doc["g"] | "";
    String email = doc["e"] | "";
    broker.trim();
    group.trim();
    email.trim();

    // i2r.link 웹의 "COM 포트 설정 전송"도 같은 형식으로 보낸다. 그 화면의 서버 칸 기본값(mqtt.i2r.link,
    // 비우면 "i2r")은 공식 펌웨어 전용 서버라 I2R_IoT는 접속하지 않는다 (Wi-Fi만 저장).
    String lower = broker;
    lower.toLowerCase();
    bool i2rLink = lower == "i2r" || lower.endsWith("i2r.link");
    if (i2rLink) broker = "";

    Preferences p;
    bool ok = p.begin(NVS_NS, false);
    if (ok) {
      p.clear();
      p.putString("ssid", ssid);
      p.putString("pw", pw);
      p.putString("broker", broker);
      p.putUShort("port", port);
      p.putString("user", user);
      p.putString("mpass", mpass);
      p.putString("group", group);
      p.end();
    }
    if (!ok) {
      Serial.println("{\"c\":\"si\",\"ok\":false,\"err\":\"nvs\"}");
      return;
    }
    Serial.println("{\"c\":\"si\",\"ok\":true}");
    if (email.length() > 0) {
      Serial.println("[I2R_Net] 이메일은 I2R_IoT에서 쓰지 않아 저장하지 않았습니다. 다음부터는 이메일 칸을 비워 두세요.");
    }
    if (i2rLink) {
      Serial.println("[I2R_Net] i2r.link 서버는 공식 펌웨어 전용이라 접속하지 않습니다. Wi-Fi만 저장했습니다.");
      Serial.println("[I2R_Net] MQTT를 쓰려면 서버 칸에 test.mosquitto.org 같은 주소를 넣어 다시 보내세요.");
    }
    if (broker.length() > 0) {
      Serial.printf("[I2R_Net] 설정을 저장했습니다. Wi-Fi \"%s\", 서버 %s:%u%s, 그룹 %s. 다시 시작합니다.\n",
                    ssid.c_str(), broker.c_str(), (unsigned)port, user.length() ? " (아이디 있음)" : "",
                    group.length() ? group.c_str() : "(기본: MAC)");
    } else {
      Serial.printf("[I2R_Net] 설정을 저장했습니다. Wi-Fi \"%s\", MQTT 없음. 다시 시작합니다.\n", ssid.c_str());
    }
    Serial.flush();
    delay(1000);
    ESP.restart();
  }

  // USB에서 '{'로 시작하는 한 줄을 받는다 (600자 제한)
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
    WiFi.setSleep(false);  // 절전이 켜져 있으면 접속 직후 서버 응답을 놓치는 공유기가 있다
  }

  // ---- 통신 작업 (core 0) ----
  void taskLoop() {
    unsigned long lastWifiTry = millis();
    unsigned long nextMqtt = 0;
    bool wifiLogged = false;
    startWifi();

    for (;;) {
      unsigned long now = millis();

      if (WiFi.status() != WL_CONNECTED) {
        if (_mqttUp) {
          _mqttUp = false;
          if (_log) Serial.println("[I2R_Net] Wi-Fi 끊김");
        }
        wifiLogged = false;
        if (now - lastWifiTry >= WIFI_RETRY_MS) {
          if (_log) Serial.println("[I2R_Net] Wi-Fi 다시 연결");
          WiFi.disconnect(true);
          vTaskDelay(pdMS_TO_TICKS(100));
          startWifi();
          lastWifiTry = millis();
        }
      } else {
        if (!wifiLogged) {
          wifiLogged = true;
          if (_log) Serial.printf("[I2R_Net] Wi-Fi 연결됨: IP %s (RSSI %d dBm)\n",
                                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
        if (!mqttConfigured()) {
          // Wi-Fi만 쓰는 모드
        } else if (!_mqtt.connected()) {
          if (_mqttUp) {
            _mqttUp = false;
            xQueueReset(_outQ);  // 끊긴 동안의 오래된 메시지는 보내지 않는다 (접속되면 상태를 다시 보냄)
            if (_log) Serial.println("[I2R_Net] MQTT 끊김");
          }
          if ((long)(now - nextMqtt) >= 0) {
            if (_log) Serial.printf("[I2R_Net] MQTT 접속 시도: %s:%u\n", _broker.c_str(), (unsigned)_port);
            unsigned long t0 = millis();
            bool ok = _user.length() > 0
                          ? _mqtt.connect(_mac.c_str(), _user.c_str(), _mpass.c_str())
                          : _mqtt.connect(_mac.c_str());
            unsigned long took = millis() - t0;
            if (ok) {
              _mqtt.subscribe(_inTopic.c_str());
              _mqttUp = true;
              _connEvent = true;
              _fails = 0;
              _lastRc = 0;
              if (_log) Serial.printf("[I2R_Net] MQTT 접속됨 (%lu ms). 받기 %s, 보내기 %s\n",
                                      took, _inTopic.c_str(), _outTopic.c_str());
            } else {
              _lastRc = _mqtt.state();
              _fails = _fails + 1;
              if (_log) Serial.printf("[I2R_Net] MQTT 접속 실패 rc=%d %s (%lu ms). %lu초 뒤 다시\n",
                                      _lastRc, rcText(_lastRc), took, MQTT_RETRY_MS / 1000);
              nextMqtt = millis() + MQTT_RETRY_MS;
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
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
};

#endif
