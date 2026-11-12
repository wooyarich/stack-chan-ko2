#if defined(REALTIME_API)

#ifndef _REALTIME_LLM_BASE_H
#define _REALTIME_LLM_BASE_H

#include <Arduino.h>
#include <M5Unified.h>
#include "StackchanExConfig.h"
#include "SpiRamJsonDocument.h"
#include "ChatHistory.h"
#include "LLMBase.h"
#include <WebSocketsClient.h>

//#define REALTIME_API_RECORD_TEST

#define GEMINI_PROMPT_MAX_SIZE   (1024*50)

#define RT_REC_LENGTH       (2000)      //0.125s 
#define RT_REC_SAMPLE_RATE  (16000)

extern String InitBuffer;
extern const String json_ChatString;

class RealtimeLLMBase: public LLMBase{
//private:
public:   //本当はprivateにしたいところだがコールバック関数にthisポインタを渡して使うためにpublicとした
    WebSocketsClient webSocket;
    SpiRamJsonDocument msgDoc;

    // for record
    //
    //int16_t* rtRecBuf;
    int rtRecSamplerate;
    int rtRecLength;
    bool realtime_recording;
    bool user_talk_mode = false;   // 터치로 시작한 대화인지(연속 대화 모드)
    bool response_done;
    portTickType startTime;

#ifdef REALTIME_API_RECORD_TEST
    int16_t* recTestBuf;
    int recTestLenMax;
    int recTestLenCnt;
#endif

    // for play
    //
    uint8_t* audioBuf[2];    // Base64をデコードして得た音声データを格納するバッファ。再生直後に更新すると音が切れたのでダブルバッファとした
    int nextBufIdx;          // 次回データを格納するダブルバッファの面（0 or 1）

public:
    RealtimeLLMBase(llm_param_t param);

    virtual void chat(String text, const char *base64_buf = NULL) {};   //dummy
    virtual String& buildInputAudioJson(String& jsonBuf, String& base64) = 0;

    // Inject a proactive user-role message and trigger a response. Used for
    // scheduled greetings (midnight bedtime, weekday morning briefings).
    // Default no-op; concrete realtime LLMs override. MUST be cheap and
    // thread-safe: it is called from the loop/HTTP/scheduler task, NOT the
    // WebSocket task, so it may only enqueue — never touch webSocket directly.
    virtual void pushUserText(const String& text) {}

    // 촬영 이미지(base64 JPEG)를 user 메시지로 주입. pushUserText 와 같은 스레드 규칙(큐 적재만).
    virtual void pushImage(const String& base64_jpeg, const String& caption) {}

    // Called from the WebSocket task each iteration (after webSocket.loop()), so
    // it is the only safe place outside the event callback to touch webSocket.
    // Concrete realtime LLMs use it to drain queued proactive text and to run the
    // response-timeout watchdog. Default no-op.
    virtual void onWebSocketTick() {}

    void invokeWebSocketLoopTask(void);
    void suspendWebSocketLoopTask(void);
    void resumeWebSocketLoopTask(void);
    void webSocketProcess();
    int getAudioLevel();
    void startRealtimeRecord();
    void stopRealtimeRecord();
    void resetRealtimeRecordStartTime();
    portTickType checkRealtimeRecordTimeout();
    bool isRealtimeRecording() {return realtime_recording;};

    // 터치로 시작한 대화인지 여부(연속 대화 모드).
    // 터치로 청취를 시작하면 true 가 되어, 응답이 끝난 뒤에도 자동으로 청취를 재개한다.
    // 터치 없이 로봇이 먼저 말한 경우(false)에는 응답 후 청취로 넘어가지 않는다.
    bool isUserTalkMode() { return user_talk_mode; }
    void setUserTalkMode(bool on) { user_talk_mode = on; }

    int base64_decode(const char* input, int size, char* output);
    void hexdump(const void *mem, uint32_t len, uint8_t cols = 16);
    void streamAudioDelta(String& delta);

    // for TTS
    //
    String outputText;

};


#endif  //_REALTIME_LLM_BASE_H

#endif  //REALTIME_API