// I2R_Time: i2r 보드 공통 시간 (NTP, 시간대, 꺼졌다 켜져도 이어 가기)
//
//   I2R_Time itime;
//   setup(): itime.begin();
//   loop():  itime.update();      // 기다리지 않음
//   itime.valid()  → 시간이 맞는지 (TLS 접속에 필요)
//   itime.text()   → "10:44:05" (현지 시각)
//
// 공식 펌웨어(v2.0)와 같은 저장 자리를 쓴다: NVS "i2r" 이름공간의 tz_offset(초), savedTime(현지 epoch).
// 그래서 공식 펌웨어에서 맞춘 시간대가 그대로 이어진다.
// 시스템 시계는 UTC로 두고, 현지 시각은 now() = UTC + 시간대로 계산한다.

#ifndef I2R_TIME_H
#define I2R_TIME_H

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"

class I2R_Time {
public:
  // 2026-02-03 00:00 UTC (i2r 서버 인증서 시작일). 이보다 이르면 시간이 아직 없는 것으로 본다.
  static const time_t VALID_AFTER = 1770076800;

  // setup()에서 한 번. 저장된 시간대를 읽고, 시계가 비어 있으면 마지막으로 저장한 시각에서 이어 간다.
  void begin() {
    Preferences p;
    if (p.begin("i2r", true)) {
      _tz = p.getLong("tz_offset", 9 * 3600);
      _saved = p.getULong("savedTime", 0);
      p.end();
    }
    if (time(nullptr) < VALID_AFTER && (time_t)_saved > VALID_AFTER) {
      setSystem((time_t)_saved - _tz + 2);  // 다시 켜는 데 걸린 약 2초를 더한다 (v2.0과 같음)
      _restored = true;
    }
  }

  // loop()에서 매번. Wi-Fi가 처음 연결되면 NTP를 시작하고, 시간을 받으면 가끔 NVS에 저장한다.
  void update() {
    if (!_sntpStarted && WiFi.status() == WL_CONNECTED) {
      sntp_set_time_sync_notification_cb(onSync);
      configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
      _sntpStarted = true;
    }
    if (_syncFlag) {
      _syncFlag = false;
      _synced = true;
      _restored = false;
      // NVS가 닳지 않게 6시간에 한 번만 저장한다 (NTP는 한 시간마다 다시 맞춘다)
      if (!_savedOnce || millis() - _lastSave >= 6UL * 3600 * 1000) saveNow();
    }
  }

  bool valid() const { return time(nullptr) >= VALID_AFTER; }  // 시간이 맞는지
  bool synced() const { return _synced; }        // 이번에 켜진 뒤 NTP로 맞췄는지
  bool restored() const { return _restored; }    // NTP 없이 저장된 시각에서 이어 가는 중인지
  long tz() const { return _tz; }                // 시간대 (초, 한국 = 32400)

  time_t now() const { return time(nullptr) + _tz; }  // 현지 시각 epoch (v2.0의 lastNtpTime과 같은 뜻)

  void local(struct tm &t) const {
    time_t n = now();
    gmtime_r(&n, &t);
  }
  int hour() const { struct tm t; local(t); return t.tm_hour; }
  int minute() const { struct tm t; local(t); return t.tm_min; }
  int second() const { struct tm t; local(t); return t.tm_sec; }
  int weekday() const { struct tm t; local(t); return t.tm_wday; }  // 0 = 일요일

  // "10:44:05"
  String text() const {
    struct tm t;
    local(t);
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
  }

  // 웹에서 보낸 시간 적용 (st / setTime 명령: 현지 epoch와 시간대). v2.0처럼 NVS에도 저장한다.
  bool setEpoch(time_t localEpoch, long tzOffset) {
    if (localEpoch <= 1000000000) return false;  // v2.0과 같은 검사
    _tz = tzOffset;
    setSystem(localEpoch - tzOffset);
    Preferences p;
    if (p.begin("i2r", false)) {
      p.putLong("tz_offset", _tz);
      p.putULong("savedTime", (uint32_t)localEpoch);
      p.end();
    }
    _restored = false;
    return true;
  }

private:
  long _tz = 9 * 3600;
  uint32_t _saved = 0;
  bool _sntpStarted = false;
  bool _synced = false;
  bool _restored = false;
  bool _savedOnce = false;
  unsigned long _lastSave = 0;
  inline static volatile bool _syncFlag = false;

  static void onSync(struct timeval *) { _syncFlag = true; }  // NTP 작업에서 불림: 표시만 한다

  static void setSystem(time_t utc) {
    struct timeval tv;
    tv.tv_sec = utc;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
  }

  void saveNow() {
    Preferences p;
    if (p.begin("i2r", false)) {
      p.putULong("savedTime", (uint32_t)now());
      p.end();
    }
    _savedOnce = true;
    _lastSave = millis();
  }
};

#endif
