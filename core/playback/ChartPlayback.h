// ChartPlayback.h —— 播放时间轴：给定当前时刻 t(ms)，给出渲染所需的全部状态
// 设计：一次构建（索引化+按时间排序），每帧 O(log n + k) 查询（k=可见 note 数）。
// 时间均为毫秒；判定线在底部，note 自上而下接近。
#pragma once

#include "../aff/AffTypes.h"
#include "Easing.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace aff {

// 统一渲染事件（拍点/长条/弧/天键打点均折算成"判定时刻"参与排序）
struct RenderNote {
    enum class Kind : uint8_t { Tap, HoldStart, ArcHead, ArcTapNote };
    Kind kind = Kind::Tap;
    int32_t time = 0;      // 判定时刻
    int32_t endTime = 0;   // 仅 Hold/Arc 有效
    int32_t lane = -1;     // Tap/Hold: 0..3
    const Arc* arc = nullptr;
    const TimingGroup* group = nullptr;
};

class ChartPlayback {
public:
    explicit ChartPlayback(const Chart& c) : chart_(c) { build(); }

    // t 时刻已进入视野（time <= t + lookahead）且未过判定窗（time >= t - tailMs）的 note
    void collectVisible(int32_t t, int32_t lookaheadMs, int32_t tailMs,
                        std::vector<const RenderNote*>& out) const {
        out.clear();
        const int32_t lo = t - tailMs, hi = t + lookaheadMs;
        auto it = std::lower_bound(notes_.begin(), notes_.end(), lo,
                                   [](const RenderNote& n, int32_t v) { return n.time < v; });
        for (; it != notes_.end() && it->time <= hi; ++it) {
            if (it->endTime >= lo) out.push_back(&*it); // 长条/弧：头部已过但尾部仍在场
        }
    }

    // 弧在 t' 时刻的中心点（x∈[-0.5,1.5], y∈[0,1]）。t' 钳制到弧时长内。
    void arcPosAt(const Arc& a, int32_t tMs, float& xOut, float& yOut) const {
        Ease xe, ye;
        if (!parseArcEasing(a.easing, xe, ye)) { xe = Ease::Linear; ye = Ease::Linear; }
        float dur = (float)(a.endTime - a.time);
        float u = dur > 0.f ? ((float)(tMs - a.time) / dur) : 0.f;
        if (u < 0.f) u = 0.f;
        if (u > 1.f) u = 1.f;
        xOut = easeValue(xe, a.xFrom, a.xTo, u);
        yOut = easeValue(ye, a.yFrom, a.yTo, u);
    }

    // 弧可见段的采样折线（从 max(time,t) 到 endTime），供渲染画剩余轨迹
    void arcVisiblePolyline(const Arc& a, int32_t t, int maxPoints,
                            std::vector<float>& xs, std::vector<float>& ys,
                            std::vector<int32_t>& ts) const {
        xs.clear(); ys.clear(); ts.clear();
        int32_t start = std::max(a.time, t);
        if (a.endTime <= start) return;
        int n = std::max(2, maxPoints);
        for (int i = 0; i < n; i++) {
            int32_t tt = start + (int32_t)((float)(a.endTime - start) * (float)i / (float)(n - 1));
            float x, y;
            arcPosAt(a, tt, x, y);
            xs.push_back(x); ys.push_back(y); ts.push_back(tt);
        }
    }

    // hold 在 t 时刻是否处于按住状态
    static bool holdActiveAt(const Hold& h, int32_t t) { return t >= h.time && t <= h.endTime; }

    const std::vector<RenderNote>& allNotes() const { return notes_; }
    const Chart& chart() const { return chart_; }

private:
    void build() {
        auto addChart = [&](const std::vector<Tap>& taps, const std::vector<Hold>& holds,
                            const std::vector<Arc>& arcs, const TimingGroup* g) {
            for (const auto& t : taps)
                notes_.push_back(RenderNote{RenderNote::Kind::Tap, t.time, t.time, t.lane, nullptr, g});
            for (const auto& h : holds)
                notes_.push_back(RenderNote{RenderNote::Kind::HoldStart, h.time, h.endTime, h.lane, nullptr, g});
            for (const auto& a : arcs) {
                notes_.push_back(RenderNote{RenderNote::Kind::ArcHead, a.time, a.endTime, -1, &a, g});
                for (const auto& at : a.arcTaps)
                    notes_.push_back(RenderNote{RenderNote::Kind::ArcTapNote, at.time, at.time, -1, &a, g});
            }
        };
        addChart(chart_.taps, chart_.holds, chart_.arcs, nullptr);
        for (const auto& g : chart_.groups)
            addChart(g.taps, g.holds, g.arcs, &g);
        std::sort(notes_.begin(), notes_.end(),
                  [](const RenderNote& a, const RenderNote& b) { return a.time < b.time; });
    }

    const Chart& chart_;
    std::vector<RenderNote> notes_;
};

} // namespace aff
