// ArcRenderer.cpp —— EGL 建立、着色器、几何生成与帧循环
#include "renderer/ArcRenderer.h"

#include <hilog/log.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

namespace arc {

using aff::Arc;
using aff::Chart;
using aff::RenderNote;

namespace {

constexpr const char* kVertSrc = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uMvp;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uMvp * vec4(aPos, 1.0);
})";

constexpr const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec4 vColor;
out vec4 frag;
void main() { frag = vColor; })";

constexpr const char* kBgVertSrc = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUv;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
})";

constexpr const char* kBgFragSrc = R"(#version 300 es
precision mediump float;
in vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uUvScale;
out vec4 frag;
void main() {
    vec2 tuv = vec2(vUv.x, 1.0 - vUv.y);
    vec2 uv = (tuv - 0.5) * uUvScale + 0.5;
    vec3 c = texture(uTex, uv).rgb * 0.32;
    vec2 d = vUv - 0.5;
    c *= 1.0 - 0.9 * dot(d, d);
    c *= mix(0.35, 1.0, smoothstep(0.0, 0.38, vUv.y));
    c = mix(c, vec3(0.02, 0.025, 0.045), 0.25);
    frag = vec4(c, 1.0);
})";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        OH_LOG_ERROR(LOG_APP, "shader compile failed: %{public}s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

} // namespace

bool ArcRenderer::initEgl(void* nativeWindow) {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) return false;
    if (!eglInitialize(display_, nullptr, nullptr)) return false;

    EGLConfig config = nullptr;
    EGLint num = 0;
    const EGLint attr[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8,
                           EGL_GREEN_SIZE, 8,   EGL_BLUE_SIZE, 8,
                           EGL_ALPHA_SIZE, 8,   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                           EGL_NONE};
    if (!eglChooseConfig(display_, attr, &config, 1, &num) || num < 1) return false;

    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, ctxAttr);
    if (context_ == EGL_NO_CONTEXT) return false;

    surface_ = eglCreateWindowSurface(display_, config, (EGLNativeWindowType)nativeWindow, nullptr);
    if (surface_ == EGL_NO_SURFACE) return false;
    if (!eglMakeCurrent(display_, surface_, surface_, context_)) return false;
    eglSwapInterval(display_, 1); // vsync
    return true;
}

bool ArcRenderer::start(void* nativeWindow, int w, int h) {
    if (running_) return true;
    width_ = w;
    height_ = h;
    surfaceW_.store(w);
    surfaceH_.store(h);
    if (!initEgl(nativeWindow)) {
        OH_LOG_ERROR(LOG_APP, "EGL init failed");
        return false;
    }
    GLuint v = compile(GL_VERTEX_SHADER, kVertSrc);
    GLuint f = compile(GL_FRAGMENT_SHADER, kFragSrc);
    if (!v || !f) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, v);
    glAttachShader(program_, f);
    glLinkProgram(program_);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        OH_LOG_ERROR(LOG_APP, "program link failed");
        return false;
    }
    uMvp_ = glGetUniformLocation(program_, "uMvp");

    GLuint bv = compile(GL_VERTEX_SHADER, kBgVertSrc);
    GLuint bf = compile(GL_FRAGMENT_SHADER, kBgFragSrc);
    if (bv && bf) {
        bgProgram_ = glCreateProgram();
        glAttachShader(bgProgram_, bv);
        glAttachShader(bgProgram_, bf);
        glLinkProgram(bgProgram_);
        GLint bgOk = 0;
        glGetProgramiv(bgProgram_, GL_LINK_STATUS, &bgOk);
        if (!bgOk) {
            OH_LOG_ERROR(LOG_APP, "background program link failed");
            glDeleteProgram(bgProgram_);
            bgProgram_ = 0;
        }
        bgUvScaleLoc_ = glGetUniformLocation(bgProgram_, "uUvScale");
        bgTexLoc_ = glGetUniformLocation(bgProgram_, "uTex");
        glDeleteShader(bv);
        glDeleteShader(bf);
    }

    glClearColor(0.055f, 0.068f, 0.09f, 1.f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 着色器在 UI 线程编译完毕，释放上下文，让渲染线程独占它
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    surfaceReady_.store(true);
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    return true;
}

