// TimingModel.h —— .aff 时间轴模型：拍↔毫秒互转、小节线生成
// .aff 全部时间值为毫秒；timing(t,bpm,bpl) 定义自 t 起生效的 BPM。
// 渲染/判定均用毫秒，拍换算仅用于小节线与节奏吸附显示。
#pragma once

#include "AffTypes.h"

#include <algorithm>
#include <cmath>

namespace aff {

class TimingModel {
public:
    explicit TimingModel(const std::vector<TimingEvent>& evs) {
        events_ = evs;
        std::sort(events_.begin(), events_.end(),
                  [](const TimingEvent& a, const TimingEvent& b) { return a.time < b.time; });
    }

    // 生效中的 timing 段（t 之前最后一段；t 早于首段时用首段）
    const TimingEvent* segmentAt(int32_t tMs) const {
        if (events_.empty()) return nullptr;
        if (tMs < events_.front().time) return &events_.front();
        const TimingEvent* best = &events_.front();
        for (const auto& e : events_) {
            if (e.time <= tMs) best = &e;
            else break;
        }
        return best;
    }

    // 毫秒 → 该段内拍数（相对段起点）
    double beatAt(int32_t tMs) const {
        const TimingEvent* seg = segmentAt(tMs);
        if (!seg || seg->bpm <= 0.0) return 0.0;
        double beatLenMs = 60000.0 / seg->bpm;
        return (tMs - seg->time) / beatLenMs;
    }

    // 拍（相对段起点）→ 毫秒
    int32_t msAtBeat(int32_t segTimeMs, double bpm, double beat) const {
        if (bpm <= 0.0) return segTimeMs;
        return segTimeMs + (int32_t)std::lround(beat * 60000.0 / bpm);
    }

    // 生成小节线时间（按各段 beatsPerLine，对齐节拍网格），供谱面渲染画竖线
    std::vector<int32_t> barLineTimes(int32_t fromMs, int32_t toMs) const {
        std::vector<int32_t> out;
        if (events_.empty()) return out;
        for (size_t i = 0; i < events_.size(); i++) {
            const TimingEvent& seg = events_[i];
            if (seg.bpm <= 0.0) continue;
            int32_t segEnd = (i + 1 < events_.size()) ? events_[i + 1].time : toMs;
            if (segEnd > toMs) segEnd = toMs;
            double barMs = (60000.0 / seg.bpm) * seg.beatsPerLine;
            if (barMs < 1.0) continue; // 防 BPM 异常死循环
            // 网格锚点是段起点：即使 fromMs 落在段中间，小节线仍对齐 seg.time + k*barMs
            int64_t k = 0;
            if (fromMs > seg.time) {
                k = (int64_t)std::ceil((double)(fromMs - seg.time) / barMs);
            }
            for (;; k++) {
                int32_t t = seg.time + (int32_t)std::llround((double)k * barMs);
                if (t > segEnd) break;
                out.push_back(t);
            }
        }
        return out;
    }

private:
    std::vector<TimingEvent> events_;
};

} // namespace aff
