// 예제 2: 시뮬레이터로 확인하는 i2r 보드 (i2r-03, i2r-02, i2r-04)
//
// 실제 보드가 i2r 시뮬레이터의 가상 보드와 똑같은 메시지를 주고받는다.
// 시뮬레이터 제어 화면에 이 보드가 가상 보드와 나란히 나타나고, 스위치를 누르면 실제 릴레이가 움직인다.
// test_protocol.py로 가상 보드(정답)와 같게 답하는지 검사할 수 있다.
//
//   프로토콜: https://github.com/kdi6033/i2r-server/blob/main/protocol.md
//   시뮬레이터: https://github.com/kdi6033/i2r-server/tree/main/sim
//
// [나눠 맡기]
//   I2R_Net   Wi-Fi, MQTT, USB 설정(si), 끊김 알림(lw)
//   I2R_Link  i2r 프로토콜 공통: 받는 순서, gs, st, hello, err, 상태 보내기(바뀔 때, 60초마다)
//   이 예제   PLC에만 있는 것: 상태에 넣을 입출력·센서 값(fillState)과 출력 명령 so(onCommand)
//
// [만든 것] 상태, gs, so, st, 오류. 입력 규칙(bio), 시간표(sch), 센서 트리거(bs)는 다음 단계에서
//   I2R_PLC 쪽에 넣는다 (지금은 err unknown으로 답한다).
//
// [필요한 라이브러리] I2R_IoT, I2R_PLC, PubSubClient, ArduinoJson (모두 라이브러리 매니저에서)
//
// [준비]
// 1) USB로 설정 보내기: 시뮬레이터 화면 "🔌 USB로 실제 보드 연결", i2r.link 웹(연결 → COM 포트),
//    또는 powershell -ExecutionPolicy Bypass -File <I2R_IoT 폴더>\tools\send-si.ps1
// 2) 이 예제를 올리고 시리얼 모니터(115200)를 열면, 시뮬레이터를 켤 명령이 나온다.
// 3) 시뮬레이터 화면(http://localhost:8000) 제어 화면의 보드 목록에서 이 보드의 MAC을 고른다.
//
// [LED] 보드 LED(GPIO 2)가 계속 켜 있으면 시작 중이거나 Wi-Fi·MQTT 접속 중, 1초에 한 번 깜박이면 모두 접속됨.
//
// [주의]
// - 시험할 때는 릴레이에 아무것도 연결하지 않는다 (시뮬레이터와 시험 스크립트가 출력을 켠다).
// - 공개 서버라 그룹과 MAC을 아는 사람은 이 보드에 명령을 보낼 수 있다. 그룹과 MAC을 공개된 곳에 적지 않는다.
// - i2r-02, i2r-04 입력은 무전압 접점이다. 입력 단자에 전압을 넣지 않는다.
//
// [번호] 사람은 1부터 (OUT1, plc.output(1)), JSON은 0부터 ("n":0 = OUT1)

#include <I2R_IoT.h>
#include <I2R_PLC.h>

I2R_PLC plc(I2R_03);  // 내 보드: I2R_02, I2R_03, I2R_04
I2R_Time itime;
I2R_Net net;
I2R_Link i2r(net, &itime);

const char *VERSION = "0.2.0";

bool hasSensor() { return plc.board() == I2R_03; }

// 상태에 넣을 값: {"in":[..],"out":[..],"temp","humi"} (못 읽은 센서는 넣지 않음)
void fillState(JsonObject s) {
  JsonArray in = s["in"].to<JsonArray>();
  for (int n = 1; n <= plc.inputCount(); n++) in.add(plc.input(n) ? 1 : 0);
  JsonArray out = s["out"].to<JsonArray>();
  for (int n = 1; n <= plc.outputCount(); n++) out.add(plc.outputState(n) ? 1 : 0);
  if (hasSensor() && plc.sensorOk()) {
    s["temp"] = I2R_Link::round1(plc.temperature());
    s["humi"] = I2R_Link::round1(plc.humidity());
  }
}