void ArcRenderer::stop() {
    surfaceReady_.store(false);
    running_ = false;
    if (thread_.joinable()) thread_.join();
    clear();
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (bgProgram_) {
        glDeleteProgram(bgProgram_);
        bgProgram_ = 0;
    }
    bgTex_ = 0;
    bgW_ = bgH_ = 0;
    {
        std::lock_guard<std::mutex> lk(bgMutex_);
        bgPending_.clear();
        bgPendingW_ = bgPendingH_ = 0;
        bgSerial_ = 0;
        bgUploadedSerial_ = 0;
    }
}

bool ArcRenderer::prepare(std::shared_ptr<const ParseResult> chart, const std::string& audioPath,
                          int gaugeKind, float chartConstant) {
    if (!chart) return false;
    {
        std::lock_guard<std::mutex> lk(chartMutex_);
        song_.unload();
        chart_ = std::move(chart);
        playback_ = std::make_unique<ChartPlayback>(chart_->chart);
        timing_ = std::make_unique<TimingModel>(chart_->chart.timings);
        audioOffset_ = chart_->chart.meta.audioOffset;
        endMs_ = playback_->allNotes().empty() ? 0 : playback_->allNotes().back().time;
        { std::lock_guard<std::mutex> sl(stateMutex_); error_.clear(); }
        {
            std::lock_guard<std::mutex> jl(judgeMutex_);
            aff::GaugeSetup gs;
            gs.kind = gaugeKind == 1   ? aff::GaugeKind::Easy
                      : gaugeKind == 2 ? aff::GaugeKind::Hard
                                       : aff::GaugeKind::Normal;
            // Lv = floor(定数)；Lv>=11 时 TN>=600 段 RF 乘 0.8
            gs.chartLevel = (int32_t)std::floor(chartConstant);
            judge_ = std::make_unique<NoteJudge>(*playback_, aff::JudgeWindows{}, gs);
        }
        curT_.store(audioOffset_);
        phase_.store(audioPath.empty() ? Error : Preparing);
        if (audioPath.empty()) { std::lock_guard<std::mutex> sl(stateMutex_); error_ = "音频未导入"; }
    }
    if (audioPath.empty()) return false;
    if (!song_.loadFile(audioPath)) {
        {
            std::lock_guard<std::mutex> lk(chartMutex_);
            { std::lock_guard<std::mutex> sl(stateMutex_); error_ = song_.error(); if (error_.empty()) error_ = "音频加载失败"; }
            phase_.store(Error);
        }
        return false;
    }
    return true;
}

void ArcRenderer::play() {
    int32_t ph = phase_.load();
    if (ph == Ready || ph == Paused) {
        if (song_.play()) phase_.store(Playing);
    } else if (ph == Finished) {
        restart();
    }
}

void ArcRenderer::pause() {
    if (phase_.load() != Playing) return;
    song_.pause();
    phase_.store(Paused);
}

void ArcRenderer::restart() {
    if (!judge_) return;
    {
        std::lock_guard<std::mutex> jl(judgeMutex_);
        if (judge_) judge_->reset();
    }
    curT_.store(audioOffset_);
    song_.seekTo(0);
    if (song_.play()) {
        phase_.store(Playing);
    } else {
        phase_.store(song_.prepared() ? Ready : Preparing);
    }
}

void ArcRenderer::clear() {
    {
        std::lock_guard<std::mutex> lk(chartMutex_);
        song_.unload();
        chart_.reset();
        playback_.reset();
        timing_.reset();
        {
            std::lock_guard<std::mutex> jl(judgeMutex_);
            judge_.reset();
        }
        curT_.store(-1);
        endMs_ = 0;
        { std::lock_guard<std::mutex> sl(stateMutex_); error_.clear(); }
        phase_.store(Idle);
    }
}

void ArcRenderer::finalizeJudgeLocked() {
    if (judge_) {
        std::lock_guard<std::mutex> jl(judgeMutex_);
        judge_->finalize();
    }
}

