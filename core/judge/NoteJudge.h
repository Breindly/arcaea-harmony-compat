// NoteJudge.h —— 判定核心（可移植，桌面可测）
// 规则：
//   地面：tap/hold 头部按轨道离散判定（hold 不做尾判/松手判定）
//   天空：ArcTap 离散判定；ArcHead 改由 update() 做连续按住判定（弧 x 跟随）
//   窗口：Pure±25ms / Far±50ms；超过 Far 不可击，100ms 后自动 Lost
//   血条：Recollection Rate 规则——Normal/Easy 从 0 起，Pure +RF、Far +RF/2、
//         Lost 固定扣（Normal -2 / Easy -1.2），终曲 RR>=70 才 Track Complete；
//         Normal/Easy 归零不中途死亡。Hard 从 100 起，Lost 按 RR 分段
//         (-9 / -(0.4RR-7) / -5)，归零立即 Track Lost。
//         RF（Recollection Factor）按可判定 note 数 TN 分段：
//           TN<400: 0.2+80/TN；400..599: 0.2+32/TN；
//           TN>=600: 0.08+96/TN（Lv>=11 再乘 0.8）。
//         （公开玩法资料：Recollection Rate，70% 清线，Hard 30% 红线）
#pragma once

#include "../playback/ChartPlayback.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace aff {

struct JudgeWindows {
    int32_t pureMs = 25;
    int32_t farMs = 50;
    int32_t lostMs = 100; // 自动 Lost 延迟
};

enum class Grade : uint8_t { None = 0, Pure, Far, Lost };

// 血条类型：0=Normal(默认) 1=Easy 2=Hard
enum class GaugeKind : uint8_t { Normal = 0, Easy = 1, Hard = 2 };

struct GaugeSetup {
    GaugeKind kind = GaugeKind::Normal;
    int32_t chartLevel = 0; // floor(定数)；仅 TN>=600 的 RF 分段使用（Lv>=11 ×0.8）
};

class NoteJudge {
public:
    explicit NoteJudge(const ChartPlayback& pb, JudgeWindows w = {}, GaugeSetup g = {})
        : pb_(pb), w_(w), gauge_(g) {
        status_.assign(pb.allNotes().size(), 0);
        resetGaugeState();
    }

    // 地面轨道击打：Tap 立即结算；HoldStart 也只判头。
    Grade tapLane(int32_t t, int lane) {
        return hitBest(t, [lane](const RenderNote& n) {
            return (n.kind == RenderNote::Kind::Tap || n.kind == RenderNote::Kind::HoldStart) &&
                   n.lane == lane && !isNoInput(n);
        });
    }

    // 天空点按：只处理 ArcTap（弧头由 update() 连续判定）。
    Grade tapSky(int32_t t, float touchArcX) {
        const float touchX = touchArcX;
        const auto& notes = pb_.allNotes();
        size_t lo = lowerBound(t - w_.farMs);
        size_t bestIdx = SIZE_MAX;
        int64_t bestDt = INT64_MAX;
        int64_t bestSigned = 0;
        float bestDx = 1e9f;
        for (size_t i = lo; i < notes.size() && notes[i].time <= t + w_.farMs; i++) {
            if (status_[i] != 0) continue;
            const RenderNote& n = notes[i];
            if (n.kind != RenderNote::Kind::ArcTapNote || n.arc == nullptr || isNoInput(n)) continue;
            float nx = 0.f, ny = 0.f;
            pb_.arcPosAt(*n.arc, n.time, nx, ny);
            (void)ny;
            float dx = nx - touchX;
            if (dx < 0.f) dx = -dx;
            if (dx > 0.45f) continue;
            int64_t signedDt = (int64_t)n.time - t;
            int64_t dt = signedDt < 0 ? -signedDt : signedDt;
            if (dt < bestDt || (dt == bestDt && dx < bestDx)) {
                bestDt = dt;
                bestSigned = signedDt;
                bestDx = dx;
                bestIdx = i;
            }
        }
        if (bestIdx == SIZE_MAX) return Grade::None;
        return applyHit(bestIdx, (int32_t)bestDt, (int32_t)bestSigned);
    }

