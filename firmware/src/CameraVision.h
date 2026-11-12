#ifndef _CAMERA_VISION_H
#define _CAMERA_VISION_H

#include <Arduino.h>

// Realtime 모드 카메라: 촬영한 JPEG(base64)를 이미 열려 있는 실시간 세션에
// 아이템으로 넣어 모델이 직접 사진을 보게 한다.
// (별도 REST 호출 방식은 TLS 핸드셰이크용 내부 RAM 부족으로 실패해서 쓰지 않는다.)
//
// ENABLE_CAMERA 빌드에서만 동작하고, 그 외에는 no-op(false)라 호출부는 그대로 둔다.
// ⚠️ 카메라 SCCB는 근접/IMU/전원/터치와 내부 I2C를 공유한다. 캡처 중에는
//    camera_is_busy()가 true가 되어 해당 센서들이 I2C 읽기를 건너뛴다.

void camera_vision_init();
bool camera_vision_look(const String& hint);              // 웹 UI용: 촬영 + 세션 주입
bool camera_vision_capture_and_send(const String& caption);  // LLM 툴용: 촬영+이미지 전송
const char* camera_vision_last_error();                   // 마지막 실패 사유(진단용)
bool camera_is_busy();                                    // sensors skip internal-I2C while true

#endif  // _CAMERA_VISION_H
