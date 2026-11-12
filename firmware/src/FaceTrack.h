#ifndef _FACE_TRACK_H
#define _FACE_TRACK_H

#include <Arduino.h>

// 간이 얼굴 추적: 일정 주기로 카메라를 켜서 1프레임만 분석하고 즉시 해제한다.
// (상시 카메라는 내부 RAM 49KB 를 점유해 HTTPS 도구를 막으므로 샘플링 방식)
// 피부색 무게중심을 얼굴로 보고 눈·머리가 그쪽을 바라보게 한다.
void   face_track_init();
void   face_track_tick();
String face_track_get_json();
bool   face_track_set_json(const String& json);

#endif  // _FACE_TRACK_H
