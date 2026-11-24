#include <Arduino.h>
#include <SD.h>
#include <ArduinoJson.h>
#include "LlmOverride.h"
#include "StackchanExConfig.h"

#define LLM_OVERRIDE_PATH "/app/AiStackChanEx/llm.json"

static int    g_type  = LLM_TYPE_CHATGPT;
static String g_key   = "";
static String g_voice = "Zephyr";

void llm_override_init()
{
  if (!SD.exists(LLM_OVERRIDE_PATH)) {
    Serial.println("[llm] no llm.json — using YAML settings");
    return;
  }
  File f = SD.open(LLM_OVERRIDE_PATH, FILE_READ);
  if (!f) return;
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { Serial.printf("[llm] llm.json parse error: %s\n", err.c_str()); return; }

  const char* provider = doc["provider"] | "openai";
  g_type = (strcmp(provider, "gemini") == 0) ? LLM_TYPE_GEMINI : LLM_TYPE_CHATGPT;
  g_key   = String((const char*)(doc["key"]   | ""));
  g_voice = String((const char*)(doc["voice"] | "Zephyr"));
  Serial.printf("[llm] override: provider=%s voice=%s key=%s\n",
                provider, g_voice.c_str(), g_key.length() ? "(set)" : "(empty)");
}

int         llm_override_type()  { return g_type; }
const char* llm_override_key()   { return g_key.c_str(); }
const char* llm_override_voice() { return g_voice.c_str(); }

String llm_override_get_json(bool maskKey)
{
  DynamicJsonDocument doc(512);
  doc["provider"] = (g_type == LLM_TYPE_GEMINI) ? "gemini" : "openai";
  doc["voice"]    = g_voice;
  doc["hasKey"]   = g_key.length() > 0;
  if (!maskKey && g_key.length()) doc["key"] = g_key;
  String out; serializeJson(doc, out); return out;
}

bool llm_override_set_json(const String& json)
{
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, json)) return false;

  if (doc.containsKey("provider")) {
    const char* p = doc["provider"] | "openai";
    g_type = (strcmp(p, "gemini") == 0) ? LLM_TYPE_GEMINI : LLM_TYPE_CHATGPT;
  }
  if (doc.containsKey("key"))   g_key   = String((const char*)(doc["key"]   | ""));
  if (doc.containsKey("voice")) g_voice = String((const char*)(doc["voice"] | "Zephyr"));
  if (g_voice.length() == 0) g_voice = "Zephyr";

  // 저장: SD 에만 기록(git 저장소와 무관)
  DynamicJsonDocument out(1024);
  out["provider"] = (g_type == LLM_TYPE_GEMINI) ? "gemini" : "openai";
  out["key"]      = g_key;
  out["voice"]    = g_voice;

  File f = SD.open(LLM_OVERRIDE_PATH, FILE_WRITE);
  if (!f) { Serial.println("[llm] llm.json write failed"); return false; }
  serializeJson(out, f);
  f.close();
  Serial.printf("[llm] saved: provider=%s voice=%s\n",
                (g_type == LLM_TYPE_GEMINI) ? "gemini" : "openai", g_voice.c_str());
  return true;
}