    // 曲终：所有仍未判定的 note 记 Lost；Normal/Easy 在此结算终曲清线。
    void finalize() {
        for (const auto& a : arcs_) {
            lifeLost(a.idx);
        }
        arcs_.clear();
        const auto& notes = pb_.allNotes();
        for (size_t i = 0; i < notes.size(); i++) {
            if (status_[i] != 0) continue;
            if (!judgeable(notes[i])) continue;
            lifeLost(i);
        }
        cursor_ = notes.size();
        // Normal/Easy：没有中途死亡，终曲 RR>=70 才算 Track Complete。
        if (gauge_.kind != GaugeKind::Hard) {
            trackLost_ = gaugeVal_ < 70.f;
        }
    }

    // 每帧推进：弧连续判定 + 超时 Lost。
    // skyXs：当前按在天空区的触点在弧坐标系里的 x 列表。
    int32_t update(int32_t t, const std::vector<float>& skyXs = {}) {
        const auto& notes = pb_.allNotes();
        int32_t missed = 0;

        // 1) 已在按住的弧：跟随 x，脱离超过 100ms 判 Lost，到时结算。
        for (size_t i = 0; i < arcs_.size();) {
            ArcState& a = arcs_[i];
            const RenderNote& n = notes[a.idx];
            float ax = 0.f, ay = 0.f;
            pb_.arcPosAt(*n.arc, t, ax, ay);
            (void)ay;
            if (nearAny(skyXs, ax)) {
                a.lastHeldMs = t;
                a.held = true;
            }
            if (t >= a.endTime) {
                if (a.held && t - a.lastHeldMs <= 100) finishArc(a);
                else {
                    loseArc(a);
                    missed++;
                }
                arcs_.erase(arcs_.begin() + (long)i);
                continue;
            }
            if (t - a.lastHeldMs > 100) {
                loseArc(a);
                arcs_.erase(arcs_.begin() + (long)i);
                missed++;
                continue;
            }
            i++;
        }

        // 2) 到达弧头窗口且天空触点在弧头 x 附近：开始按住。
        size_t lo = lowerBound(t - w_.lostMs);
        for (size_t i = lo; i < notes.size() && notes[i].time <= t + 50; i++) {
            if (status_[i] != 0) continue;
            const RenderNote& n = notes[i];
            if (n.kind != RenderNote::Kind::ArcHead || n.arc == nullptr || n.arc->isTrace ||
                isNoInput(n))
                continue;
            if (t < n.time - 50 || t > n.time + w_.lostMs) continue;
            float ax = 0.f, ay = 0.f;
            pb_.arcPosAt(*n.arc, n.time, ax, ay);
            (void)ay;
            if (nearAny(skyXs, ax)) {
                status_[i] = 3; // arc holding
                ArcState a;
                a.idx = i;
                a.endTime = n.endTime;
                a.lastHeldMs = t;
                a.held = true;
                arcs_.push_back(a);
            }
        }

        // 3) 超时自动 Lost（正在按住的弧 status=3 会被跳过）。
        while (cursor_ < notes.size() && notes[cursor_].time < t - w_.lostMs) {
            bool canJudge = !isNoInput(notes[cursor_]);
            if (canJudge && notes[cursor_].kind == RenderNote::Kind::ArcHead) {
                canJudge = notes[cursor_].arc && !notes[cursor_].arc->isTrace;
            }
            if (canJudge && status_[cursor_] == 0) {
                lifeLost(cursor_);
                missed++;
            }
            cursor_++;
        }
        return missed;
    }

    bool isJudged(size_t noteIdx) const { return noteIdx < status_.size() && status_[noteIdx] != 0; }