// so: {"c":"so","m":MAC,"n":0,"v":1,"d":5}  n = 0부터, d = 켠 뒤 저절로 끌 초 (없으면 계속)
void cmdSo(JsonObjectConst j, JsonVariantConst id) {
  JsonVariantConst n = j["n"], v = j["v"], d = j["d"];
  if (!I2R_Link::isInt(n) || n.as<long>() < 0 || n.as<long>() >= plc.outputCount()) return i2r.sendErr("so", "bad_n", id);
  if (!I2R_Link::isOnOff(v)) return i2r.sendErr("so", "bad_v", id);
  float sec = 0;
  if (!d.isNull()) {
    if (!I2R_Link::isNum(d) || d.as<float>() < 0 || d.as<float>() > 86400) return i2r.sendErr("so", "bad_v", id);
    sec = d.as<float>();
  }
  bool on = I2R_Link::onOff(v);
  plc.output(n.as<long>() + 1, on, on ? (uint32_t)lroundf(sec) : 0);  // JSON 0부터 → 보드 1부터
  i2r.sendState(id);  // 명령에는 바로 답한다
}

// 보드 명령 (공통 명령 gs, st는 I2R_Link가 처리). 모르는 명령이면 false → err unknown
bool onCommand(const char *c, JsonObjectConst j, JsonVariantConst id) {
  if (!strcmp(c, "so")) { cmdSo(j, id); return true; }
  return false;  // bio, sch, bs는 다음 단계
}

// 보드 LED: Wi-Fi와 MQTT가 모두 접속되면 1초에 한 번 깜박이고(0.5초 켜짐, 0.5초 꺼짐), 아니면 계속 켜 둔다
void updateLed() {
  static unsigned long at = 0;
  static bool on = true;
  if (!net.mqttConnected()) {
    if (!on) plc.led(on = true);
    return;
  }
  if (millis() - at >= 500) {
    at = millis();
    plc.led(on = !on);
  }
}

// MQTT에 접속될 때마다 (hello와 상태는 I2R_Link가 이미 보냄)
void onConnected() {
  Serial.println();
  Serial.println("[SimCheck] MQTT 접속됨. 시뮬레이터를 이렇게 켜세요:");
  Serial.printf("  python sim.py --server %s", net.broker().c_str());
  if (net.port() != 1883) Serial.printf(" --port %u", net.port());
  Serial.printf(" --group %s\n", net.group().c_str());
  Serial.printf("  제어 화면 보드 목록에서 %s 를 고르세요.\n\n", net.mac().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(500);
  plc.begin();                 // 출력은 모두 꺼진 상태에서 시작
  plc.led(true);
  itime.begin();

  i2r.setBoard(plc.type(), VERSION, plc.inputCount(), plc.outputCount());
  if (hasSensor()) {
    i2r.addSensor("temp", 0.2);  // 온도가 0.2도 바뀌면 상태를 보냄
    i2r.addSensor("humi", 1.0);
  }
  i2r.onState(fillState);
  i2r.onCommand(onCommand);
  i2r.onConnect(onConnected);
  i2r.begin();
  net.begin();                 // 기다리지 않는다. Wi-Fi가 없어도 PLC는 돈다

  Serial.println("==== I2R_IoT 02_SimCheck ====");
  Serial.printf("보드 %s, MAC %s\n", plc.type(), net.mac().c_str());
  if (!net.configured()) {
    Serial.println("Wi-Fi 설정이 없습니다. tools\\send-si.ps1 로 USB 설정을 보내세요.");
  } else if (!net.mqttConfigured()) {
    Serial.println("MQTT 서버 설정이 없습니다. send-si.ps1 에서 서버를 넣으세요 (기본 test.mosquitto.org).");
  } else {
    Serial.printf("서버 %s:%u, 토픽 %s / %s\n", net.broker().c_str(), net.port(),
                  net.inTopic().c_str(), net.outTopic().c_str());
  }
}

void loop() {
  plc.update();   // 1) PLC: 입력 읽기, 자동 끄기, 센서. Wi-Fi와 상관없이 항상 먼저
  itime.update();
  net.update();   // 2) 통신: 받은 명령을 I2R_Link로 넘김 (기다리지 않음)
  i2r.update();  // 3) 바뀌었으면 상태 보내기, 60초마다 살아 있음
  updateLed();
}