ArcRenderer::PlayState ArcRenderer::state() const {
    PlayState s;
    int32_t ph = phase_.load();
    if (ph == Preparing && song_.prepared()) ph = Ready;
    if (ph == Preparing && song_.failed()) {
        ph = Error;
    }
    s.phase = ph;
    s.audioReady = song_.prepared();
    s.rendererReady = surfaceReady_.load();
    s.surfaceW = surfaceW_.load();
    s.surfaceH = surfaceH_.load();
    s.duration = song_.durationMs();
    s.time = curT_.load();
    {
        std::lock_guard<std::mutex> sl(stateMutex_);
        s.error = error_;
    }
    if (s.error.empty() && ph == Error) {
        std::string e = song_.error();
        s.error = e.empty() ? "音频加载失败" : e;
    }
    {
        std::lock_guard<std::mutex> jl(judgeMutex_);
        if (judge_) {
            s.combo = judge_->combo();
            s.maxCombo = judge_->maxCombo();
            s.pure = judge_->pure();
            s.far = judge_->far();
            s.lost = judge_->lost();
            s.total = judge_->totalJudgeable();
            s.judgeSerial = judge_->judgeSerial();
            s.lastGrade = (int32_t)judge_->lastGrade();
            s.lastDelta = judge_->lastDelta();
            double acc = s.total > 0 ? (double)s.pure + 0.5 * (double)s.far : 0.0;
            s.score = s.total > 0 ? (int32_t)(10000000.0 * acc / (double)s.total + 0.5) : 0;
            s.gaugeF = judge_->gaugeF();
            s.gauge = judge_->gauge();
            s.trackLost = judge_->trackLost();
        }
    }
    return s;
}

void ArcRenderer::setNoteSpeed(float speed) {
    if (speed < 1.f) speed = 1.f;
    if (speed > 6.5f) speed = 6.5f;
    noteSpeedX10_.store((int32_t)std::lround(speed * 10.f));
}

void ArcRenderer::setBackground(const void* rgba, size_t bytes, int w, int h) {
    if (rgba == nullptr || w <= 0 || h <= 0) return;
    const size_t need = (size_t)w * (size_t)h * 4u;
    if (bytes < need) return;
    const uint8_t* src = static_cast<const uint8_t*>(rgba);
    std::lock_guard<std::mutex> lk(bgMutex_);
    bgPending_.assign(src, src + need);
    bgPendingW_ = w;
    bgPendingH_ = h;
    bgSerial_++;
}

void ArcRenderer::clearBackground() {
    std::lock_guard<std::mutex> lk(bgMutex_);
    bgPending_.clear();
    bgPendingW_ = bgPendingH_ = 0;
    bgSerial_++;
}

