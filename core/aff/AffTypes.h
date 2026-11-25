// AffTypes.h -- portable AFF chart data model.
// Time is expressed in milliseconds. Ground lanes use [0,3]; sky arcs use
// x in [-0.5,1.5] and y in [0,1].
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aff {

// ――― 事件模型（全部按时间排序存放于 Chart） ―――

struct Tap {              // (t, lane);
    int32_t time = 0;
    int32_t lane = 0;     // 0..3
};

struct Hold {             // hold(t0, t1, lane);
    int32_t time = 0;
    int32_t endTime = 0;
    int32_t lane = 0;     // 0..3
};

struct ArcTap {           // arc 的 [arctap(t),...] 后缀
    int32_t time = 0;
};

struct Arc {              // arc(t0, t1, x0, x1, easing, y0, y1, color, skyline, isTrace)[arctap...];
    int32_t time = 0;
    int32_t endTime = 0;
    float xFrom = 0.f;    // 轨道横坐标，-0.5 ~ 1.5
    float xTo = 0.f;
    std::string easing;   // s|b|si|so 及 2~4 段组合（sisi/soso/siso/sosi）
    float yFrom = 0.f;    // 高度 0 ~ 1
    float yTo = 0.f;
    int32_t color = 0;    // 0=蓝 1=红
    std::string skyline;  // none|clk_(...) 等，音击特效
    bool isTrace = false; // true = 影弧（无判定）
    std::vector<ArcTap> arcTaps;
};

struct TimingEvent {      // timing(t, bpm, beatsPerLine);
    int32_t time = 0;
    double bpm = 120.0;
    double beatsPerLine = 4.0;
};

struct CameraEvent {      // camera(t, x, y, z, rx, ry, rz, easing, durationMs);
    int32_t time = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    float rx = 0.f, ry = 0.f, rz = 0.f;
    std::string easing;   // l|q|reset|...
    int32_t duration = 0;
};

struct SceneControlEvent { // scenecontrol(t, type, fParam, iParam);
    int32_t time = 0;
    std::string type;     // hidegroup|trackshow|enwidencamera|...
    float fparam = 0.f;
    int32_t iparam = 0;
};

// ――― TimingGroup：包住一组 note 附加渲染/判定属性 ―――

struct TimingGroupFlags {
    bool noInput = false;      // noinput —— 该组不参与判定
    bool fadingHolds = false;  // fadingholds —— 长条渐隐
    bool noClap = false;
    bool noShadow = false;
    bool noInputEase = false;  // 少见，语料未出现，保留位
    float angleX = 0.f;        // anglex<deg*10> —— 视角旋转（原值/10 度）
    float angleY = 0.f;        // angley<deg*10>
};

struct TimingGroup {
    TimingGroupFlags flags;
    std::vector<TimingEvent> timings; // 组内独立变速（影响该组滚动速度），不进全局时轴
    std::vector<Tap> taps;
    std::vector<Hold> holds;
    std::vector<Arc> arcs;
};

// ――― 整张谱面 ―――

struct ChartMeta {
    int32_t audioOffset = 0;            // AudioOffset: 毫秒
    double timingPointDensityFactor = 1.0; // TimingPointDensityFactor（可选）
};

struct Chart {
    ChartMeta meta;
    std::vector<TimingEvent> timings;
    std::vector<Tap> taps;              // 顶层（组外）
    std::vector<Hold> holds;
    std::vector<Arc> arcs;
    std::vector<CameraEvent> cameras;
    std::vector<SceneControlEvent> sceneControls;
    std::vector<TimingGroup> groups;

    // 展平后的判定用 note 计数（含组内；影弧除外）
    size_t totalPlayableNotes() const;
};

// ――― 解析结果 ―――

struct ParseIssue {
    int line = 0;
    std::string message;
};

struct ParseResult {
    Chart chart;
    bool ok = true;
    std::vector<ParseIssue> issues; // 非致命问题（容错继续解析）
};

} // namespace aff
