#include "UiTuning.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

#define UITUNE_SPIFFS_PATH "/uitune.json"

// 기본값은 이 포크에서 검증한 값(원본 대비 조정된 상태).
static bool g_clockEnabled = true;
static int  g_eyeTopMin    = 44;
static int  g_eyeIdleSec   = 6;
static int  g_servoEaseMs  = 600;
static int  g_servoAmpX    = 10;
static int  g_servoAmpY    = 7;
static int  g_listenSec    = 20;

static void clampAll()
{
    if (g_eyeTopMin   < 0)   g_eyeTopMin   = 0;
    if (g_eyeTopMin   > 100) g_eyeTopMin   = 100;
    if (g_eyeIdleSec  < 2)   g_eyeIdleSec  = 2;
    if (g_eyeIdleSec  > 60)  g_eyeIdleSec  = 60;
    if (g_servoEaseMs < 100) g_servoEaseMs = 100;
    if (g_servoEaseMs > 1500)g_servoEaseMs = 1500;
    if (g_servoAmpX   < 0)   g_servoAmpX   = 0;
    if (g_servoAmpX   > 20)  g_servoAmpX   = 20;
    if (g_servoAmpY   < 0)   g_servoAmpY   = 0;
    if (g_servoAmpY   > 20)  g_servoAmpY   = 20;
    if (g_listenSec   < 5)   g_listenSec   = 5;
    if (g_listenSec   > 60)  g_listenSec   = 60;
}

static void load_from_spiffs()
{
    if (!SPIFFS.exists(UITUNE_SPIFFS_PATH)) {
        Serial.println("[uitune] no uitune.json — defaults");
        return;
    }
    File f = SPIFFS.open(UITUNE_SPIFFS_PATH, "r");
    if (!f) return;
    String body = f.readString();
    f.close();
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, body)) {
        Serial.println("[uitune] parse error — defaults");
        return;
    }
    g_clockEnabled = doc["clock"]        | g_clockEnabled;
    g_eyeTopMin    = doc["eyeTopMin"]    | g_eyeTopMin;
    g_eyeIdleSec   = doc["eyeIdleSec"]   | g_eyeIdleSec;
    g_servoEaseMs  = doc["servoEaseMs"]  | g_servoEaseMs;
    g_servoAmpX    = doc["servoAmpX"]    | g_servoAmpX;
    g_servoAmpY    = doc["servoAmpY"]    | g_servoAmpY;
    g_listenSec    = doc["listenSec"]    | g_listenSec;
    clampAll();
    Serial.println("[uitune] loaded config from SPIFFS");
}

String ui_tuning_get_json()
{
    DynamicJsonDocument doc(512);
    doc["clock"]       = g_clockEnabled;
    doc["eyeTopMin"]   = g_eyeTopMin;
    doc["eyeIdleSec"]  = g_eyeIdleSec;
    doc["servoEaseMs"] = g_servoEaseMs;
    doc["servoAmpX"]   = g_servoAmpX;
    doc["servoAmpY"]   = g_servoAmpY;
    doc["listenSec"]   = g_listenSec;
    String out;
    serializeJson(doc, out);
    return out;
}

bool ui_tuning_set_json(const String& json)
{
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, json)) return false;
    if (doc.containsKey("clock"))       g_clockEnabled = doc["clock"];
    if (doc.containsKey("eyeTopMin"))   g_eyeTopMin    = doc["eyeTopMin"];
    if (doc.containsKey("eyeIdleSec"))  g_eyeIdleSec   = doc["eyeIdleSec"];
    if (doc.containsKey("servoEaseMs")) g_servoEaseMs  = doc["servoEaseMs"];
    if (doc.containsKey("servoAmpX"))   g_servoAmpX    = doc["servoAmpX"];
    if (doc.containsKey("servoAmpY"))   g_servoAmpY    = doc["servoAmpY"];
    if (doc.containsKey("listenSec"))   g_listenSec    = doc["listenSec"];
    clampAll();

    File f = SPIFFS.open(UITUNE_SPIFFS_PATH, "w");
    if (!f) { Serial.println("[uitune] SPIFFS open(w) failed"); return false; }
    f.print(ui_tuning_get_json());
    f.close();
    Serial.println("[uitune] saved config to SPIFFS");
    return true;
}

void ui_tuning_init()
{
    load_from_spiffs();
    Serial.println("[uitune] ready");
}

bool ui_clock_enabled()     { return g_clockEnabled; }
int  ui_eye_top_min()       { return g_eyeTopMin; }
int  ui_eye_idle_sec()      { return g_eyeIdleSec; }
int  ui_servo_ease_ms()     { return g_servoEaseMs; }
int  ui_servo_amp_x()       { return g_servoAmpX; }
int  ui_servo_amp_y()       { return g_servoAmpY; }
int  ui_listen_timeout_ms() { return g_listenSec * 1000; }