void ArcRenderer::uploadBackgroundIfNeeded() {
    std::lock_guard<std::mutex> lk(bgMutex_);
    if (bgSerial_ == bgUploadedSerial_) return;
    bgUploadedSerial_ = bgSerial_;
    if (bgPending_.empty()) {
        if (bgTex_ != 0) {
            glDeleteTextures(1, &bgTex_);
            bgTex_ = 0;
            bgW_ = bgH_ = 0;
        }
        return;
    }
    if (bgTex_ == 0) glGenTextures(1, &bgTex_);
    glBindTexture(GL_TEXTURE_2D, bgTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bgPendingW_, bgPendingH_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, bgPending_.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    bgW_ = bgPendingW_;
    bgH_ = bgPendingH_;
}

void ArcRenderer::drawBackground() {
    if (bgProgram_ == 0 || bgTex_ == 0 || width_ <= 0 || height_ <= 0 || bgW_ <= 0 ||
        bgH_ <= 0)
        return;
    const float screenAspect = (float)width_ / (float)height_;
    const float imageAspect = (float)bgW_ / (float)bgH_;
    float sx = 1.f, sy = 1.f;
    if (screenAspect > imageAspect) sy = imageAspect / screenAspect;
    else sx = screenAspect / imageAspect;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(bgProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bgTex_);
    glUniform1i(bgTexLoc_, 0);
    glUniform2f(bgUvScaleLoc_, sx, sy);

    const float quad[] = {-1.f, -1.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 1.f,
                          -1.f, -1.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f, -1.f, 1.f, 0.f, 1.f};
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
    const GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &vbo);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void ArcRenderer::setTouch(int32_t id, float x, float y) {
    std::lock_guard<std::mutex> lk(touchMutex_);
    for (auto& p : touches_) {
        if (p.id == id) {
            p.x = x;
            p.y = y;
            return;
        }
    }
    touches_.push_back(TouchPoint{id, x, y});
}

void ArcRenderer::removeTouch(int32_t id) {
    std::lock_guard<std::mutex> lk(touchMutex_);
    for (size_t i = 0; i < touches_.size(); i++) {
        if (touches_[i].id == id) {
            touches_.erase(touches_.begin() + (long)i);
            return;
        }
    }
}

std::vector<float> ArcRenderer::skyTouchXs() const {
    std::vector<float> out;
    std::lock_guard<std::mutex> lk(touchMutex_);
    for (const auto& p : touches_) {
        if (p.y <= 0.45f) out.push_back(arcXFromScreen(p.x));
    }
    return out;
}

void ArcRenderer::onTouchDown(int32_t id, float xNorm, float yNorm) {
    setTouch(id, xNorm, yNorm);
    if (phase_.load() != Playing) return;
    std::lock_guard<std::mutex> jl(judgeMutex_);
    if (!judge_) return;
    int32_t t = curT_.load();
    Grade g;
    if (yNorm > 0.45f) {
        g = judge_->tapLane(t, laneFromScreenX(xNorm));
    } else {
        g = judge_->tapSky(t, arcXFromScreen(xNorm));
    }
    if (g != Grade::None) {
        lastGrade_ = (uint8_t)g;
        lastJudgeFlashMs_ = t;
    }
}

void ArcRenderer::onTouchMove(int32_t id, float xNorm, float yNorm) {
    setTouch(id, xNorm, yNorm);
}

void ArcRenderer::onTouchUp(int32_t id, float /*xNorm*/, float /*yNorm*/) {
    removeTouch(id);
}

void ArcRenderer::loop() {
    if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
        OH_LOG_ERROR(LOG_APP, "render thread eglMakeCurrent failed");
        running_ = false;
        return;
    }
    while (running_) {
        eglQuerySurface(display_, surface_, EGL_WIDTH, &width_);
        eglQuerySurface(display_, surface_, EGL_HEIGHT, &height_);
        surfaceW_.store(width_);
        surfaceH_.store(height_);
        unitsPerMs_ = 0.00015f * (float)noteSpeedX10_.load();
        glViewport(0, 0, width_, height_);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        uploadBackgroundIfNeeded();
        drawBackground();

        {
            std::lock_guard<std::mutex> lk(chartMutex_);
            if (playback_) {
                int32_t ph = phase_.load();
                if (ph == Preparing && song_.prepared()) {
                    ph = Ready;
                    phase_.store(ph);
                    curT_.store(audioOffset_);
                }
                if (ph == Preparing && song_.failed()) {
                    ph = Error;
                    { std::lock_guard<std::mutex> sl(stateMutex_); if (error_.empty()) error_ = song_.error(); }
                    phase_.store(ph);
                }
                if (ph == Playing) {
                    int32_t pos = song_.positionMs();
                    int32_t t = audioOffset_ + pos + userOffsetMs_.load();
                    curT_.store(t);
                    bool failed = false;
                    {
                        std::lock_guard<std::mutex> jl(judgeMutex_);
                        if (judge_) {
                            judge_->update(t, skyTouchXs());
                            failed = judge_->trackLost();
                        }
                    }
                    if (failed) {
                        // Track Lost：立即停曲，不再把剩余音符补记 Lost
                        song_.pause();
                        phase_.store(Finished);
                    } else if (song_.completed() || t > endMs_ + 2000) {
                        finalizeJudgeLocked();
                        song_.pause();
                        phase_.store(Finished);
                    }
                    draw(t);
                } else if (ph == Finished) {
                    draw(curT_.load());
                } else if (ph == Paused) {
                    draw(curT_.load());
                } else {
                    // Preparing / Ready / Error：显示静止的谱面首帧
                    draw(curT_.load() >= 0 ? curT_.load() : audioOffset_);
                }
            }
        }
        eglSwapBuffers(display_, surface_);
    }
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    song_.pause();
}

void ArcRenderer::draw(int32_t tMs) {
    buildGeometry(tMs);
    if (verts_.empty()) return;
    Mat4 vp = mat4Mul(perspective(50.f,
                                  (float)width_ / (float)height_ * kFieldAspectBoost, 0.1f,
                                  100.f),
                      playfieldView());
    glUseProgram(program_);
    glUniformMatrix4fv(uMvp_, 1, GL_FALSE, vp.m);
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts_.size() * sizeof(Vertex), verts_.data(), GL_STREAM_DRAW);
    GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts_.size());
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &vbo);
}

