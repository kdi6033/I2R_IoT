# I2R_IoT

## I2R_IoT는 왜 있나요

**i2r 보드가 서버, 웹, 다른 보드와 이야기하는 방법(i2r 프로토콜)을 한 곳에서 처리하는 라이브러리**입니다.

| 왜 | 내용 |
|---|---|
| **한 문장으로 같은 코드** | "i2r-03 온도 30도 넘으면 출력 1 켜 줘"처럼 한 문장만 말해도 Claude가 **늘 같은 코드**를 짜려면, 사용자 코드가 토픽과 JSON을 직접 다루지 않아야 합니다. 프로토콜은 이 라이브러리가 맡고, 사용자 코드는 함수만 부릅니다 |
| **PLC와 Otto가 함께** | IoT PLC(i2r-02/03/04)와 Otto 로봇은 같은 서버, 같은 프로토콜을 씁니다. 통신 코드를 [I2R_PLC](https://github.com/kdi6033/I2R_PLC)와 [OttoI2R](https://github.com/kdi6033/OttoI2R)에 따로 두면 두 벌이 서로 어긋나므로, 공통 부분을 여기 **한 벌**만 둡니다 |
| **규격과 시험이 하나** | 프로토콜 원본은 [i2r-server/protocol.md](https://github.com/kdi6033/i2r-server/blob/main/protocol.md), 정답은 시뮬레이터 가상 보드, 시험은 `test_protocol.py`입니다. 이 라이브러리를 쓰는 보드는 모두 같은 시험을 통과합니다 |

```
 사용자 스케치 (키트의 my_sketches)   plc.output(1, ON), 내 규칙
        │
 I2R_PLC / OttoI2R                    보드 입출력, 보드 명령 (so, bio, sch, bs / ot ...)
        │
 I2R_IoT ── I2R_Link                  i2r 프로토콜 공통: 받는 순서, gs, st, hello, err, 상태 보내기
         ── I2R_Net                   Wi-Fi, MQTT, USB 설정(si), 끊김 알림(lw)
         ── I2R_Time                  시간 (NTP, 시간대)
        │
 MQTT 서버 ── 시뮬레이터, 대시보드, 다른 보드
```

> 🛠️ **만드는 중입니다 (0.3.0).** 함수를 하나씩 만들고 i2r-03 보드에서 확인하고 있습니다. 아래 표의 **상태**를 보세요.
> ✅ 보드 확인 완료 (i2r-03) · 🛠️ 만드는 중 (컴파일만 확인, 이름과 동작이 바뀔 수 있음)

보드 입출력은 다루지 않습니다. PLC(i2r-02, i2r-03, i2r-04)는 [I2R_PLC](https://github.com/kdi6033/I2R_PLC), 로봇은 [OttoI2R](https://github.com/kdi6033/OttoI2R)와 함께 씁니다.

## 설계 원칙

| 원칙 | 내용 |
|---|---|
| PLC를 멈추지 않음 | Wi-Fi와 MQTT는 ESP32 두 번째 코어(core 0)에서 돕니다. 서버에 닿지 않아도 `loop()`는 멈추지 않습니다 |
| 내가 고르는 서버 | 검증·교육은 공개 시험 서버(test.mosquitto.org:1883), 집 안은 i2r-05 로컬 서버, 어디서나는 아마존 개인 서버. 서버 주소를 비우면 Wi-Fi만 씁니다 |
| i2r.link는 쓰지 않음 | i2r.link(mqtt.i2r.link)는 공식 펌웨어 전용입니다. 설정에 그 주소가 오면 접속하지 않고 Wi-Fi만 저장합니다 |
| 공식 펌웨어 설정을 건드리지 않음 | 설정은 NVS `i2riot`에 따로 저장합니다. 공식 펌웨어를 다시 설치하면 원래 설정 그대로 i2r.link에 접속됩니다 (시간대 `i2r`만 함께 씀) |
| 개인 정보를 내보내지 않음 | 이메일은 저장하지 않습니다. 토픽은 `i2r/{그룹}/in`, `out`이고, 그룹 기본값은 MAC으로 만든 코드(`d4d4da73873c`)입니다 |
| 끊김은 서버가 알림 | MQTT 유언(lw)을 씁니다. 보드가 살아 있는 동안에는 추가 통신 없이, 전원이 꺼지거나 Wi-Fi가 끊기면 서버가 `{"c":"lw","fr":MAC,"on":0}`을 보냅니다. 화면은 이것으로 offline을 표시합니다 |
| 비밀 값은 코드에 없음 | Wi-Fi와 MQTT 비밀번호는 USB로 받아 보드에 저장하고, 시리얼에 찍지 않습니다 |

## 설치

Arduino IDE **Library Manager**에서 `I2R_IoT`를 찾아 설치합니다 (2026-11-20 등록, 목록에 나타나기까지 하루쯤 걸릴 수 있음). **PubSubClient**, **ArduinoJson**(7 이상)도 함께 설치됩니다.
아직 목록에 없으면 이 저장소를 `Documents/Arduino/libraries/I2R_IoT`에 받습니다.

보드 설정: ESP32 Dev Module, Flash Size 8MB, Partition Scheme Huge APP. esp32 보드 패키지 3.0 이상.

## 설정 보내기 (USB)

보드에 I2R_IoT 예제가 올라가 있을 때 아래 중 하나로 보냅니다. 저장되면 `{"c":"si","ok":true}` 응답 뒤 다시 시작합니다.

1. **PC 스크립트** (비밀번호가 화면에 보이지 않음, 포트를 스스로 찾음)
   ```
   powershell -ExecutionPolicy Bypass -File tools\send-si.ps1
   ```
   공유기 이름과 비밀번호를 넣은 뒤 서버를 번호로 고릅니다 (1 test.mosquitto.org, 2 broker.emqx.io, 3 직접 입력, 4 없음). 나머지를 모두 Enter로 넘기면 test.mosquitto.org:1883, 그룹은 MAC 기반이 됩니다. 시뮬레이터(i2r-server)와 같은 목록입니다.
2. **i2r.link 기기연결 → COM 포트 (USB)**: 이메일 칸은 비우고, 서버 칸에 `test.mosquitto.org`(또는 내 서버)를 넣습니다. 포트, 아이디, 그룹은 1번 방법으로만 넣을 수 있습니다.
3. **시리얼 모니터**(115200, 새 줄)에 한 줄:
   ```
   {"c":"si","ssid":"공유기","password":"비밀번호","mqttBroker":"test.mosquitto.org","mqttPort":1883,"mqttUser":"","mqttPass":"","g":""}
   ```

**MQTT나 웹으로 온 `si`는 받지 않습니다.** 남이 공개 서버로 내 보드 설정을 바꾸지 못하게 하기 위해서입니다.

## 함수

### 시간 `I2R_Time`

| 함수 | 설명 | 상태 |
|---|---|:---:|
| `itime.begin()` | `setup()`에서 한 번. 저장된 시간대를 읽고, 시계가 비어 있으면 마지막 저장 시각에서 이어 감 | 🛠️ |
| `itime.update()` | `loop()`에서 매번. Wi-Fi가 연결되면 NTP를 시작하고 시간을 가끔 저장 | 🛠️ |
| `itime.setTz(초)` | 시간대 정하기 (한국 `9 * 3600`). 저장하지 않음 | 🛠️ |
| `itime.valid()` | 시간이 맞는지 | 🛠️ |
| `itime.synced()`, `itime.restored()` | NTP로 맞췄는지 / 저장된 시각에서 이어 가는 중인지 | 🛠️ |
| `itime.now()` | 현지 시각 epoch | 🛠️ |
| `itime.local(tm)` | 현지 시각을 연, 월, 일, 요일로 나눠 받기 | 🛠️ |
| `itime.text()` | `"10:44:05"` | 🛠️ |
| `itime.hour()`, `minute()`, `second()`, `weekday()` | 현지 시, 분, 초, 요일(0 = 일요일) | 🛠️ |
| `itime.tz()` | 시간대 (초, 한국 = 32400) | 🛠️ |
| `itime.setEpoch(현지epoch, 시간대)` | 웹에서 보낸 시간 적용 (`st` 명령), NVS에 저장 | 🛠️ |

### 통신 `I2R_Net`

| 함수 | 설명 | 상태 |
|---|---|:---:|
| `net.begin()` | `setup()`에서 한 번. 설정이 있으면 통신 시작 (기다리지 않음), 없으면 통신 없이 동작 | 🛠️ |
| `net.update()` | `loop()`에서 매번. 받은 메시지를 내 함수로 넘기고 USB 설정 줄을 읽음 | 🛠️ |
| `net.onMessage(함수)` | 받은 메시지 `함수(json, from)`. `from`은 `I2R_FROM_MQTT`, `I2R_FROM_USB`, `I2R_FROM_WEB` | 🛠️ |
| `net.onConnect(함수)` | MQTT에 접속될 때마다 (상태를 다시 보낼 때) | 🛠️ |
| `net.publish(json)` | 내 메시지 보내기 (`i2r/{그룹}/out`). 끊겨 있으면 `false` | 🛠️ |
| `net.publishIn(json)` | 같은 그룹의 다른 보드에게 (`i2r/{그룹}/in`) | 🛠️ |
| `net.inject(json, from)` | 다른 통로(보드 홈페이지 등)에서 받은 명령을 같은 처리로 넘기기 | 🛠️ |
| `net.configured()`, `net.mqttConfigured()` | Wi-Fi 설정이 있는지 / MQTT 서버 설정이 있는지 | 🛠️ |
| `net.wifiConnected()`, `net.mqttConnected()` | 연결 상태 | 🛠️ |
| `net.mac()`, `ssid()`, `broker()`, `port()`, `user()`, `group()`, `ip()` | 기기 ID(MAC 대문자)와 저장된 설정 (비밀번호는 읽을 수 없음) | 🛠️ |
| `net.inTopic()`, `net.outTopic()` | 받기, 보내기 토픽 | 🛠️ |
| `net.fails()`, `net.lastRc()`, `I2R_Net::rcText(rc)` | 접속 실패 횟수와 원인 (원인은 읽을 수 있는 말로) | 🛠️ |
| `net.setRestartAfterFails(n)` | MQTT가 n번 연속 실패하면 다시 시작 (기본 0 = 안 함) | 🛠️ |
| `net.setLastWill(false)` | 끊김 알림(lw) 끄기. 기본은 켜짐: 접속하면 `{"c":"lw","fr":MAC,"on":1}`, 보드가 갑자기 사라지면 서버가 `"on":0`을 대신 보냄 (최대 약 45초 뒤) | 🛠️ |

### 프로토콜 `I2R_Link`

i2r 프로토콜의 공통 부분([protocol.md](https://github.com/kdi6033/i2r-server/blob/main/protocol.md) 2~5장)을 처리합니다. 보드는 **상태에 넣을 값**과 **보드 명령** 두 가지만 함수로 넘겨 줍니다.

| 함수 | 설명 | 상태 |
|---|---|:---:|
| `I2R_Link i2r(net, &itime)` | 만들기. 변수 이름 `link`는 시스템 함수와 겹치니 쓰지 않습니다 | 🛠️ |
| `i2r.setBoard(종류, 버전, 입력 수, 출력 수)` | `hello`에 들어갈 보드 정보 (`"i2r-03"`, `"0.2.0"`, 4, 4) | 🛠️ |
| `i2r.addSensor(이름, 폭)` | 센서 이름(`"temp"`)과 상태를 다시 보낼 변화 폭(0.2) | 🛠️ |
| `i2r.onState(함수)` | `함수(JsonObject s)`: `s["in"]`, `s["out"]` 배열과 센서 값을 넣음 (못 읽은 센서는 넣지 않음) | 🛠️ |
| `i2r.onCommand(함수)` | `함수(c, j, id)`: 보드 명령(PLC `so` 등). 처리했으면 `true`, 모르면 `false` → `err unknown` | 🛠️ |
| `i2r.onConnect(함수)` | 접속될 때마다 (`hello`와 상태를 보낸 뒤) | 🛠️ |
| `i2r.begin()` | `net.begin()`보다 먼저. 받기, 접속 함수를 `I2R_Net`에 겁니다 | 🛠️ |
| `i2r.update()` | `loop()`에서 매번. 바뀌면 0.2초 묶어서, 아니면 60초마다 상태를 보냄 | 🛠️ |
| `i2r.sendState(id)`, `i2r.sendErr(명령, 코드, id)` | 보드 명령의 답 (상태, 오류) | 🛠️ |
| `i2r.send(doc, toIn)` | 아무 메시지나 보내기 (`fr`을 붙임). `toIn`이면 같은 그룹의 다른 보드에게 | 🛠️ |
| `i2r.markDirty()` | 상태를 곧 다시 보내도록 표시 (입출력 변화는 저절로 알아챔) | 🛠️ |
| `I2R_Link::isInt`, `isNum`, `isOnOff`, `onOff`, `round1` | 받은 값 검사 (시뮬레이터와 같은 규칙) | 🛠️ |

`I2R_Link`가 저절로 처리하는 것: 접속 직후 5초 무시, 512바이트 넘으면 `too_long`, `fr`이 자기 MAC이면 무시, 다른 보드의 `lw`·`hello`·`err` 무시, `gs`(MAC 없어도 답함), 출력 명령에 MAC 없으면 `no_m`, 다른 보드 것은 조용히 무시, `st`/`setTime`, `id`를 답에 붙이기.

## 예제

| 예제 | 내용 | 상태 |
|---|---|:---:|
| 01_KoreaTime | 공유기에 접속해 인터넷에서 한국 시간을 받아 1초마다 시리얼에 보여 주기 | 🛠️ |
| MqttTest | test.mosquitto.org로 5초마다 상태 보내기, `{"c":"gs"}`, `{"c":"echo"}`에 답하기 | 🛠️ |
| 02_SimCheck | 실제 i2r-02/03/04가 시뮬레이터 가상 보드와 같은 메시지를 주고받기 (상태, `gs`, `so`, `st`, 오류). `I2R_Link`를 쓰는 기본 예제: 보드 쪽 코드는 상태 값(`fillState`)과 `so`(`onCommand`)뿐. 시뮬레이터 제어 화면에 나타나고 `test_protocol.py`로 검사. **I2R_PLC도 설치** | 🛠️ |

## 프로토콜 확인: i2r 시뮬레이터

내 보드가 i2r 프로토콜대로 동작하는지는 **i2r 시뮬레이터에서 확인합니다.** 켜는 방법은 [i2r-server: 시뮬레이터로 시작하기](https://github.com/kdi6033/i2r-server#-시뮬레이터로-시작하기)에 있습니다.

<img src="https://raw.githubusercontent.com/kdi6033/i2r-server/main/images/simulator.png" width="720" alt="i2r 시뮬레이터 화면: 위는 제어 화면, 아래는 가상 보드">

화면에서 이렇게 확인합니다.

| 볼 곳 | 확인할 것 |
|---|---|
| 제어 화면 맨 위 **보드 목록** | 내 보드(MAC)가 가상 보드와 나란히 나타나는지, **온라인**인지 |
| **스위치** 누르기 → **보낸 명령**, **받은 메시지** | `so` 명령이 나가고, 내 보드가 바뀐 상태(`"out":[1,...]`)로 답하는지 |
| 가상 보드 카드 | 정답 예시. 내 보드의 메시지가 가상 보드의 메시지와 같은 모양인지 |
| 보드 목록 맨 아래 **🔌 USB로 실제 보드 연결** | 프로그램 다운로드, Wi-Fi·서버 넣기, 시리얼 모니터 |

| 하려는 것 | 보는 곳 |
|---|---|
| 시뮬레이터 켜기 (Claude Code 한 줄, 직접 켜기) | [i2r-server: 시뮬레이터로 시작하기](https://github.com/kdi6033/i2r-server#-시뮬레이터로-시작하기) |
| 실제 보드에 02_SimCheck 올리기, Wi-Fi·서버 넣기, 시험 | [i2r-server: 실제 보드 연결하기](https://github.com/kdi6033/i2r-server#-실제-보드-연결하기) (방법 A Claude Code, B 직접, C 시뮬레이터 화면에서 USB로) |
| 메시지 형식 | [protocol.md](https://github.com/kdi6033/i2r-server/blob/main/protocol.md) |

**이 라이브러리 예제와 관련해 알아 둘 것**

- 시뮬레이터 제어 화면에는 상태 메시지에 `in`, `out`이 있는 보드만 나옵니다. **02_SimCheck**를 올린 보드는 나오고, MqttTest처럼 `in`, `out`을 보내지 않는 보드는 나오지 않습니다.
- 02_SimCheck는 시리얼 모니터에 시뮬레이터를 켤 명령(서버, 그룹)을 보여 줍니다.
- 02_SimCheck의 보드 LED(GPIO 2): **계속 켜짐** = 시작 중, Wi-Fi·MQTT 접속 중 / **1초에 한 번 깜박임** = 모두 접속됨.
- `test_protocol.py`로 시험하면 02_SimCheck는 지금 `gs`, `so`, `st`, 오류까지라서 `bio`, `sch`, `bs` 시험은 실패하는 것이 정상입니다 (다음 단계에서 I2R_PLC 쪽에 넣음).
- 공개 서버에서는 그룹과 MAC을 아는 사람이 보드에 명령을 보낼 수 있습니다. 내 보드의 그룹과 MAC을 공개된 곳에 적지 마세요.

## 다음에 만들 것

- `I2R_Web.h`: 보드 홈페이지 (stand alone, 입력 표시, 출력 스위치)
- `I2R_PLC_IoT.h` (I2R_PLC 쪽): `so`, `bio`, `sch`, `bs`를 `I2R_Link`의 보드 명령으로 (사용자 스케치가 몇 줄로 줄어듦)
- `OttoI2R_IoT.h` (OttoI2R 쪽): Otto 명령
- 8883 TLS + 아이디·비밀번호 (아마존 개인 서버)

통신 규격(원본): [i2r-server/protocol.md](https://github.com/kdi6033/i2r-server/blob/main/protocol.md)
