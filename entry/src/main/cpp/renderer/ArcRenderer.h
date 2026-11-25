// ArcRenderer.h —— XComponent/EGL 渲染器 + AVPlayer 曲目状态机
// 输入：谱面（ChartPlayback + TimingModel）、音频文件路径；输出：透视角奏高速路画面。
#pragma once

#include "aff/AffTypes.h"
#include "aff/TimingModel.h"
#include "audio/SongPlayer.h"
#include "judge/NoteJudge.h"
#include "playback/ChartPlayback.h"
#include "render/PlayfieldMath.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace arc {

using aff::ChartPlayback;
using aff::Grade;
using aff::NoteJudge;
using aff::ParseResult;
using aff::TimingModel;

class ArcRenderer {
public:
    enum Phase : int32_t {
        Idle = 0,
        Preparing = 1,
        Ready = 2,
        Playing = 3,
        Paused = 4,
        Finished = 5,
        Error = 6,
    };

    struct PlayState {
        int32_t phase = Idle;
        int32_t time = 0;
        int32_t duration = 0;
        uint32_t combo = 0, maxCombo = 0, pure = 0, far = 0, lost = 0, total = 0;
        uint32_t judgeSerial = 0;
        int32_t lastGrade = 0;
        int32_t lastDelta = 0;
        int32_t score = 0;
        float gaugeF = 0.f; // Recollection Rate 0..100（Normal/Easy 从 0 起）
        int32_t gauge = 0;
        bool trackLost = false;
        bool audioReady = false;
        bool rendererReady = false;
        int32_t surfaceW = 0;
        int32_t surfaceH = 0;
        std::string error;
    };

    bool start(void* nativeWindow, int w, int h); // 建立 EGL + 启动帧循环线程
    void stop();                                  // 停线程 + 释放 EGL + 清空曲目
    bool running() const { return running_; }

    // 装载谱面 + 音频文件（异步 prepare）。返回 false 时 state().error 有原因。
    // gaugeKind: 0=Normal 1=Easy 2=Hard；chartConstant=谱面定数（Lv=floor(定数) 进 RF 分段）
    bool prepare(std::shared_ptr<const ParseResult> chart, const std::string& audioPath,
                 int gaugeKind = 0, float chartConstant = 0.f);
    void play();
    void pause();
    void restart();
    void clear();

    PlayState state() const;
    void setUserOffset(int32_t ms) { userOffsetMs_.store(ms); }
    void setNoteSpeed(float speed);
    float noteSpeed() const { return (float)noteSpeedX10_.load() / 10.f; }

    // 游玩背景（RGBA8888，UI 线程调用；渲染线程下一帧上传纹理）
    void setBackground(const void* rgba, size_t bytes, int w, int h);
    void clearBackground();

    // 触摸（XComponent UI 线程回调进来；内部加锁）
    void onTouchDown(int32_t id, float xNorm, float yNorm);
    void onTouchMove(int32_t id, float xNorm, float yNorm);
    void onTouchUp(int32_t id, float xNorm, float yNorm);

private:
    struct TouchPoint {
        int32_t id = 0;
        float x = 0.f;
        float y = 0.f;
    };
    void setTouch(int32_t id, float x, float y);
    void removeTouch(int32_t id);
    std::vector<float> skyTouchXs() const;
    void loop();
    bool initEgl(void* nativeWindow);
    void draw(int32_t tMs);
    void buildGeometry(int32_t tNow);
    void finalizeJudgeLocked();
    void uploadBackgroundIfNeeded();
    void drawBackground();

    // 动态几何（每帧重建；骨架阶段规模小，可接受）
    struct Vertex {
        float x, y, z, r, g, b, a;
    };
    std::vector<Vertex> verts_;
    void pushQuad(float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2,
                  float z2, float x3, float y3, float z3, float r, float g, float b, float a);
    void pushLineOnFloor(float x0, float z0, float x1, float z1, float w, float r, float g, float b,
                         float a);
    void pushRibbon(const std::vector<float>& xs, const std::vector<float>& ys,
                    const std::vector<int32_t>& ts, int32_t now, float halfW, float r, float g,
                    float b, float a);

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    GLuint program_ = 0;
    GLint uMvp_ = -1;
    GLuint bgProgram_ = 0;
    GLint bgUvScaleLoc_ = -1;
    GLint bgTexLoc_ = -1;
    GLuint bgTex_ = 0;
    int bgW_ = 0, bgH_ = 0;
    int width_ = 0, height_ = 0;

    mutable std::mutex bgMutex_;
    std::vector<uint8_t> bgPending_;
    int bgPendingW_ = 0, bgPendingH_ = 0;
    uint32_t bgSerial_ = 0;
    uint32_t bgUploadedSerial_ = 0;

    mutable     std::mutex chartMutex_;
    mutable std::mutex stateMutex_;
    std::shared_ptr<const ParseResult> chart_;
    std::unique_ptr<ChartPlayback> playback_;
    std::unique_ptr<TimingModel> timing_;
    SongPlayer song_;
    int32_t audioOffset_ = 0;
    int32_t endMs_ = 0;
    std::string error_;
    std::atomic<int32_t> phase_{Idle};
    std::atomic<int32_t> userOffsetMs_{0};
    std::atomic<int32_t> noteSpeedX10_{30};

    std::unique_ptr<NoteJudge> judge_;
    mutable std::mutex judgeMutex_;
    mutable std::mutex touchMutex_;
    std::vector<TouchPoint> touches_;
    std::atomic<int32_t> curT_{-1};
    int64_t lastJudgeFlashMs_ = -10000;
    uint8_t lastGrade_ = 0;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> surfaceReady_{false};
    std::atomic<int32_t> surfaceW_{0};
    std::atomic<int32_t> surfaceH_{0};
    float unitsPerMs_ = 0.0045f;
};

} // namespace arc