void ArcRenderer::pushQuad(float x0, float y0, float z0, float x1, float y1, float z1, float x2,
                           float y2, float z2, float x3, float y3, float z3, float r, float g,
                           float b, float a) {
    verts_.push_back({x0, y0, z0, r, g, b, a});
    verts_.push_back({x1, y1, z1, r, g, b, a});
    verts_.push_back({x2, y2, z2, r, g, b, a});
    verts_.push_back({x0, y0, z0, r, g, b, a});
    verts_.push_back({x2, y2, z2, r, g, b, a});
    verts_.push_back({x3, y3, z3, r, g, b, a});
}

void ArcRenderer::pushLineOnFloor(float x0, float z0, float x1, float z1, float w, float r, float g,
                                  float b, float a) {
    // 同时给 z 方向厚度，否则横跨轨道的判定线/小节线会退化成零面积四边形而不可见。
    pushQuad(x0 - w, 0.015f, z0 + w, x0 + w, 0.015f, z0 + w, x1 + w, 0.015f, z1 - w, x1 - w,
             0.015f, z1 - w, r, g, b, a);
}

void ArcRenderer::pushRibbon(const std::vector<float>& xs, const std::vector<float>& ys,
                             const std::vector<int32_t>& ts, int32_t now, float halfW, float r,
                             float g, float b, float a) {
    if (xs.size() < 2) return;
    for (size_t i = 0; i + 1 < xs.size(); i++) {
        float wx0 = arcXToFloorX(xs[i]), wy0 = arcYToHeight(ys[i]);
        float wx1 = arcXToFloorX(xs[i + 1]), wy1 = arcYToHeight(ys[i + 1]);
        float z0 = timeToZ(ts[i], now, unitsPerMs_);
        float z1 = timeToZ(ts[i + 1], now, unitsPerMs_);
        if (z0 > 0.05f) continue; // 已过判定线段不画
        if (z1 < -60.f) continue; // 远裁剪外
        pushQuad(wx0 - halfW, wy0, z0, wx0 + halfW, wy0, z0, wx1 + halfW, wy1, z1, wx1 - halfW,
                 wy1, z1, r, g, b, a);
    }
}

