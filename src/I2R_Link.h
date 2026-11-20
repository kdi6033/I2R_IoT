// I2R_Link: i2r 프로토콜 처리 (보드 종류와 상관없는 공통 부분)
//
// i2r 프로토콜 원본: https://github.com/kdi6033/i2r-server/blob/main/protocol.md
// 정답(가상 보드): i2r-server/sim/board.py  ·  시험: i2r-server/sim/test_protocol.py
//
// 이 파일이 맡는 것 (protocol.md 장 번호)
//   2장  접속하면 hello, 상태를 보냄 (lw on:1은 I2R_Net이 보냄). 접속 직후 5초 동안 받은 명령은 무시
//   3장  받은 메시지를 정해진 순서로 거름: 길이, JSON, fr이 나, 알림, gs, MAC 없음(no_m), 다른 보드
//   4장  상태 메시지: 명령에는 바로, 바뀌면 0.2초 묶음, 센서는 정한 폭 이상 바뀔 때, 60초마다 살아 있음
//   5장  gs, st/setTime, hello, err (id를 붙여 돌려줌)
// 보드가 맡는 것 (함수 두 개만 넘겨 주면 됨)
//   onState(함수)   상태에 넣을 값: "in", "out" 배열과 센서 값 (못 읽은 센서는 넣지 않음)
//   onCommand(함수) 보드 명령 (PLC: so, bio, sch, bs / Otto: ot ...). 모르는 명령이면 false
//
// 사용 예 (자세히는 examples/02_SimCheck)
//   I2R_Net net;  I2R_Time itime;  I2R_Link i2r(net, &itime);   (변수 이름 link는 시스템 함수와 겹쳐 쓰지 않는다)
//   i2r.setBoard("i2r-03", "0.2.0", 4, 4);  i2r.addSensor("temp", 0.2);
//   i2r.onState(fillState);  i2r.onCommand(onCommand);
//   setup(): itime.begin(); i2r.begin(); net.begin();      loop(): itime.update(); net.update(); i2r.update();

#ifndef I2R_LINK_H
#define I2R_LINK_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>
#include "I2R_Net.h"
#include "I2R_Time.h"

class I2R_Link {
 public:
  static const int PROTOCOL_VERSION = 1;          // protocol.md의 pv
  static const size_t MSG_LIMIT = 512;            // 이보다 긴 명령은 too_long (protocol.md 4.2)
  static const unsigned long IGNORE_MS = 5000;    // 접속 직후 무시 (서버에 남은 옛 명령)
  static const unsigned long COALESCE_MS = 200;   // 이 시간 안의 여러 변화는 상태 한 번으로
  static const unsigned long HEARTBEAT_MS = 60000;
  static const unsigned long CHECK_MS = 50;       // 상태가 바뀌었는지 보는 간격
  static const uint8_t MAX_SENSORS = 8;

  // 상태에 값 넣기: s["in"], s["out"] 배열과 센서 값 (s["temp"] = 23.5)
  typedef void (*StateFn)(JsonObject s);
  // 보드 명령: c = 명령, j = 받은 JSON, id = 요청 번호(없으면 null). 처리했으면 true, 모르는 명령이면 false
  typedef bool (*CommandFn)(const char *c, JsonObjectConst j, JsonVariantConst id);
  typedef void (*EventFn)();

  I2R_Link(I2R_Net &net, I2R_Time *time = nullptr) : _net(net), _time(time) { _self = this; }

  // ---- setup()에서, begin() 전에 ----
  void setBoard(const char *type, const char *ver, uint8_t ins, uint8_t outs) {
    _type = type; _ver = ver; _ins = ins; _outs = outs;
  }
  // 센서 이름(protocol.md 9.4: temp, humi, lux ...)과 상태를 다시 보낼 변화 폭
  void addSensor(const char *name, float step) {
    if (_nSensors >= MAX_SENSORS) return;
    _sensors[_nSensors] = name;
    _steps[_nSensors] = step;
    _sent[_nSensors] = NAN;
    _nSensors++;
  }
  void onState(StateFn fn) { _onState = fn; }
  void onCommand(CommandFn fn) { _onCmd = fn; }
  void onConnect(EventFn fn) { _onConn = fn; }        // 접속될 때마다, hello와 상태를 보낸 뒤
  void setLog(bool on) { _log = on; }                 // [받음], [보냄]을 시리얼에 찍기 (기본 켜짐)
  // MAC이 꼭 있어야 하는 명령을 더하기 (기본: so, st, setTime, bio, bs, sch)
  void addControl(const char *c) { if (_nExtra < 8) _extra[_nExtra++] = c; }

