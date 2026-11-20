# I2R_IoT

> 🛠️ **만드는 중입니다 (0.2.2).** 함수를 하나씩 만들고 i2r-03 보드에서 확인하고 있습니다. 아래 표의 **상태**를 보세요.
> ✅ 보드 확인 완료 (i2r-03) · 🛠️ 만드는 중 (컴파일만 확인, 이름과 동작이 바뀔 수 있음)

i2r 보드(ESP32) **공통 통신 라이브러리**입니다. Wi-Fi, MQTT, 시간, 설정 받기를 맡습니다.
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

아직 Library Manager에 없습니다. 이 저장소를 `Documents/Arduino/libraries/I2R_IoT`에 받고, Library Manager에서 **PubSubClient**, **ArduinoJson**(7 이상)을 설치합니다.

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

## 예제

| 예제 | 내용 | 상태 |
|---|---|:---:|
| 01_KoreaTime | 공유기에 접속해 인터넷에서 한국 시간을 받아 1초마다 시리얼에 보여 주기 | 🛠️ |
| MqttTest | test.mosquitto.org로 5초마다 상태 보내기, `{"c":"gs"}`, `{"c":"echo"}`에 답하기 | 🛠️ |
| 02_SimCheck | 실제 i2r-02/03/04가 시뮬레이터 가상 보드와 같은 메시지를 주고받기 (상태, `gs`, `so`, `st`, 오류). 시뮬레이터 제어 화면에 나타나고 `test_protocol.py`로 검사. **I2R_PLC도 설치** | 🛠️ |

## 프로토콜 확인: i2r 시뮬레이터

i2r 프로토콜은 [i2r-server의 시뮬레이터](https://github.com/kdi6033/i2r-server/tree/main/sim)로 확인합니다.
시뮬레이터는 PC에 **가상 i2r 보드**와 **제어 화면**을 띄웁니다. 가상 보드는 실제 보드 펌웨어와 같은 메시지를 주고받으므로, 내 보드가 보내야 할 메시지의 **정답 예시**가 됩니다.

**1. 준비 (처음 한 번)**

- [Python 3](https://www.python.org/downloads/) 설치 (Windows는 **Add python.exe to PATH** 체크)
- `git clone https://github.com/kdi6033/i2r-server` (또는 Code → Download ZIP)
- `python -m pip install "paho-mqtt>=2.0"`

**2. 켜기**

```
cd i2r-server/sim
python sim.py --boards 2
```

Windows는 `sim/start-sim.bat`을 두 번 눌러도 됩니다. 브라우저에 **http://localhost:8000** 이 열립니다. 끌 때는 창에서 **Ctrl+C**.

**3. 메시지 보기**

| 화면 | 보이는 것 |
|---|---|
| 가상 보드 카드 | **IN1~IN4**를 누르면 상태 메시지(`{"m":..,"in":[..],"out":[..],...}`)가 나가고, 받은 명령과 보낸 메시지가 카드 아래에 보입니다 |
| 제어 화면 | 스위치를 누르면 `so` 명령을 보내고, 보드가 보낸 상태로 화면이 바뀝니다. 입력, 스위치 이름, 온도·습도를 누르면 `bio`, `sch`, `bs` 설정 창이 열립니다 |

**4. 내 보드와 함께 보기**

시뮬레이터를 **내 보드와 같은 서버, 같은 그룹**으로 켜면 (서버는 `send-si.ps1`에서 고른 번호와 같은 것) 내 보드도 제어 화면의 보드 목록에 나타납니다. 그룹은 보드 시리얼 모니터에 나오는 토픽 `i2r/<그룹>/out`의 가운데 부분입니다.

```
python sim.py --server test.mosquitto.org --group <내 보드 그룹>
```

- 제어 화면에는 상태 메시지에 `in`, `out`이 있는 보드만 나옵니다. **02_SimCheck** 예제를 올린 보드는 나오고, MqttTest처럼 `in`, `out`을 보내지 않는 보드는 나오지 않습니다.
- 02_SimCheck는 시리얼 모니터에 시뮬레이터를 켤 명령(서버, 그룹)을 그대로 보여 줍니다.
- 02_SimCheck의 보드 LED(GPIO 2): **계속 켜짐** = 시작 중, Wi-Fi·MQTT 접속 중 / **1초에 한 번 깜박임** = Wi-Fi와 MQTT 모두 접속됨.
- 규칙 시험: `python test_protocol.py --server test.mosquitto.org --group <그룹> --mac <MAC>`. 02_SimCheck는 지금 `gs`, `so`, `st`, 오류까지라서 `bio`, `sch`, `bs` 시험은 실패하는 것이 정상입니다. 공통 부분은 확인이 끝나면 `I2R_Link.h`로 옮깁니다.

**주의**: test.mosquitto.org는 공개 서버라 누구나 토픽을 보고 명령을 보낼 수 있습니다. 내 보드의 그룹과 MAC을 공개된 곳(GitHub, 카페 글)에 적지 말고, 개인 정보를 메시지에 넣지 마세요. 시뮬레이터만 쓸 때는 그룹을 정하지 않으면 실행할 때마다 `sim-` 뒤 임의 코드로 바뀝니다.

## 다음에 만들 것

- `I2R_Web.h`: 보드 홈페이지 (stand alone, 입력 표시, 출력 스위치)
- `I2R_Link.h`: 공통 프로토콜 (봉투 `m`, `e`, `t`, `fr`, MAC 규칙, `gs`, `st`, `onCommand()`)
- 8883 TLS + 아이디·비밀번호 (아마존 개인 서버)

통신 규격: [i2r 통신 프로토콜](https://github.com/kdi6033/i2r#-5-i2r-%ED%86%B5%EC%8B%A0-%ED%94%84%EB%A1%9C%ED%86%A0%EC%BD%9C)
