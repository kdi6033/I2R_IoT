// I2R_IoT: i2r 보드(ESP32) 공통 통신 라이브러리
//
// 이 파일 하나로 아래를 모두 불러온다.
//   I2R_Time.h  시간 (NTP, 시간대, 꺼졌다 켜져도 이어 가기)
//   I2R_Net.h   Wi-Fi, MQTT(1883, 아이디·비밀번호), 설정 받기, 기기 ID(MAC)
//
// 보드 입출력은 다루지 않는다. PLC는 I2R_PLC, 로봇은 OttoI2R가 이 라이브러리를 가져다 쓴다.
// 통신 규격: https://github.com/kdi6033/i2r (protocol)

#ifndef I2R_IOT_H
#define I2R_IOT_H

#include "I2R_Time.h"
#include "I2R_Net.h"

#endif
