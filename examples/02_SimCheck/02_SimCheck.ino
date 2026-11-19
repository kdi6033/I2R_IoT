// 예제 2: 시뮬레이터로 확인하는 i2r 보드 (i2r-03, i2r-02, i2r-04)
//
// 실제 보드가 i2r 시뮬레이터의 가상 보드와 똑같은 메시지를 주고받는다.
// 시뮬레이터 제어 화면에 이 보드가 가상 보드와 나란히 나타나고, 스위치를 누르면 실제 릴레이가 움직인다.
// test_protocol.py로 가상 보드(정답)와 같게 답하는지 검사할 수 있다.
//
//   시뮬레이터: https://github.com/kdi6033/i2r-server/tree/main/sim
//
// [online/offline] I2R_Net이 접속하면 {"c":"lw","on":1}을 보내고, 보드가 갑자기 사라지면 서버가
//   {"c":"lw","on":0}을 대신 보낸다. USB를 뽑으면 약 45초 안에 시뮬레이터 제어 화면이 "오프라인"이 된다.
//
// [만든 것] 1단계 상태 보내기, 2단계 gs, so, st 명령과 오류. 입력 규칙(bio), 시간표(sch),
//   센서 트리거(bs)는 다음 단계에서 넣는다 (지금은 err unknown으로 답한다).
//
// [필요한 라이브러리] I2R_IoT, I2R_PLC, PubSubClient, ArduinoJson (모두 라이브러리 매니저에서)
//
// [준비]
// 1) USB로 설정 보내기 (서버 기본값 test.mosquitto.org)
//      powershell -ExecutionPolicy Bypass -File <I2R_IoT 폴더>\tools\send-si.ps1
// 2) 이 예제를 올리고 시리얼 모니터(115200)를 열면, 시뮬레이터를 켤 명령이 나온다.
//      python sim.py --server test.mosquitto.org --group <이 보드의 그룹>
// 3) 시뮬레이터 화면(http://localhost:8000) 제어 화면의 보드 목록에서 이 보드의 MAC을 고른다.
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

const char *VERSION = "0.1.0";
const int PROTOCOL_VERSION = 1;
const size_t MSG_LIMIT = 512;              // 이보다 긴 명령은 too_long (시뮬레이터와 같게)
const unsigned long IGNORE_MS = 5000;      // MQTT에 접속한 뒤 이 시간 동안 받은 명령은 무시 (서버에 남은 옛 명령)
const unsigned long COALESCE_MS = 200;     // 이 시간 안의 여러 변화는 상태 한 번으로 묶는다
const unsigned long HEARTBEAT_MS = 60000;  // 바뀌지 않아도 이 간격으로 상태를 보낸다
const float TEMP_STEP = 0.2;               // 온도가 이만큼 바뀌면 상태를 보낸다
const float HUMI_STEP = 1.0;               // 습도가 이만큼 바뀌면 상태를 보낸다

unsigned long connectedAt = 0;     // 마지막으로 MQTT에 접속한 시각
unsigned long dirtySince = 0;      // 상태가 바뀌어 보내야 하는 때 (0 = 없음)
unsigned long lastStateAt = 0;     // 마지막으로 상태를 보낸 시각
uint16_t lastInputs = 0;           // 지난번 입력 상태 (바뀌었는지 비교)
float sentTemp = NAN, sentHumi = NAN;  // 마지막으로 보낸 온도, 습도

bool hasSensor() { return plc.board() == I2R_03; }

float round1(float x) { return roundf(x * 10.0f) / 10.0f; }

// ---- 보내기: 모든 메시지에 fr(보낸 보드 MAC)을 붙인다 ----
void sendJson(JsonDocument &doc) {
  doc["fr"] = net.mac();
  char buf[MSG_LIMIT];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf) - 1) {
    Serial.println("[SimCheck] 보낼 메시지가 너무 깁니다");
    return;
  }
  if (net.publish(buf)) {
    Serial.print("[보냄] ");
    Serial.println(buf);
  }
}