    bool isHolding(size_t noteIdx) const {
        for (const auto& a : arcs_) {
            if (a.idx == noteIdx) return true;
        }
        return false;
    }

    uint32_t combo() const { return combo_; }
    uint32_t maxCombo() const { return maxCombo_; }
    uint32_t pure() const { return pure_; }
    uint32_t far() const { return far_; }
    uint32_t lost() const { return lost_; }
    float gaugeF() const { return gaugeVal_; }
    int32_t gauge() const { return (int32_t)std::lround(gaugeVal_); }
    bool trackLost() const { return trackLost_; }
    GaugeKind gaugeKind() const { return gauge_.kind; }
    float recollectionFactor() const { return rf_; }

    uint32_t judgeSerial() const { return judgeSerial_; }
    uint8_t lastGrade() const { return lastGrade_; }
    int32_t lastDelta() const { return lastDelta_; }

    uint32_t totalJudgeable() const {
        uint32_t n = 0;
        for (auto& x : pb_.allNotes()) {
            if (!judgeable(x)) continue;
            n++;
        }
        return n;
    }

    void reset() {
        status_.assign(pb_.allNotes().size(), 0);
        arcs_.clear();
        cursor_ = 0;
        pure_ = far_ = lost_ = combo_ = maxCombo_ = 0;
        resetGaugeState();
        judgeSerial_ = 0;
        lastGrade_ = 0;
        lastDelta_ = 0;
    }

private:
    struct ArcState {
        size_t idx = SIZE_MAX;
        int32_t endTime = 0;
        int32_t lastHeldMs = 0;
        bool held = false;
    };

    static bool isNoInput(const RenderNote& n) {
        return n.group != nullptr && n.group->flags.noInput;
    }

    bool judgeable(const RenderNote& n) const {
        if (isNoInput(n)) return false;
        if (n.kind == RenderNote::Kind::ArcTapNote || n.kind == RenderNote::Kind::Tap ||
            n.kind == RenderNote::Kind::HoldStart)
            return true;
        if (n.kind == RenderNote::Kind::ArcHead) return n.arc != nullptr && !n.arc->isTrace;
        return false;
    }

    static bool nearAny(const std::vector<float>& skyXs, float arcX) {
        for (float x : skyXs) {
            if (std::fabs(x - arcX) <= 0.32f) return true;
        }
        return false;
    }

    // ――― Recollection Rate 血条 ―――
    // RF 只依赖可判定 note 数与谱面等级；judge 构造后不变。
    void computeRecollectionFactor() {
        uint32_t tn = totalJudgeable();
        if (tn <= 0) {
            rf_ = 0.0f;
            return;
        }
        float n = (float)tn;
        if (tn < 400) {
            rf_ = 0.2f + 80.0f / n;
        } else if (tn < 600) {
            rf_ = 0.2f + 32.0f / n;
        } else {
            rf_ = 0.08f + 96.0f / n;
            if (gauge_.chartLevel >= 11) rf_ *= 0.8f;
        }
    }

    void resetGaugeState() {
        computeRecollectionFactor();
        gaugeVal_ = gauge_.kind == GaugeKind::Hard ? 100.f : 0.f;
        trackLost_ = false;
    }

    void clampGauge() {
        if (gaugeVal_ > 100.f) gaugeVal_ = 100.f;
        if (gaugeVal_ < 0.f) gaugeVal_ = 0.f;
    }