  // I2R_Net에 받기, 접속 함수를 건다. net.begin()보다 먼저 부른다
  void begin() {
    _net.onMessage(msgThunk);
    _net.onConnect(connThunk);
  }

  // loop()에서 매번. 바뀌었으면 묶어서, 아니면 60초마다 상태를 보낸다
  void update() {
    unsigned long now = millis();
    if (now - _lastCheck >= CHECK_MS) {
      _lastCheck = now;
      checkChanges();
    }
    if (!_net.mqttConnected()) { _up = false; return; }  // 끊겨 있으면 표시만 남기고, 다시 붙으면 접속 처리가 보낸다
    if (!_up) return;  // 접속 처리(hello, 상태)가 먼저 나가야 한다 (protocol.md 2장 순서)
    if (_dirtySince && now - _dirtySince >= COALESCE_MS) sendState();
    else if (now - _lastStateAt >= HEARTBEAT_MS) sendState();
  }

  // ---- 보드 명령 함수 안에서 쓰는 것 ----
  void markDirty() { if (!_dirtySince) _dirtySince = millis(); }

  // 상태 메시지 (protocol.md 4.1). 명령의 답이면 id를 넘긴다
  void sendState(JsonVariantConst id = JsonVariantConst()) {
    JsonDocument doc;
    doc["m"] = _net.mac();
    fillState(doc);
    doc["t"] = _type;
    doc["e"] = _net.group();
    if (!id.isNull()) doc["id"] = id;
    remember(doc);
    send(doc);
    _lastStateAt = millis();
    _dirtySince = 0;
  }

  // 오류 (protocol.md 5.6, 7장): {"c":"err","m","re","code","id"?}
  void sendErr(const char *re, const char *code, JsonVariantConst id = JsonVariantConst()) {
    JsonDocument doc;
    doc["c"] = "err";
    doc["m"] = _net.mac();
    doc["re"] = re;
    doc["code"] = code;
    if (!id.isNull()) doc["id"] = id;
    send(doc);
  }

