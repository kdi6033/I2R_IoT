// 예제 1: 공유기에 접속해 인터넷에서 한국 시간을 받아 시리얼 모니터에 보여 주기
//
// 보드가 인터넷 시간 서버(NTP: pool.ntp.org 등)에서 시간을 받아 1초마다 이렇게 보여 준다.
//   2026-11-19 (목) 10:25:03  한국 시간 (NTP)
// 출력(릴레이)은 건드리지 않는다.
//
// [준비] 보드에 공유기 설정이 없으면 PC에서 아래를 실행해 USB로 보낸다 (비밀번호는 화면에 보이지 않음).
//   powershell -ExecutionPolicy Bypass -File <I2R_IoT 폴더>\tools\send-si.ps1
//   이 예제는 Wi-Fi만 쓰므로 "MQTT 서버"에서 none을 넣어도 된다.
//
// [시리얼 모니터] 115200
//
// [알아 둘 것]
// - 공유기 연결 → 시간 받기까지 보통 몇 초 걸린다. 그동안은 "시간을 기다리는 중"이 나온다.
// - 한 번 시간을 받으면 보드가 마지막 시각을 저장해 둔다. 다시 켰을 때 인터넷이 없어도
//   "(저장된 시각)"으로 이어 가다가, 인터넷이 되면 다시 정확한 시간으로 맞춘다.

#include <I2R_IoT.h>

I2R_Time itime;
I2R_Net net;

const char *WEEK[] = {"일", "월", "화", "수", "목", "금", "토"};
unsigned long lastPrint = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  itime.begin();
  itime.setTz(9 * 3600);  // 한국 시간 = 세계 표준시(UTC) + 9시간
  net.begin();            // 저장된 공유기 설정으로 Wi-Fi 연결 (기다리지 않음)

  Serial.println("==== 예제 1: 한국 시간 ====");
  if (!net.configured()) {
    Serial.println("공유기 설정이 없습니다. tools/send-si.ps1로 설정을 보내 주세요.");
  } else {
    Serial.print("공유기: ");
    Serial.println(net.ssid());
  }
}

void loop() {
  itime.update();  // Wi-Fi가 연결되면 인터넷에서 시간을 받는다
  net.update();

  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();

    if (!itime.valid()) {
      Serial.printf("시간을 기다리는 중... (Wi-Fi %s)\n", net.wifiConnected() ? "연결됨" : "연결 중");
      return;
    }

    struct tm t;
    itime.local(t);  // 한국 시각으로 나눠 받기
    Serial.printf("%04d-%02d-%02d (%s) %02d:%02d:%02d  한국 시간 %s\n",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, WEEK[t.tm_wday],
                  t.tm_hour, t.tm_min, t.tm_sec,
                  itime.synced() ? "(NTP)" : "(저장된 시각, 인터넷 시간 기다리는 중)");
  }
}
