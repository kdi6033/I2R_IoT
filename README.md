# I2R_IoT

> 🛠️ **만드는 중입니다 (0.1.0).** 함수를 하나씩 만들고 i2r-03 보드에서 확인하고 있습니다. 아래 표의 **상태**를 보세요.
> ✅ 보드 확인 완료 (i2r-03) · 🛠️ 만드는 중 (컴파일만 확인, 이름과 동작이 바뀔 수 있음)

i2r 보드(ESP32) **공통 통신 라이브러리**입니다. Wi-Fi, MQTT(TLS), 시간, i2r 통신 규격을 맡습니다.
보드 입출력은 다루지 않습니다. PLC(i2r-02, i2r-03, i2r-04)는 [I2R_PLC](https://github.com/kdi6033/I2R_PLC), 로봇은 [OttoI2R](https://github.com/kdi6033/OttoI2R)가 이 라이브러리를 가져다 씁니다.

## 설계 원칙

| 원칙 | 내용 |
|---|---|
| PLC를 멈추지 않음 | Wi-Fi와 MQTT는 ESP32 두 번째 코어(core 0)에서 돕니다. 서버에 닿지 않아도 `loop()`는 멈추지 않습니다 |
| 서버를 고치지 않음 | 공식 펌웨어와 같은 토픽(`i2r/{email}/in`, `out`), 접속 방식(8883 TLS, client id = MAC), 메시지 필드를 씁니다 |
| 설정이 이어짐 | 공식 펌웨어와 같은 NVS 자리(`netcfg`, `certs`, `i2r`)를 씁니다. 공식 펌웨어로 설정해 둔 보드에 올려도 다시 설정하지 않아도 됩니다 |
| 비밀 값은 코드에 없음 | Wi-Fi 비밀번호, 이메일은 USB로 받아 보드에 저장합니다. 인증서와 개인키는 라이브러리에 없습니다 |

## 설치

0.1.0은 Library Manager에 아직 없습니다. 이 저장소를 `Documents/Arduino/libraries/I2R_IoT`에 받고, Library Manager에서 **PubSubClient**, **ArduinoJson**(7 이상)을 설치합니다.

보드 설정: ESP32 Dev Module, Flash Size 8MB, Partition Scheme Huge APP. esp32 보드 패키지 3.0 이상.

## 함수

### 시간 `I2R_Time`

| 함수 | 설명 | 상태 |
|---|---|:---:|
| `itime.begin()` | `setup()`에서 한 번. 저장된 시간대를 읽고, 시계가 비어 있으면 마지막 저장 시각에서 이어 감 | 🛠️ |
| `itime.update()` | `loop()`에서 매번. Wi-Fi가 연결되면 NTP를 시작하고 시간을 가끔 저장 | 🛠️ |
| `itime.valid()` | 시간이 맞는지 (TLS 접속에 필요) | 🛠️ |
| `itime.synced()`, `itime.restored()` | NTP로 맞췄는지 / 저장된 시각에서 이어 가는 중인지 | 🛠️ |
| `itime.now()` | 현지 시각 epoch | 🛠️ |
| `itime.text()` | `"10:44:05"` | 🛠️ |
| `itime.hour()`, `minute()`, `second()`, `weekday()` | 현지 시, 분, 초, 요일(0 = 일요일) | 🛠️ |
| `itime.tz()` | 시간대 (초, 한국 = 32400) | 🛠️ |
| `itime.setEpoch(현지epoch, 시간대)` | 웹에서 보낸 시간 적용 (`st` 명령), NVS에 저장 | 🛠️ |

### 통신 `I2R_Net`

| 함수 | 설명 | 상태 |
|---|---|:---:|
| `net.begin()` | `setup()`에서 한 번. 설정이 있으면 통신 시작 (기다리지 않음), 없으면 통신 없이 동작 | 🛠️ |
| `net.update()` | `loop()`에서 매번. 받은 메시지를 내 함수로 넘기고 USB 설정 줄을 읽음 | 🛠️ |
| `net.onMessage(함수)` | 받은 메시지 `함수(json, from)`. `from`은 `I2R_FROM_MQTT` 또는 `I2R_FROM_USB` | 🛠️ |
| `net.onConnect(함수)` | MQTT에 접속될 때마다 (상태를 다시 보낼 때) | 🛠️ |
| `net.publish(json)` | 내 메시지 보내기 (`i2r/{email}/out`). 끊겨 있으면 `false` | 🛠️ |
| `net.publishIn(json)` | 같은 사용자의 다른 보드에게 (`i2r/{email}/in`) | 🛠️ |
| `net.configured()`, `net.hasCerts()` | 설정이 있는지 / 인증서가 있는지 | 🛠️ |
| `net.wifiConnected()`, `net.mqttConnected()` | 연결 상태 | 🛠️ |
| `net.mac()`, `email()`, `broker()`, `ssid()` | 기기 ID(MAC 대문자)와 저장된 설정 (비밀번호는 읽을 수 없음) | 🛠️ |
| `net.fails()`, `lastRc()`, `lastSslError()` | 접속 실패 횟수와 원인 | 🛠️ |
| `net.setTls(false)`, `net.setPort(1883)` | 로컬 서버용 (`begin()` 전에) | 🛠️ |
| `net.setRestartAfterFails(n)` | MQTT가 n번 연속 실패하면 다시 시작 (기본 0 = 안 함) | 🛠️ |

**USB 설정:** 시리얼(115200)로 `{"c":"si","ssid":"...","password":"...","e":"...","mqttBroker":"..."}` 한 줄을 보내면 저장하고 `{"c":"si","ok":true}` 응답 뒤 다시 시작합니다. i2r.link의 "COM 포트 설정 전송"과 같습니다. **MQTT로 온 `si`는 받지 않습니다.**

## 예제

| 예제 | 내용 | 상태 |
|---|---|:---:|
| NetCheck | Wi-Fi, MQTT, 시간을 5초마다 보기. 공유기를 꺼도 loop가 멈추지 않는지 확인 | 🛠️ |
| StateToDashboard | 공식 펌웨어와 같은 상태 메시지를 보내 i2r.link 화면에 뜨는지 확인 (릴레이는 안 움직임) | 🛠️ |

## 다음에 만들 것

- `I2R_Link.h`: 공통 프로토콜 (봉투 `m`, `e`, `t`, `fr`, 명령 `gs`, `st`, 상태 보내기, 보드 고유 명령은 `onCommand()`로)
- I2R_PLC의 `I2R_PLC_IoT.h`: 원격 `so` → `plc.output()`

통신 규격: [i2r 통신 프로토콜](https://github.com/kdi6033/i2r#-5-i2r-%ED%86%B5%EC%8B%A0-%ED%94%84%EB%A1%9C%ED%86%A0%EC%BD%9C)
