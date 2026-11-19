// 접속 확인: Wi-Fi, MQTT, 시간을 5초마다 시리얼로 보기
// 출력(릴레이)은 건드리지 않는다.
//
// [준비] 설정이 없으면 PC에서 아래를 실행해 USB로 설정을 보낸다 (비밀번호는 화면에 보이지 않음).
//   powershell -ExecutionPolicy Bypass -File <I2R_IoT 폴더>\tools\send-si.ps1
// 또는 시리얼 모니터(115200, 새 줄)에 한 줄로 넣는다.
//   {"c":"si","ssid":"공유기이름","password":"비밀번호","mqttBroker":"test.mosquitto.org"}
// 저장되면 {"c":"si","ok":true}가 나오고 보드가 다시 시작한다.
//
// [확인할 것]
// 1) MAC, Wi-Fi 이름, 서버, 토픽이 맞게 나오는지 (비밀번호는 나오지 않음)
// 2) Wi-Fi 연결 → 시간 맞음(NTP) → MQTT 접속 순서로 바뀌는지
// 3) 받기 토픽으로 메시지를 보내면 [받음]으로 보이는지 (MQTT Explorer 등)
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
  Serial.print("Wi-Fi 설정: ");
  Serial.println(net.configured() ? net.ssid() : String("없음"));
  if (net.configured()) {
    Serial.print("MQTT 서버: ");
    if (net.mqttConfigured()) {
      Serial.printf("%s:%u%s\n", net.broker().c_str(), (unsigned)net.port(),
                    net.user().length() ? " (아이디 있음)" : "");
      Serial.print("토픽: 받기 ");
      Serial.print(net.inTopic());
      Serial.print(", 보내기 ");
      Serial.println(net.outTopic());
    } else {
      Serial.println("없음 (Wi-Fi만)");
    }
  }
}

void loop() {
  loops++;
  itime.update();
  net.update();

  if (millis() - lastPrint >= 5000) {
    lastPrint = millis();
    Serial.printf("[상태] Wi-Fi %s | 시간 %s%s | MQTT %s",
                  net.wifiConnected() ? "연결" : "끊김",
                  itime.valid() ? itime.text().c_str() : "없음",
                  itime.synced() ? " (NTP)" : (itime.restored() ? " (저장된 시각)" : ""),
                  !net.mqttConfigured() ? "설정 없음" : (net.mqttConnected() ? "접속" : "끊김"));
    if (net.mqttConfigured() && !net.mqttConnected() && net.fails() > 0)
      Serial.printf(" (실패 %u번: %s)", (unsigned)net.fails(), I2R_Net::rcText(net.lastRc()));
    Serial.printf(" | loop 횟수 %lu | 남은 메모리 %u\n", loops, (unsigned)ESP.getFreeHeap());
  }
}
