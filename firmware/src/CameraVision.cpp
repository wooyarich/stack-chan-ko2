#include <Arduino.h>
#include "CameraVision.h"
#include "Robot.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif
#if defined(ENABLE_CAMERA)
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "SpiRamJsonDocument.h"
#include "driver/Camera.h"
#endif

extern bool isOffline;

static volatile bool s_busy = false;
static String s_lastErr;
const char* camera_vision_last_error() { return s_lastErr.c_str(); }
bool camera_is_busy() { return s_busy; }
void camera_vision_init() {}

// 촬영 → base64 → 열려 있는 실시간 세션에 이미지 아이템으로 전송.
// (별도 HTTPS 는 TLS 핸드셰이크용 내부 RAM 이 부족해 실패하므로 사용하지 않는다.)
bool camera_vision_capture_and_send(const String& caption) {
#if defined(ENABLE_CAMERA) && defined(REALTIME_API)
    s_lastErr = "";
    if (isOffline) { s_lastErr = "offline"; return false; }
    if (robot == nullptr || robot->llm == nullptr) { s_lastErr = "no-llm"; return false; }
    if (s_busy) { s_lastErr = "busy"; return false; }
    s_busy = true;

    if (!camera_ensure_init()) {
        s_busy = false; s_lastErr = "camera-init-failed";
        return false;
    }
    String b64;
    bool ok = camera_capture_base64(b64);
    camera_release();          // 캡처 직후 해제 → HTTPS 도구용 내부 RAM 확보
    s_busy = false;
    if (!ok || b64.length() == 0) {
        s_lastErr = "capture-failed";
        Serial.println("[vision] capture failed");
        return false;
    }
    Serial.printf("[vision] captured %u bytes b64 (internal free=%u)\n",
                  (unsigned)b64.length(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    String cap = caption.length() ? caption
        : String("지금 카메라로 찍은 사진이야. 보이는 사람/주변을 사용자에게 자연스럽게 한국어로 한두 문장으로 말해줘. 사진에 없는 건 지어내지 마.");
    ((RealtimeLLMBase*)robot->llm)->pushImage(b64, cap);
    b64 = String();
    return true;
#else
    (void)caption;
    s_lastErr = "camera-disabled";
    return false;
#endif
}

// 웹 UI용: 촬영해서 세션에 넣고 로봇이 직접 말하게 한다.
bool camera_vision_look(const String& hint) {
    return camera_vision_capture_and_send(hint);
}
