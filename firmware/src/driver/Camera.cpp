#if defined(ENABLE_CAMERA)

#include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "Camera.h"
#include <SPIFFS.h>
#include <base64.h>
#include "DebugTools.h"
using namespace m5avatar;
extern Avatar avatar;


bool isSubWindowON = true;
bool isSilentMode = false;

static camera_config_t camera_config = {
    .pin_pwdn     = -1,
    .pin_reset    = -1,
    .pin_xclk     = 2,
    .pin_sscb_sda = 12,
    .pin_sscb_scl = 11,

    .pin_d7 = 47,
    .pin_d6 = 48,
    .pin_d5 = 16,
    .pin_d4 = 15,
    .pin_d3 = 42,
    .pin_d2 = 41,
    .pin_d1 = 40,
    .pin_d0 = 39,

    .pin_vsync = 46,
    .pin_href  = 38,
    .pin_pclk  = 45,

    .xclk_freq_hz = 20000000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    // GC0308 은 JPEG 출력을 지원하지 않아 RGB565 로 받아 frame2jpg 로 변환한다.
    .pixel_format = PIXFORMAT_RGB565,
    .frame_size   = FRAMESIZE_96X96,  // 96x96 — 내부 RAM 절약(프레임 18KB)
    .jpeg_quality = 45,               // frame2jpg 품질
    .fb_count     = 1,
    .fb_location  = CAMERA_FB_IN_PSRAM,
    .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
};

static bool s_cam_inited = false;

// 필요할 때만 초기화(내부 RAM 절약). 이미 초기화되어 있으면 그대로 true.
bool camera_ensure_init(void) {
  if (s_cam_inited) return true;
  bool ok = (camera_init() == ESP_OK);
  s_cam_inited = ok;
  Serial.printf("[camera] init %s (internal free=%u)\n", ok ? "ok" : "FAILED",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  return ok;
}

// 분석용 프레임 접근(호출자가 반드시 camera_frame_return).
camera_fb_t* camera_frame_get(void) {
  M5.In_I2C.release();
  return esp_camera_fb_get();
}
void camera_frame_return(camera_fb_t* fb) {
  if (fb) esp_camera_fb_return(fb);
}

// 캡처 후 해제해 내부 RAM 을 돌려준다.
void camera_release(void) {
  if (!s_cam_inited) return;
  esp_camera_deinit();
  s_cam_inited = false;
  Serial.printf("[camera] released (internal free=%u)\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

esp_err_t camera_init(void){

    //initialize the camera
    M5.In_I2C.release();
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        //Serial.println("Camera Init Failed");
        M5.Display.println("Camera Init Failed");
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    s->set_hmirror(s, 0);        // 左右反転

    return ESP_OK;
}

#if defined(ENABLE_FACE_DETECT)
static void draw_face_boxes(fb_data_t *fb, std::list<dl::detect::result_t> *results, int face_id)
{
    int x, y, w, h;
    uint32_t color = FACE_COLOR_YELLOW;
    if (face_id < 0)
    {
        color = FACE_COLOR_RED;
    }
    else if (face_id > 0)
    {
        color = FACE_COLOR_GREEN;
    }
    if(fb->bytes_per_pixel == 2){
        //color = ((color >> 8) & 0xF800) | ((color >> 3) & 0x07E0) | (color & 0x001F);
        color = ((color >> 16) & 0x001F) | ((color >> 3) & 0x07E0) | ((color << 8) & 0xF800);
    }
    int i = 0;
    for (std::list<dl::detect::result_t>::iterator prediction = results->begin(); prediction != results->end(); prediction++, i++)
    {
        // rectangle box
        x = (int)prediction->box[0];
        y = (int)prediction->box[1];

        // yが負の数のときにfb_gfx_drawFastHLine()でメモリ破壊してリセットする不具合の対策
        if(y < 0){
           y = 0;
        }

        w = (int)prediction->box[2] - x + 1;
        h = (int)prediction->box[3] - y + 1;

        //Serial.printf("x:%d y:%d w:%d h:%d\n", x, y, w, h);

        if((x + w) > fb->width){
            w = fb->width - x;
        }
        if((y + h) > fb->height){
            h = fb->height - y;
        }

        //Serial.printf("x:%d y:%d w:%d h:%d\n", x, y, w, h);

        //fb_gfx_fillRect(fb, x+10, y+10, w-20, h-20, FACE_COLOR_RED);  //モザイク
        fb_gfx_drawFastHLine(fb, x, y, w, color);
        fb_gfx_drawFastHLine(fb, x, y + h - 1, w, color);
        fb_gfx_drawFastVLine(fb, x, y, h, color);
        fb_gfx_drawFastVLine(fb, x + w - 1, y, h, color);

#if TWO_STAGE
        // landmarks (left eye, mouth left, nose, right eye, mouth right)
        int x0, y0, j;
        for (j = 0; j < 10; j+=2) {
            x0 = (int)prediction->keypoint[j];
            y0 = (int)prediction->keypoint[j+1];
            fb_gfx_fillRect(fb, x0, y0, 3, 3, color);
        }
#endif
    }
}
#endif  // ENABLE_FACE_DETECT (draw_face_boxes)

bool camera_capture_and_face_detect(void){
  bool isDetected = false;

  //acquire a frame
  M5.In_I2C.release();
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    //Serial.println("Camera Capture Failed");
    M5.Display.println("Camera Capture Failed");
    return ESP_FAIL;
  }

#if defined(ENABLE_FACE_DETECT)
  int face_id = 0;

#if TWO_STAGE
  HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
  HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
  std::list<dl::detect::result_t> &candidates = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
  std::list<dl::detect::result_t> &results = s2.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3}, candidates);
#else
  HumanFaceDetectMSR01 s1(0.3F, 0.5F, 10, 0.2F);
  std::list<dl::detect::result_t> &results = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
#endif


  
  if (results.size() > 0) {
    //Serial.printf("Face detected : %d\n", results.size());

    isDetected = true;

    fb_data_t rfb;
    rfb.width = fb->width;
    rfb.height = fb->height;
    rfb.data = fb->buf;
    rfb.bytes_per_pixel = 2;
    rfb.format = FB_RGB565;

    draw_face_boxes(&rfb, &results, face_id);

  }
#endif  //ENABLE_FACE_DETECT

  if(isSubWindowON){
    avatar.updateSubWindowCam565(fb->buf);
  }

  //Serial.println("\n<<< heap size before fb return >>>");  
  //check_heap_free_size();

  //return the frame buffer back to the driver for reuse
  esp_camera_fb_return(fb);

  //Serial.println("<<< heap size after fb return >>>");  
  //check_heap_free_size();

  return isDetected;
}



bool camera_capture_base64(String& out)
{
  //acquire a frame
  M5.In_I2C.release();
  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Camera Capture Failed");
    return false;
  }

  // JPEG 센서 출력이면 버퍼를 그대로 쓴다(변환 버퍼/내부 RAM 절약).
  uint8_t *jpg_buf     = fb->buf;
  size_t   jpg_buf_len = fb->len;
  uint8_t *conv_buf    = NULL;

  if (fb->format != PIXFORMAT_JPEG) {
    if (!frame2jpg(fb, 80, &conv_buf, &jpg_buf_len)) {
      esp_camera_fb_return(fb);
      Serial.println("JPEG compression failed");
      return false;
    }
    jpg_buf = conv_buf;
  }

  out = base64::encode(jpg_buf, jpg_buf_len);

  if (conv_buf) free(conv_buf);
  esp_camera_fb_return(fb);
  return out.length() > 0;
}

#endif