void ArcRenderer::buildGeometry(int32_t tNow) {
    verts_.clear();
    constexpr float kFloorZ = -60.f; // 画面最远

    // 4 条地面轨（暗色分隔 + 微亮轨面）
    for (int lane = 0; lane < 4; lane++) {
        float cx = laneCenterX(lane);
        pushQuad(cx - 0.48f, 0.f, 0.f, cx + 0.48f, 0.f, 0.f, cx + 0.48f, 0.f, kFloorZ,
                 cx - 0.48f, 0.f, kFloorZ, 0.04f, 0.05f, 0.07f, 0.78f);
    }
    // 轨道边线
    for (int i = 0; i <= 4; i++) {
        float x = -2.f + i;
        pushLineOnFloor(x, 0.f, x, kFloorZ, 0.02f, 0.25f, 0.28f, 0.34f, 0.8f);
    }
    // 判定线（命中后 120ms 内闪光：Pure=亮白 Far=金黄 Lost=红）
    {
        float jr = 0.95f, jg = 0.96f, jb = 1.f, ja = 0.95f;
        int64_t sinceHit = (int64_t)tNow - lastJudgeFlashMs_;
        if (sinceHit >= 0 && sinceHit < 120) {
            float k = 1.f - (float)sinceHit / 120.f;
            if (lastGrade_ == (uint8_t)Grade::Pure) { jg = 1.f; jb = 1.f; }
            else if (lastGrade_ == (uint8_t)Grade::Far) { jr = 1.f; jg = 0.85f; jb = 0.3f; }
            else { jr = 1.f; jg = 0.3f; jb = 0.3f; }
            ja = 0.95f + 0.05f * k;
        }
        pushLineOnFloor(-2.2f, 0.f, 2.2f, 0.f, 0.05f, jr, jg, jb, ja);
    }

    // 小节线
    if (timing_) {
        auto bars = timing_->barLineTimes(tNow - 300, tNow - (int32_t)(kFloorZ / unitsPerMs_));
        for (int32_t bt : bars) {
            float z = timeToZ(bt, tNow, unitsPerMs_);
            if (z <= 0.05f && z >= kFloorZ)
                pushLineOnFloor(-2.f, z, 2.f, z, 0.012f, 0.4f, 0.44f, 0.55f, 0.5f);
        }
    }

    // 可见 note（已判定的不画：指针 → 索引查判定状态）
    std::vector<const RenderNote*> vis;
    playback_->collectVisible(tNow, (int32_t)(-kFloorZ / unitsPerMs_), 200, vis);
    const Chart& ch = playback_->chart();
    const std::vector<RenderNote>& all = playback_->allNotes();
    const RenderNote* base = all.data();
    auto noteColor = 0.92f;
    for (const RenderNote* nPtr : vis) {
        size_t idx = (size_t)(nPtr - base);
        bool judged = false;
        bool holding = false;
        {
            std::lock_guard<std::mutex> jl(judgeMutex_);
            judged = judge_ && judge_->isJudged(idx);
            holding = judge_ && judge_->isHolding(idx);
        }
        // hold/弧命中后仍保留剩余轨迹，避免长条/弧在头部判定时瞬间消失
        if (judged && nPtr->kind != RenderNote::Kind::HoldStart &&
            nPtr->kind != RenderNote::Kind::ArcHead)
            continue;
        const RenderNote* n = nPtr;
        switch (n->kind) {
            case RenderNote::Kind::Tap: {
                float cx = laneCenterX(n->lane);
                float z = timeToZ(n->time, tNow, unitsPerMs_);
                if (z > 0.1f || z < kFloorZ) break;
                pushQuad(cx - 0.26f, 0.03f, z - 0.13f, cx + 0.26f, 0.03f, z - 0.13f, cx + 0.26f,
                         0.03f, z + 0.13f, cx - 0.26f, 0.03f, z + 0.13f, noteColor, noteColor, 1.f,
                         1.f);
                break;
            }
            case RenderNote::Kind::HoldStart: {
                if (n->group) { // 组内 hold 用组 flag（fadingholds 以后做透明处理）
                }
                float cx = laneCenterX(n->lane);
                float z0 = timeToZ(n->time, tNow, unitsPerMs_);
                float z1 = timeToZ(n->endTime, tNow, unitsPerMs_);
                if (z1 > 0.1f) z1 = 0.1f;
                if (z0 < kFloorZ) z0 = kFloorZ;
                if (z0 <= z1) break;
                pushQuad(cx - 0.2f, 0.025f, z0, cx + 0.2f, 0.025f, z0, cx + 0.2f, 0.025f, z1,
                         cx - 0.2f, 0.025f, z1, 0.75f, 0.82f, 1.f,
                         judged ? 0.32f : 0.85f);
                break;
            }
            case RenderNote::Kind::ArcHead: {
                const Arc& a = *n->arc;
                float alpha = a.isTrace ? 0.22f : (holding ? 0.95f : (judged ? 0.26f : 0.95f));
                float cr = a.color == 0 ? 0.36f : 1.0f;
                float cg = a.color == 0 ? 0.56f : 0.35f;
                float cb = a.color == 0 ? 1.0f : 0.45f;
                std::vector<float> xs, ys;
                std::vector<int32_t> ts;
                playback_->arcVisiblePolyline(a, tNow, 24, xs, ys, ts);
                pushRibbon(xs, ys, ts, tNow, 0.13f, cr, cg, cb, alpha);
                break;
            }
            case RenderNote::Kind::ArcTapNote: {
                const Arc& a = *n->arc;
                float px, py;
                playback_->arcPosAt(a, n->time, px, py);
                float wx = arcXToFloorX(px), wy = arcYToHeight(py);
                float z = timeToZ(n->time, tNow, unitsPerMs_);
                if (z > 0.1f || z < kFloorZ) break;
                float cr = a.color == 0 ? 0.55f : 1.0f;
                float cg = a.color == 0 ? 0.72f : 0.5f;
                float cb = a.color == 0 ? 1.0f : 0.58f;
                pushQuad(wx - 0.16f, wy + 0.16f, z, wx + 0.16f, wy + 0.16f, z, wx, wy - 0.2f, z,
                         wx, wy - 0.2f, z, cr, cg, cb, 1.f); // 菱形（退化三角形）
                verts_.push_back({wx - 0.16f, wy + 0.16f, z, cr, cg, cb, 1.f});
                verts_.push_back({wx, wy - 0.2f, z, cr, cg, cb, 1.f});
                verts_.push_back({wx + 0.16f, wy + 0.16f, z, cr, cg, cb, 1.f});
                break;
            }
        }
    }
    (void)ch;
}

} // namespace arc