    // LOST 扣血：Normal -2 / Easy -1.2 / Hard 按 RR 分段。
    void lostPenalty() {
        if (gauge_.kind == GaugeKind::Hard) {
            if (gaugeVal_ >= 40.f) gaugeVal_ -= 9.f;
            else if (gaugeVal_ > 30.f) gaugeVal_ -= (0.4f * gaugeVal_ - 7.f);
            else gaugeVal_ -= 5.f;
        } else if (gauge_.kind == GaugeKind::Easy) {
            gaugeVal_ -= 1.2f;
        } else {
            gaugeVal_ -= 2.f;
        }
        clampGauge();
        // 只有 Hard 归零会立即 Track Lost；Normal/Easy 归零继续，终曲结算。
        if (gauge_.kind == GaugeKind::Hard && gaugeVal_ <= 0.f) {
            trackLost_ = true;
        }
    }

    void gaugePure() {
        gaugeVal_ += rf_;
        clampGauge();
    }

    void gaugeFar() {
        gaugeVal_ += rf_ * 0.5f;
        clampGauge();
    }

    void recordEvent(uint8_t grade, int32_t deltaMs) {
        lastGrade_ = grade;
        lastDelta_ = deltaMs;
        judgeSerial_++;
    }

    void lifeLost(size_t idx) {
        status_[idx] = 2;
        lost_++;
        combo_ = 0;
        lostPenalty();
        recordEvent(3, 0);
    }

    void loseArc(const ArcState& a) { lifeLost(a.idx); }

    void finishArc(const ArcState& a) {
        status_[a.idx] = 1;
        pure_++;
        combo_++;
        if (combo_ > maxCombo_) maxCombo_ = combo_;
        gaugePure();
        recordEvent(1, 0);
    }

    Grade applyHit(size_t idx, int32_t absDelta, int32_t signedDelta) {
        Grade g = gradeOf(absDelta);
        if (g == Grade::Lost) {
            lifeLost(idx);
            return g;
        }
        status_[idx] = 1;
        if (g == Grade::Pure) {
            pure_++;
            gaugePure();
        } else {
            far_++;
            gaugeFar();
        }
        combo_++;
        if (combo_ > maxCombo_) maxCombo_ = combo_;
        recordEvent((uint8_t)g, signedDelta);
        return g;
    }

    template <typename Pred>
    Grade hitBest(int32_t t, Pred pred) {
        const auto& notes = pb_.allNotes();
        int64_t best = INT64_MAX;
        int64_t bestSigned = 0;
        size_t bestIdx = SIZE_MAX;
        size_t lo = lowerBound(t - w_.farMs);
        for (size_t i = lo; i < notes.size() && notes[i].time <= t + w_.farMs; i++) {
            if (status_[i] != 0 || !pred(notes[i])) continue;
            int64_t d = (int64_t)notes[i].time - t;
            int64_t ad = d < 0 ? -d : d;
            if (ad < best) {
                best = ad;
                bestSigned = d;
                bestIdx = i;
            }
        }
        if (bestIdx == SIZE_MAX) return Grade::None;
        return applyHit(bestIdx, (int32_t)best, (int32_t)bestSigned);
    }

    Grade gradeOf(int32_t absDelta) const {
        if (absDelta <= w_.pureMs) return Grade::Pure;
        if (absDelta <= w_.farMs) return Grade::Far;
        return Grade::Lost;
    }

    size_t lowerBound(int32_t t) const {
        const auto& notes = pb_.allNotes();
        size_t lo = 0, hi = notes.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (notes[mid].time < t) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    const ChartPlayback& pb_;
    JudgeWindows w_;
    GaugeSetup gauge_;
    float rf_ = 0.f;      // Recollection Factor（每 PURE 增量）
    float gaugeVal_ = 0.f; // Recollection Rate 0..100
    std::vector<uint8_t> status_; // 0=待判 1=命中 2=Lost 3=弧按住中
    std::vector<ArcState> arcs_;
    size_t cursor_ = 0;
    uint32_t pure_ = 0, far_ = 0, lost_ = 0, combo_ = 0, maxCombo_ = 0;
    bool trackLost_ = false;
    uint32_t judgeSerial_ = 0;
    uint8_t lastGrade_ = 0;
    int32_t lastDelta_ = 0;
};

} // namespace aff
