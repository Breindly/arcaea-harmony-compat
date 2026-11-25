// PlayfieldMath.h —— 渲染数学（无 GL 依赖，桌面可单测）
// 场地模型：透视相机俯视高速公路；判定线 z=0，未来时间向 +z 延伸。
//   地面：4 轨 x∈[-2,2]（lane 0..3 中心 -1.5..1.5，轨宽 1）
//   天弧：x∈[-0.5,1.5] 映射为 worldX∈[-4,4]；高度 y∈[0,1] 映射为 0.4..3.0
#pragma once

#include <cmath>
#include <cstdint>

namespace arc {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& at(int r, int c) { return m[c * 4 + r]; }
    const float& at(int r, int c) const { return m[c * 4 + r]; }
};

inline Mat4 mat4Mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.at(i, k) * b.at(k, c);
            r.at(i, c) = s;
        }
    return r;
}

// OpenGL 惯例（列主序、clip z∈[-1,1]）
inline Mat4 perspective(float fovYDeg, float aspect, float nearZ, float farZ) {
    Mat4 r;
    float f = 1.f / std::tan(fovYDeg * 3.14159265f / 360.f);
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = (farZ + nearZ) / (nearZ - farZ);
    r.at(2, 3) = 2.f * farZ * nearZ / (nearZ - farZ); // 第三行平移项
    r.at(3, 2) = -1.f;                                 // w = -z_view（面前点 w>0）
    r.at(3, 3) = 0.f;
    return r;
}

inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f{center.x - eye.x, center.y - eye.y, center.z - eye.z};
    float fl = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    f.x /= fl; f.y /= fl; f.z /= fl;
    Vec3 s{f.y * up.z - f.z * up.y, f.z * up.x - f.x * up.z, f.x * up.y - f.y * up.x};
    float sl = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
    s.x /= sl; s.y /= sl; s.z /= sl;
    Vec3 u{s.y * f.z - s.z * f.y, s.z * f.x - s.x * f.z, s.x * f.y - s.y * f.x};
    Mat4 r;
    r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z;
    r.at(0, 3) = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
    r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z;
    r.at(1, 3) = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
    r.at(2, 3) = f.x * eye.x + f.y * eye.y + f.z * eye.z;
    return r;
}

// ――― 场地映射 ―――
// 竖屏下把水平视野压缩，让 4 条轨道占约 75% 屏宽（避免判定线处轨道被裁切）。
inline constexpr float kFieldAspectBoost = 1.5f;
inline constexpr float kFieldWidthFrac = 0.75f;

inline float laneCenterX(int lane) { return -1.5f + (float)lane; }         // 0..3 → -1.5..1.5
inline float arcXToFloorX(float x) { return (x - 0.5f) * 4.f; }            // [-0.5,1.5] → [-4,4]
inline float arcYToHeight(float y) { return 0.4f + y * 2.6f; }             // [0,1] → [0.4,3.0]

// 屏幕归一化 x ↔ 场地坐标（与 draw() 里的水平视野压缩保持一致）
inline float fieldXFromScreen(float xNorm) {
    return (xNorm - 0.5f) / kFieldWidthFrac * 4.f;
}
inline int laneFromScreenX(float xNorm) {
    int lane = (int)std::floor(fieldXFromScreen(xNorm) + 2.f);
    if (lane < 0) lane = 0;
    if (lane > 3) lane = 3;
    return lane;
}
// 弧的 x∈[-0.5,1.5] 映射到屏幕：screen = 0.5 + (x-0.5)*kFieldWidthFrac
inline float arcXFromScreen(float xNorm) {
    return (xNorm - 0.5f) / kFieldWidthFrac + 0.5f;
}

// note 时间 → 场上 z 坐标：未来为负（远处），判定线 z=0，相机在 +z 侧朝 -z 看
// （OpenGL 规范朝向：面朝 -z 时右=+x，lane0 在画面左侧）。unitsPerMs 典型 0.0045。
inline float timeToZ(int32_t noteTimeMs, int32_t nowMs, float unitsPerMs) {
    return -(float)(noteTimeMs - nowMs) * unitsPerMs;
}

// 播放相机（全局一致，后续 camera() 事件在此插值）
inline Mat4 playfieldView() {
    return lookAt(Vec3{0.f, 4.6f, 6.2f}, Vec3{0.f, 1.0f, -2.2f}, Vec3{0.f, 1.f, 0.f});
}

} // namespace arc
