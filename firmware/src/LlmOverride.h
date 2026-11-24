#ifndef _LLM_OVERRIDE_H
#define _LLM_OVERRIDE_H

#include <Arduino.h>

// AI 서비스 선택(OpenAI / Gemini)과 API 키를 SD 의 /app/AiStackChanEx/llm.json 에만
// 보관한다. 저장소(git)에는 어떤 키도 들어가지 않는다.
// 부팅 시 YAML 설정을 읽은 뒤 이 값으로 llm.type / apikey.aiservice 를 덮어쓴다.
void        llm_override_init();          // SD 에서 로드(없으면 기본값)
int         llm_override_type();          // LLM_TYPE_CHATGPT(0) / LLM_TYPE_GEMINI(3)
const char* llm_override_key();           // "" 이면 YAML 값 사용
const char* llm_override_voice();         // Gemini 보이스 이름(기본 Zephyr)
String      llm_override_get_json(bool maskKey);
bool        llm_override_set_json(const String& json);   // 저장(SD) + 즉시 반영

#endif  // _LLM_OVERRIDE_H
