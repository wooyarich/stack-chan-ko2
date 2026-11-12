#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "BatteryReaction.h"
#include "Sfx.h"          // 충전/배터리부족 효과음 이벤트(음성토글과 무관)
#include "Gesture.h"
#include "IdleMotion.h"
#include "CameraVision.h"
#include "Robot.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif

using namespace m5avatar;
extern Avatar avatar;
extern bool isOffline;

#define BATT_SPIFFS_PATH  "/battery.json"

// loop-thread-only config (no mutex).
static bool   g_enabled        = true;
static int    g_lowThreshold   = 20;     // percent
static bool   g_lowReact       = true;
static int    g_lowIntervalMin = 15;
static bool   g_autoOffVbus    = false;  // 충전선(VBUS)이 빠지면 자동 종료(PC 전원 연동)
static String g_lowPrompt    = "지금 배터리가 얼마 안 남았어. 배고픈 듯이 '배고파~ 충전 좀 해줘' 하고 귀엽게 말해줘. 짧게.";

static void clampCfg() {
    if (g_lowThreshold < 1) g_lowThreshold = 1;
    if (g_lowThreshold > 99) g_lowThreshold = 99;
    if (g_lowIntervalMin < 1) g_lowIntervalMin = 1;
}

static bool is_speaking() {
#if defined(REALTIME_API)
    if (robot && robot->llm) return ((RealtimeLLMBase*)(robot->llm))->getAudioLevel() > 200;
#endif
    return false;
}

static bool speak(const String& p) {
#if defined(REALTIME_API)
    if (isOffline || robot == nullptr || robot->llm == nullptr) return false;
    ((RealtimeLLMBase*)(robot->llm))->pushUserText(p);
    return true;
#else
    return false;
#endif
}

static void load_from_spiffs() {
    if (!SPIFFS.exists(BATT_SPIFFS_PATH)) { Serial.println("[batt] no battery.json — defaults"); return; }
    File f = SPIFFS.open(BATT_SPIFFS_PATH, "r");
    if (!f) return;
    String body = f.readString(); f.close();
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, body)) { Serial.println("[batt] parse error — defaults"); return; }
    g_enabled        = doc["enabled"]        | g_enabled;
    g_lowThreshold   = doc["lowThreshold"]   | g_lowThreshold;
    g_lowReact       = doc["lowReact"]       | g_lowReact;
    g_lowIntervalMin = doc["lowIntervalMin"] | g_lowIntervalMin;
    g_autoOffVbus    = doc["autoOffVbus"]    | g_autoOffVbus;
    { const char* p = doc["lowPrompt"]    | ""; if (p && *p) g_lowPrompt = String(p); }
    clampCfg();
    Serial.println("[batt] loaded config from SPIFFS");
}

String battery_reaction_get_json() {
    DynamicJsonDocument doc(1024);
    doc["enabled"]        = g_enabled;
    doc["lowThreshold"]   = g_lowThreshold;
    doc["lowReact"]       = g_lowReact;
    doc["lowIntervalMin"] = g_lowIntervalMin;
    doc["autoOffVbus"]    = g_autoOffVbus;
    doc["lowPrompt"]      = g_lowPrompt;
    doc["level"]          = (int)M5.Power.getBatteryLevel();   // live (for UI)
    doc["charging"]       = M5.Power.isCharging();
#if defined(ARDUINO_M5STACK_CORES3)
    doc["vbus"]           = M5.Power.Axp2101.isVBUS();         // USB 전원 연결 여부(진단용)
#endif
    String out; serializeJson(doc, out); return out;
}

bool battery_reaction_set_json(const String& json) {
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, json)) return false;
    if (doc.containsKey("enabled"))        g_enabled        = doc["enabled"];
    if (doc.containsKey("lowThreshold"))   g_lowThreshold   = doc["lowThreshold"];
    if (doc.containsKey("lowReact"))       g_lowReact       = doc["lowReact"];
    if (doc.containsKey("lowIntervalMin")) g_lowIntervalMin = doc["lowIntervalMin"];
    if (doc.containsKey("autoOffVbus"))    g_autoOffVbus    = doc["autoOffVbus"];
    if (doc.containsKey("lowPrompt"))      { const char* p = doc["lowPrompt"]    | ""; g_lowPrompt = String(p); }
    clampCfg();
    File f = SPIFFS.open(BATT_SPIFFS_PATH, "w");
    if (!f) { Serial.println("[batt] SPIFFS open(w) failed"); return false; }
    f.print(battery_reaction_get_json());
    f.close();
    Serial.println("[batt] saved config to SPIFFS");
    return true;
}

void battery_reaction_tick() {
    static uint32_t lastCheckMs = 0;
    static uint32_t lastLowMs = 0;
    static uint32_t lastVbusMs = 0;
    static uint32_t vbusLostMs = 0;

    uint32_t now = millis();

    // 충전선(VBUS) 감시 — 1초 주기. PC 전원과 연동해 자동 종료한다.
    // (부팅 직후 5초는 PMIC 상태가 안정될 때까지 건너뛴다)
    if (g_autoOffVbus && now > 5000 && now - lastVbusMs >= 1000) {
        lastVbusMs = now;
#if defined(ARDUINO_M5STACK_CORES3)
        bool vbus = M5.Power.Axp2101.isVBUS();
#else
        bool vbus = M5.Power.getVBUSVoltage() > 4000;
#endif
        if (vbus) {
            vbusLostMs = 0;
        } else if (vbusLostMs == 0) {
            vbusLostMs = now;
        } else if (now - vbusLostMs >= 3000) {
            Serial.println("[batt] VBUS lost 3s -> power off");
            vbusLostMs = 0;
            M5.Power.powerOff();
        }
    }

    if (now - lastCheckMs < 30000 && lastCheckMs != 0) return;   // every 30s
    lastCheckMs = now;

    if (!g_enabled) return;
    if (camera_is_busy()) return;   // camera owns the internal I2C (AXP) during capture

    int level = (int)M5.Power.getBatteryLevel();
    bool charging = M5.Power.isCharging();

    // Low battery (not charging) -> 효과음 "low"(매핑 시) + (옵션)음성 "배고파". 간격 제한 공유.
    if (!charging && level > 0 && level <= g_lowThreshold) {
        if (now - lastLowMs >= (uint32_t)g_lowIntervalMin * 60000UL) {
            lastLowMs = now;
            sfx_play_event("low");
            if (g_lowReact && !is_speaking()) {
                avatar.setExpression(Expression::Sad);
                gesture_play(Expression::Sad);
                idle_motion_hold(2500);
                speak(g_lowPrompt);
                Serial.printf("[batt] low reaction (level=%d)\n", level);
            }
        }
    }
}

void battery_reaction_init() {
    load_from_spiffs();
    Serial.println("[batt] ready (polled from loop)");
}