  // 아무 메시지나 out으로 (fr을 붙임). toIn이면 같은 그룹의 다른 보드에게(in)
  bool send(JsonDocument &doc, bool toIn = false) {
    doc["fr"] = _net.mac();
    char buf[MSG_LIMIT];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf) - 1) {
      Serial.println("[I2R_Link] 보낼 메시지가 512바이트를 넘습니다");
      return false;
    }
    bool ok = toIn ? _net.publishIn(buf) : _net.publish(buf);
    if (ok && _log) {
      Serial.print(toIn ? "[보냄 in] " : "[보냄] ");
      Serial.println(buf);
    }
    return ok;
  }

  // ---- 값 검사 (시뮬레이터 board.py와 같은 규칙) ----
  static bool isInt(JsonVariantConst v) { return v.is<long>() && !v.is<bool>(); }
  static bool isNum(JsonVariantConst v) { return v.is<float>() && !v.is<bool>(); }
  static bool isOnOff(JsonVariantConst v) {
    return v.is<bool>() || (isInt(v) && (v.as<long>() == 0 || v.as<long>() == 1));
  }
  static bool onOff(JsonVariantConst v) { return v.is<bool>() ? v.as<bool>() : v.as<long>() == 1; }
  static float round1(float x) { return roundf(x * 10.0f) / 10.0f; }

  unsigned long connectedAt() const { return _connectedAt; }

 private:
  static I2R_Link *_self;
  static void msgThunk(const char *json, I2R_Source from) { if (_self) _self->onMessage(json, from); }
  static void connThunk() { if (_self) _self->onConnected(); }

  I2R_Net &_net;
  I2R_Time *_time;
  const char *_type = "i2r";
  const char *_ver = "0.0.0";
  uint8_t _ins = 0, _outs = 0;
  const char *_sensors[MAX_SENSORS];
  float _steps[MAX_SENSORS];
  float _sent[MAX_SENSORS];
  uint8_t _nSensors = 0;
  const char *_extra[8];
  uint8_t _nExtra = 0;
  StateFn _onState = nullptr;
  CommandFn _onCmd = nullptr;
  EventFn _onConn = nullptr;
  bool _log = true;
  bool _up = false;  // 이번 접속의 hello, 상태를 보냈는지
  unsigned long _connectedAt = 0, _dirtySince = 0, _lastStateAt = 0, _lastCheck = 0;
  String _sentIO;  // 마지막으로 보낸 "in","out" (바뀌었는지 비교)

  void fillState(JsonDocument &doc) {
    if (!_onState) return;
    // 빈 문서의 as<JsonObject>()는 쓸 수 없는 null 객체라, 비어 있으면 객체로 만든다
    JsonObject s = doc.is<JsonObject>() ? doc.as<JsonObject>() : doc.to<JsonObject>();
    _onState(s);
  }

  static String ioKey(JsonDocument &doc) {
    String k;
    serializeJson(doc["in"], k);
    k += '|';
    serializeJson(doc["out"], k);
    return k;
  }

  // 보낸 값 기억 (다음 변화 비교용)
  void remember(JsonDocument &doc) {
    _sentIO = ioKey(doc);
    for (uint8_t i = 0; i < _nSensors; i++) {
      JsonVariantConst v = doc[_sensors[i]];
      _sent[i] = isNum(v) ? v.as<float>() : NAN;
    }
  }

  // 입출력이 바뀌었거나 센서가 정한 폭 이상 바뀌었으면 보낼 표시
  void checkChanges() {
    if (!_onState) return;
    JsonDocument doc;
    fillState(doc);
    if (ioKey(doc) != _sentIO) { markDirty(); return; }
    for (uint8_t i = 0; i < _nSensors; i++) {
      JsonVariantConst v = doc[_sensors[i]];
      if (!isNum(v)) continue;  // 못 읽은 센서는 비교하지 않음
      if (isnan(_sent[i]) || fabsf(v.as<float>() - _sent[i]) >= _steps[i] - 0.001f) { markDirty(); return; }
    }
  }

  void sendHello() {
    JsonDocument doc;
    doc["c"] = "hello";
    doc["t"] = _type;
    doc["pv"] = PROTOCOL_VERSION;
    doc["ver"] = _ver;
    doc["in"] = _ins;
    doc["out"] = _outs;
    JsonArray s = doc["sensors"].to<JsonArray>();
    for (uint8_t i = 0; i < _nSensors; i++) s.add(_sensors[i]);
    send(doc);
  }

  void onConnected() {
    _connectedAt = millis();
    sendHello();
    sendState();
    _up = true;
    if (_onConn) _onConn();
  }

  bool isControl(const char *c) {
    static const char *list[] = {"so", "st", "setTime", "bio", "bs", "sch"};
    for (const char *x : list) if (!strcmp(c, x)) return true;
    for (uint8_t i = 0; i < _nExtra; i++) if (!strcmp(c, _extra[i])) return true;
    return false;
  }

  void logRx(const char *json) { if (_log) Serial.printf("[받음] %s\n", json); }

  // st / setTime: 시간대(tz, tzOffset, tz_offset)와 현지 시각(epoch) (protocol.md 5.2)
  void cmdSt(JsonObjectConst j, JsonVariantConst id) {
    if (_time) {
      JsonVariantConst tz = j["tz"];
      if (tz.isNull()) tz = j["tzOffset"];
      if (tz.isNull()) tz = j["tz_offset"];
      JsonVariantConst epoch = j["epoch"];
      if (isNum(epoch)) _time->setEpoch((time_t)epoch.as<double>(), isNum(tz) ? tz.as<long>() : _time->tz());
      else if (isNum(tz)) _time->setTz(tz.as<long>());
    }
    sendState(id);
  }

  // 받은 메시지 처리 순서 (protocol.md 3장)
  void onMessage(const char *json, I2R_Source from) {
    if (from == I2R_FROM_MQTT && millis() - _connectedAt < IGNORE_MS) return;      // 1
    if (strlen(json) > MSG_LIMIT) return sendErr("?", "too_long");                 // 2
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc.is<JsonObject>()) return;               // 3
    JsonObjectConst j = doc.as<JsonObjectConst>();
    if (_net.mac().equalsIgnoreCase(j["fr"] | "")) return;                         // 4
    const char *c = j["c"] | "";
    if (!*c || !strcmp(c, "lw") || !strcmp(c, "hello") || !strcmp(c, "err")) return;  // 5
    JsonVariantConst id = j["id"];
    JsonVariantConst mv = j["m"];
    const char *m = mv | "";
    if (!strcmp(c, "gs")) {                                                         // 6
      if (mv.isNull() || !strcmp(m, "*") || _net.mac().equalsIgnoreCase(m)) { logRx(json); sendState(id); }
      return;
    }
    if (mv.isNull()) {                                                              // 7
      if (isControl(c)) { logRx(json); sendErr(c, "no_m", id); }
      return;
    }
    if (strcmp(m, "*") != 0 && !_net.mac().equalsIgnoreCase(m)) return;            // 8
    logRx(json);                                                                    // 9
    if (!strcmp(c, "st") || !strcmp(c, "setTime")) return cmdSt(j, id);
    if (_onCmd && _onCmd(c, j, id)) return;
    sendErr(c, "unknown", id);
  }
};

inline I2R_Link *I2R_Link::_self = nullptr;

#endif
