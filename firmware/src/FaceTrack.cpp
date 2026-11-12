#include <Arduino.h>
#include "FaceTrack.h"
#include "CameraVision.h"
#include "face/RoboEyesView.h"
#include "NightMode.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#if defined(ENABLE_CAMERA)
#include "driver/Camera.h"
#endif

#define FACETRACK_PATH "/facetrack.json"

static bool   g_enabled     = false;   // 카메라 RAM 제약으로 기본 비활성(웹/API 로 켤 수 있음)
static int    g_intervalSec = 4;
static int    g_minCovPct   = 2;
static uint32_t g_lastMs    = 0;
static bool     s_camHeld   = false;   // 추적 중에는 카메라를 켜둔다(반복 init/deinit 이 크래시 유발)

#if defined(ENABLE_CAMERA)
// RGB565 한 프레임에서 피부색 픽셀의 무게중심을 찾는다(간이 얼굴 추정).
// 성공 시 nx/ny(-1..1, 화면 기준)와 점유율(%)을 돌려준다.
static bool find_face_rgb565(const uint8_t* buf, int w, int h, float* nx, float* ny, int* covPct)
{
    const uint16_t* px = (const uint16_t*)buf;
    const int step = 2;                       // 2px 간격 샘플링
    long sx = 0, sy = 0, n = 0, total = 0;
    for (int y = 0; y < h; y += step) {
        const uint16_t* row = px + (size_t)y * w;
        for (int x = 0; x < w; x += step) {
            uint16_t p = row[x];
            int r = ((p >> 11) & 0x1F) << 3;
            int g = ((p >> 5)  & 0x3F) << 2;
            int b = ( p        & 0x1F) << 3;
            total++;
            int mn = min(r, min(g, b));
            if (r > 95 && g > 40 && b > 20 && r > g && r > b &&
                (r - mn) > 15 && abs(r - g) > 15) {
                sx += x; sy += y; n++;
            }
        }
    }
    if (total == 0) return false;
    int cov = (int)((n * 100) / total);
    if (cov < 1 || cov > 70) return false;    // 너무 적으면 없음, 너무 많으면 벽/나무
    *nx = ((float)sx / (float)n) / (w * 0.5f) - 1.0f;   // -1(좌) .. +1(우)
    *ny = ((float)sy / (float)n) / (h * 0.5f) - 1.0f;   // -1(위) .. +1(아래)
    *covPct = cov;
    return true;
}
#endif

void face_track_init()
{
#if defined(ENABLE_CAMERA)
    if (SPIFFS.exists(FACETRACK_PATH)) {
        File f = SPIFFS.open(FACETRACK_PATH, "r");
        if (f) {
            DynamicJsonDocument doc(256);
            if (!deserializeJson(doc, f.readString())) {
                g_enabled     = doc["enabled"]     | g_enabled;
                g_intervalSec = doc["intervalSec"] | g_intervalSec;
                g_minCovPct   = doc["minCovPct"]   | g_minCovPct;
            }
            f.close();
        }
    }
#endif
    if (g_intervalSec < 2)  g_intervalSec = 2;
    if (g_intervalSec > 30) g_intervalSec = 30;
    Serial.printf("[facetrack] ready (enabled=%d interval=%ds)\n", (int)g_enabled, g_intervalSec);
}

String face_track_get_json()
{
    DynamicJsonDocument doc(256);
    doc["enabled"]     = g_enabled;
    doc["intervalSec"] = g_intervalSec;
    doc["minCovPct"]   = g_minCovPct;
    String out; serializeJson(doc, out); return out;
}

bool face_track_set_json(const String& json)
{
    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, json)) return false;
    if (doc.containsKey("enabled"))     g_enabled     = doc["enabled"];
    if (doc.containsKey("intervalSec")) g_intervalSec = doc["intervalSec"];
    if (doc.containsKey("minCovPct"))   g_minCovPct   = doc["minCovPct"];
#if defined(ENABLE_CAMERA)
    // 추적을 끄면 카메라를 해제해 RAM 을 돌려준다.
    if (!g_enabled && s_camHeld) { camera_release(); s_camHeld = false; }
#endif
    if (g_intervalSec < 2)  g_intervalSec = 2;
    if (g_intervalSec > 30) g_intervalSec = 30;
    if (g_minCovPct < 1)    g_minCovPct = 1;
    if (g_minCovPct > 40)   g_minCovPct = 40;
#if defined(ENABLE_CAMERA)
    File f = SPIFFS.open(FACETRACK_PATH, "w");
    if (!f) return false;
    f.print(face_track_get_json());
    f.close();
#endif
    return true;
}

void face_track_tick()
{
#if defined(ENABLE_CAMERA)
    if (!g_enabled) {
        if (s_camHeld) { camera_release(); s_camHeld = false; }
        return;
    }
    if (night_mode_is_night()) return;
    if (camera_is_busy()) return;
    uint32_t now = millis();
    if (now - g_lastMs < (uint32_t)g_intervalSec * 1000UL) return;
    g_lastMs = now;

    if (!s_camHeld) {
        if (!camera_ensure_init()) { Serial.println("[facetrack] camera init failed"); return; }
        s_camHeld = true;      // 이후 샘플은 재초기화 없이 프레임만 읽는다
    }
    camera_fb_t* fb = camera_frame_get();
    float nx = 0, ny = 0; int cov = 0; bool found = false;
    if (fb && fb->format == PIXFORMAT_RGB565 && fb->buf) {
        found = find_face_rgb565(fb->buf, fb->width, fb->height, &nx, &ny, &cov);
    }
    if (fb) camera_frame_return(fb);

    if (found && cov >= g_minCovPct) {
        roboeyes_view_look_at(nx, ny);
        Serial.printf("[facetrack] face nx=%.2f ny=%.2f cov=%d%%\n", nx, ny, cov);
    }
#endif
}
