// 대시보드 확인: 공식 펌웨어와 같은 모양의 상태 메시지를 보내서 i2r.link 화면에 뜨는지 보기
//
// 실제 릴레이는 움직이지 않는다. OUT1 "표시"만 10초마다 켜짐/꺼짐으로 바꿔서 보낸다.
// i2r.link에서 이 보드를 열었을 때 OUT1 표시가 10초마다 바뀌면 성공이다.
// (서버를 하나도 고치지 않고 라이브러리로 만든 메시지가 그대로 받아들여진다는 뜻)
//
// 보내는 메시지 (공식 펌웨어 v2.0과 같은 필드):
//   {"m":"내 MAC","in":[0,0,0,0],"out":[1,0,0,0],"e":"내 이메일","t":"i2r-03","fr":"내 MAC"}
//
// 웹에서 버튼을 누르면 오는 명령은 [받음]으로만 보여 준다. (이 예제는 명령을 실행하지 않음)
// 준비는 NetCheck 예제와 같다.

#include <I2R_IoT.h>

I2R_Time itime;
I2R_Net net;

const char *BOARD_TYPE = "i2r-03";  // 내 보드에 맞게 "i2r-02", "i2r-04"로 바꾸세요
const int PORTS = 4;                // i2r-04는 8

bool fakeOut1 = false;
unsigned long lastSend = 0;

void sendState() {
  JsonDocument doc;
  doc["m"] = net.mac();
  JsonArray in = doc["in"].to<JsonArray>();
  JsonArray out = doc["out"].to<JsonArray>();
  for (int i = 0; i < PORTS; i++) {
    in.add(0);
    out.add(i == 0 && fakeOut1 ? 1 : 0);
  }
  doc["e"] = net.email();
  doc["t"] = BOARD_TYPE;
  doc["fr"] = net.mac();

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));
  bool ok = net.publish(buf);
  Serial.print(ok ? "[보냄] " : "[못 보냄: MQTT 끊김] ");
  Serial.println(buf);
}

void onMessage(const char *json, I2R_Source from) {
  Serial.print("[받음] ");
  Serial.println(json);
}

void onConnect() {
  sendState();  // 접속되면 바로 지금 상태를 보낸다 (공식 펌웨어와 같음)
}

void setup() {
  Serial.begin(115200);
  delay(500);
  itime.begin();
  net.onMessage(onMessage);
  net.onConnect(onConnect);
  net.begin();
  Serial.print("MAC: ");
  Serial.println(net.mac());
}

void loop() {
  itime.update();
  net.update();

  if (net.mqttConnected() && millis() - lastSend >= 10000) {
    lastSend = millis();
    fakeOut1 = !fakeOut1;
    sendState();
  }
}