// 상태: {"m","in","out","t","e","temp","humi","fr"} (+ 명령에 id가 있었으면 같은 id)
void sendState(JsonVariantConst id = JsonVariantConst()) {
  JsonDocument doc;
  doc["m"] = net.mac();
  JsonArray in = doc["in"].to<JsonArray>();
  for (int n = 1; n <= plc.inputCount(); n++) in.add(plc.input(n) ? 1 : 0);
  JsonArray out = doc["out"].to<JsonArray>();
  for (int n = 1; n <= plc.outputCount(); n++) out.add(plc.outputState(n) ? 1 : 0);
  doc["t"] = plc.type();
  doc["e"] = net.group();  // 공식 펌웨어는 이메일이지만, I2R_IoT는 이메일을 저장하지 않아 그룹을 넣는다
  if (hasSensor() && plc.sensorOk()) {
    doc["temp"] = round1(plc.temperature());
    doc["humi"] = round1(plc.humidity());
    sentTemp = round1(plc.temperature());
    sentHumi = round1(plc.humidity());
  }
  if (!id.isNull()) doc["id"] = id;
  sendJson(doc);
  lastStateAt = millis();
  dirtySince = 0;
}

void sendHello() {
  JsonDocument doc;
  doc["c"] = "hello";
  doc["t"] = plc.type();
  doc["pv"] = PROTOCOL_VERSION;
  doc["ver"] = VERSION;
  doc["in"] = plc.inputCount();
  doc["out"] = plc.outputCount();
  JsonArray s = doc["sensors"].to<JsonArray>();
  if (hasSensor()) {
    s.add("temp");
    s.add("humi");
  }
  sendJson(doc);
}

// 오류: {"c":"err","m":MAC,"re":명령,"code":..,"id"?}
void sendErr(const char *re, const char *code, JsonVariantConst id) {
  JsonDocument doc;
  doc["c"] = "err";
  doc["m"] = net.mac();
  doc["re"] = re;
  doc["code"] = code;
  if (!id.isNull()) doc["id"] = id;
  sendJson(doc);
}

void markDirty() {
  if (dirtySince == 0) dirtySince = millis();
}

// 출력이 실제로 바뀔 때마다 (원격 명령, 자동 끄기 모두) 상태를 보내도록 표시
void onOutput(uint8_t n, bool on) { markDirty(); }

// ---- 값 검사 (시뮬레이터 board.py와 같은 규칙) ----
bool isInt(JsonVariantConst v) { return v.is<long>() && !v.is<bool>(); }
bool isNum(JsonVariantConst v) { return v.is<float>() && !v.is<bool>(); }
bool isOnOff(JsonVariantConst v) { return v.is<bool>() || (isInt(v) && (v.as<long>() == 0 || v.as<long>() == 1)); }
bool onOffValue(JsonVariantConst v) { return v.is<bool>() ? v.as<bool>() : v.as<long>() == 1; }

// so: {"c":"so","m":MAC,"n":0,"v":1,"d":5}  n = 0부터, d = 켠 뒤 자동으로 끌 초 (없으면 계속)
void cmdSo(JsonDocument &j, JsonVariantConst id) {
  JsonVariantConst n = j["n"], v = j["v"], d = j["d"];
  if (!isInt(n) || n.as<long>() < 0 || n.as<long>() >= plc.outputCount()) return sendErr("so", "bad_n", id);
  if (!isOnOff(v)) return sendErr("so", "bad_v", id);
  float sec = 0;
  if (!d.isNull()) {
    if (!isNum(d) || d.as<float>() < 0 || d.as<float>() > 86400) return sendErr("so", "bad_v", id);
    sec = d.as<float>();
  }
  bool on = onOffValue(v);
  plc.output(n.as<long>() + 1, on, on ? (uint32_t)lroundf(sec) : 0);  // JSON 0부터 → 보드 1부터
  sendState(id);  // 명령에는 바로 답한다 (묶지 않음)
}

// st / setTime: 시간대(tz, tzOffset, tz_offset)와 현지 시각(epoch)
void cmdSt(JsonDocument &j, JsonVariantConst id) {
  JsonVariantConst tz = j["tz"];
  if (tz.isNull()) tz = j["tzOffset"];
  if (tz.isNull()) tz = j["tz_offset"];
  JsonVariantConst epoch = j["epoch"];
  if (isNum(epoch)) {
    itime.setEpoch((time_t)epoch.as<double>(), isNum(tz) ? tz.as<long>() : itime.tz());
  } else if (isNum(tz)) {
    itime.setTz(tz.as<long>());
  }
  sendState(id);
}

