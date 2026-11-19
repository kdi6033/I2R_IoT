// MQTT 시험: test.mosquitto.org(공개 시험 서버)로 보내고 받기
//
// 보드는 5초마다 상태를 보내고, 명령을 받으면 답한다. 출력(릴레이)은 건드리지 않는다.
//
// [준비]
// 1) USB로 설정 보내기 (서버 기본값이 test.mosquitto.org)
//      powershell -ExecutionPolicy Bypass -File <I2R_IoT 폴더>\tools\send-si.ps1
// 2) PC에 MQTT Explorer(https://mqtt-explorer.com) 설치 → Host: test.mosquitto.org, Port: 1883 → CONNECT
// 3) 시리얼에 나온 보내기 토픽(i2r/<그룹>/out)이 MQTT Explorer에 5초마다 보이면 성공
//
// [명령 보내 보기] MQTT Explorer의 Publish에서 토픽 i2r/<그룹>/in, 형식 json으로
//   {"c":"gs"}             → 보드가 지금 상태를 바로 보냄
//   {"c":"echo","t":"hi"}  → 보드가 {"c":"echo","t":"hi"}를 그대로 돌려줌
//
// [주의] 공개 서버라 누구나 이 토픽을 볼 수 있고 명령도 보낼 수 있다.
//   이메일 같은 개인 정보를 토픽이나 메시지에 넣지 않는다. 실제 기기(모터, 전기 기구)를 연결하지 않는다.

#include <I2R_IoT.h>

I2R_Time itime;
I2R_Net net;

unsigned long lastSend = 0;
unsigned long count = 0;

void sendState(const char *why) {
  JsonDocument doc;
  doc["m"] = net.mac();
  doc["cnt"] = count;
  doc["up"] = millis() / 1000;                       // 켜진 뒤 지난 초
  if (itime.valid()) doc["time"] = itime.text();
  doc["heap"] = ESP.getFreeHeap();
  doc["why"] = why;

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));
  bool ok = net.publish(buf);
  Serial.print(ok ? "[보냄] " : "[못 보냄: MQTT 끊김] ");
  Serial.println(buf);
}

void onMessage(const char *json, I2R_Source from) {
  Serial.print("[받음] ");
  Serial.println(json);

  JsonDocument doc;
  if (deserializeJson(doc, json)) return;
  const char *c = doc["c"] | "";
  if (strcmp(c, "gs") == 0) {
    sendState("gs");
  } else if (strcmp(c, "echo") == 0) {
    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    net.publish(buf);
    Serial.print("[보냄] ");
    Serial.println(buf);
  }
}

void onConnect() {
  sendState("connect");  // 접속되면 바로 지금 상태를 보낸다
}

void setup() {
  Serial.begin(115200);
  delay(500);
  itime.begin();
  net.onMessage(onMessage);
  net.onConnect(onConnect);
  net.begin();

  Serial.println("==== I2R_IoT MqttTest ====");
  Serial.print("MAC: ");
  Serial.println(net.mac());
  if (net.mqttConfigured()) {
    Serial.printf("서버: %s:%u\n", net.broker().c_str(), (unsigned)net.port());
    Serial.print("MQTT Explorer에서 볼 토픽: ");
    Serial.println(net.outTopic());
    Serial.print("명령을 보낼 토픽: ");
    Serial.println(net.inTopic());
  } else {
    Serial.println("MQTT 서버 설정이 없습니다. tools/send-si.ps1로 설정을 보내세요.");
  }
}

void loop() {
  itime.update();
  net.update();

  if (net.mqttConnected() && millis() - lastSend >= 5000) {
    lastSend = millis();
    count++;
    sendState("timer");
  }
}
