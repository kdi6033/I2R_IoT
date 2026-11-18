// 접속 확인: Wi-Fi, MQTT, 시간을 5초마다 시리얼로 보기
// 출력(릴레이)은 건드리지 않는다.
//
// [준비]
// 공식 펌웨어로 Wi-Fi와 이메일을 설정해 둔 보드라면 그대로 접속된다. (설정과 인증서는 보드에 남아 있음)
// 설정이 없으면 i2r.link의 "COM 포트 설정 전송"으로 보내거나, 시리얼 모니터(115200, 새 줄)에 한 줄로 넣는다.
//   {"c":"si","ssid":"공유기이름","password":"비밀번호","e":"내이메일","mqttBroker":"mqtt.i2r.link"}
// 저장되면 {"c":"si","ok":true}가 나오고 보드가 다시 시작한다.
//
// [확인할 것]
// 1) MAC, 이메일, 서버가 맞게 나오는지 (비밀번호는 나오지 않음)
// 2) Wi-Fi 연결 → 시간 맞음 → MQTT 접속됨 순서로 바뀌는지
// 3) 웹에서 이 보드에 명령을 보내면 [받음]으로 보이는지
// 4) 공유기를 껐다 켜면 다시 접속되는지. 그동안 "loop 횟수"가 계속 늘어나는지 (loop가 멈추지 않음)

#include <I2R_IoT.h>

I2R_Time itime;
I2R_Net net;

unsigned long lastPrint = 0;
unsigned long loops = 0;

void onMessage(const char *json, I2R_Source from) {
  Serial.print(from == I2R_FROM_USB ? "[받음 USB] " : "[받음 MQTT] ");
  Serial.println(json);
}

void onConnect() {
  Serial.println("[알림] MQTT에 접속됐습니다.");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  itime.begin();
  net.onMessage(onMessage);
  net.onConnect(onConnect);
  net.begin();

  Serial.println("==== I2R_IoT NetCheck ====");
  Serial.print("MAC: ");
  Serial.println(net.mac());
  Serial.print("설정: ");
  Serial.println(net.configured() ? "있음" : "없음");
  if (net.configured()) {
    Serial.print("Wi-Fi: ");
    Serial.println(net.ssid());
    Serial.print("이메일: ");
    Serial.println(net.email());
    Serial.print("서버: ");
    Serial.println(net.broker());
    Serial.print("인증서: ");
    Serial.println(net.hasCerts() ? "있음" : "없음");
    Serial.print("토픽: 받기 ");
    Serial.print(net.inTopic());
    Serial.print(", 보내기 ");
    Serial.println(net.outTopic());
  }
}

void loop() {
  loops++;
  itime.update();
  net.update();

  if (millis() - lastPrint >= 5000) {
    lastPrint = millis();
    Serial.printf("[상태] Wi-Fi %s | 시간 %s %s | MQTT %s",
                  net.wifiConnected() ? "연결" : "끊김",
                  itime.valid() ? itime.text().c_str() : "없음",
                  itime.synced() ? "(NTP)" : (itime.restored() ? "(저장된 시각)" : ""),
                  net.mqttConnected() ? "접속" : "끊김");
    if (!net.mqttConnected() && net.fails() > 0) Serial.printf(" (실패 %u번, rc=%d)", (unsigned)net.fails(), net.lastRc());
    Serial.printf(" | loop 횟수 %lu | 남은 메모리 %u\n", loops, (unsigned)ESP.getFreeHeap());
  }
}