bool isControl(const char *c) {
  const char *list[] = {"so", "st", "setTime", "bio", "bs", "sch"};  // MAC이 꼭 있어야 하는 명령
  for (const char *x : list)
    if (strcmp(c, x) == 0) return true;
  return false;
}

// 받은 메시지 (I2R_Net이 loop() 안에서 불러 준다)
void onMessage(const char *json, I2R_Source from) {
  if (from == I2R_FROM_MQTT && millis() - connectedAt < IGNORE_MS) return;
  if (strlen(json) > MSG_LIMIT) return sendErr("?", "too_long", JsonVariantConst());

  JsonDocument j;
  if (deserializeJson(j, json)) return;              // JSON이 아니면 조용히 버림
  if (!j.is<JsonObject>()) return;
  const char *fr = j["fr"] | "";
  if (net.mac().equalsIgnoreCase(fr)) return;         // 내가 보낸 것이 되돌아온 것
  const char *c = j["c"] | "";
  if (!*c || !strcmp(c, "lw") || !strcmp(c, "hello") || !strcmp(c, "err")) return;  // 다른 보드의 알림
  JsonVariantConst id = j["id"];
  JsonVariantConst mv = j["m"];
  const char *m = mv | "";

  if (!strcmp(c, "gs")) {
    if (mv.isNull() || !strcmp(m, "*") || net.mac().equalsIgnoreCase(m)) {
      Serial.printf("[받음] %s\n", json);
      sendState(id);
    }
    return;
  }
  if (mv.isNull()) {
    if (isControl(c)) {
      Serial.printf("[받음] %s\n", json);
      sendErr(c, "no_m", id);
    }
    return;
  }
  if (strcmp(m, "*") != 0 && !net.mac().equalsIgnoreCase(m)) return;  // 다른 보드 것: 조용히 무시

  Serial.printf("[받음] %s\n", json);
  if (!strcmp(c, "so")) cmdSo(j, id);
  else if (!strcmp(c, "st") || !strcmp(c, "setTime")) cmdSt(j, id);
  else sendErr(c, "unknown", id);  // bio, sch, bs는 다음 단계
}

// MQTT에 접속될 때마다: 자기소개와 지금 상태
void onConnect() {
  connectedAt = millis();
  Serial.println();
  Serial.println("[SimCheck] MQTT 접속됨. 시뮬레이터를 이렇게 켜세요:");
  Serial.printf("  python sim.py --server %s", net.broker().c_str());
  if (net.port() != 1883) Serial.printf(" --port %u", net.port());
  Serial.printf(" --group %s\n", net.group().c_str());
  Serial.printf("  제어 화면 보드 목록에서 %s 를 고르세요.\n\n", net.mac().c_str());
  sendHello();
  sendState();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  plc.begin();                 // 출력은 모두 꺼진 상태에서 시작
  plc.onOutputChange(onOutput);
  itime.begin();
  net.onMessage(onMessage);
  net.onConnect(onConnect);
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
  lastInputs = plc.inputBits();
}

void loop() {
  plc.update();   // 1) PLC: 입력 읽기, 자동 끄기, 센서. Wi-Fi와 상관없이 항상 먼저
  itime.update();
  net.update();   // 2) 통신: 받은 명령 처리 (기다리지 않음)

  // 3) 상태가 바뀌었으면 보낼 준비
  uint16_t now = plc.inputBits();
  if (now != lastInputs) {
    lastInputs = now;
    markDirty();
  }
  if (hasSensor() && plc.sensorOk()) {
    float t = round1(plc.temperature()), h = round1(plc.humidity());
    if (isnan(sentTemp) || fabsf(t - sentTemp) >= TEMP_STEP - 0.001f) markDirty();
    if (isnan(sentHumi) || fabsf(h - sentHumi) >= HUMI_STEP - 0.001f) markDirty();
  }

  // 4) 접속돼 있을 때만 보낸다. 끊겨 있으면 바뀐 표시만 남겨 두고, 다시 붙으면 onConnect가 보낸다
  if (net.mqttConnected()) {
    if (dirtySince && millis() - dirtySince >= COALESCE_MS) sendState();
    else if (millis() - lastStateAt >= HEARTBEAT_MS) sendState();
  }
}
