#ifndef _UI_TUNING_H
#define _UI_TUNING_H

#include <Arduino.h>

// 화면·움직임 튜닝 값. 웹 설정(/uitune_set)에서 조정하고 SPIFFS(/uitune.json)에 저장한다.
// 렌더 태스크·서보 태스크·WebSocket 태스크가 동시에 읽으므로 정렬된 int/bool 읽기만 사용한다.

void   ui_tuning_init();
String ui_tuning_get_json();
bool   ui_tuning_set_json(const String& json);

bool ui_clock_enabled();      // 상단 시계 표시
int  ui_eye_top_min();        // 눈 상단 하한 y(px) — 시계 영역 보호
int  ui_eye_idle_sec();       // 시선 자동 이동 기본 주기(초)
int  ui_servo_ease_ms();      // 시선 추종 서보 이징(ms)
int  ui_servo_amp_x();        // 좌우 진폭(도)
int  ui_servo_amp_y();        // 상하 진폭(도)
int  ui_listen_timeout_ms();  // 무음 청취 타임아웃(ms)

#endif  // _UI_TUNING_H
