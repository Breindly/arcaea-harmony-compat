// Easing.h —— 弧线/镜头缓动（语义依据 AffTools 参考实现 ArcAlgorithm.cs，
// 与游戏逆向一致；camera 的 qi/qo 同源 Qi/Qo）
//
// 关键语义：两段式缓动（sisi/soso/siso/sosi）中，X 坐标用第一段函数、
// Y 坐标用第二段函数，各自独立对整个时长插值（不是按时间分段）。
#pragma once

#include <cmath>
#include <string>

namespace aff {

enum class Ease : uint8_t { Linear, Bezier, SineIn, SineOut };

inline float easeValue(Ease e, float start, float end, float t) {
    switch (e) {
        case Ease::SineIn:
            return start + (end - start) * std::sin(1.5707963f * t);
        case Ease::SineOut:
            return start + (end - start) * (1.f - std::cos(1.5707963f * t));
        case Ease::Bezier: {
            float o = 1.f - t;
            return o * o * o * start + 3.f * o * o * t * start +
                   3.f * o * t * t * end + t * t * t * end;
        }
        case Ease::Linear:
        default:
            return (1.f - t) * start + end * t;
    }
}

// 解析弧缓动串 → (x 轴函数, y 轴函数)。返回 false 表示无法识别（调用方按 Linear 容错）。
// 合法形如：s | b | si | so | sisi | siso | sosi | soso（两段组合取第一/第二段）
inline bool parseArcEasing(const std::string& s, Ease& xOut, Ease& yOut) {
    auto kindOf = [](const std::string& seg) -> int {
        if (seg == "si") return (int)Ease::SineIn;
        if (seg == "so") return (int)Ease::SineOut;
        if (seg == "b") return (int)Ease::Bezier;
        return -1;
    };
    if (s == "s") { xOut = Ease::Linear; yOut = Ease::Linear; return true; }
    if (s == "b") { xOut = Ease::Bezier; yOut = Ease::Bezier; return true; }
    if (s.size() == 2) {
        int k = kindOf(s);
        if (k < 0) return false;
        xOut = yOut = (Ease)k;
        return true;
    }
    if (s.size() == 4) {
        std::string a = s.substr(0, 2), b = s.substr(2, 2);
        int ka = kindOf(a), kb = kindOf(b);
        if (ka < 0 || kb < 0) return false;
        xOut = (Ease)ka; // 第一段 → X
        yOut = (Ease)kb; // 第二段 → Y
        return true;
    }
    return false;
}

// 镜头缓动：l=线性 q=二次 qi=三次入 qo=三次出 reset=瞬移
inline float cameraEase(const std::string& name, float t) {
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    if (name == "qi") return t * t * t;
    if (name == "qo") { float v = t - 1.f; return v * t * t + 1.f; }
    if (name == "q") return t * t;
    if (name == "reset") return 1.f;
    return t; // l 及未知
}

// Rhythm-game world-coordinate helpers (screen mapping belongs to the renderer).
inline float arcXToWorld(float x) { return -8.5f * x + 4.25f; }
inline float arcYToWorld(float y) { return 1.f + 4.5f * y; }

} // namespace aff
