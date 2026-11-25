// math_selftest.cpp —— 渲染数学桌面单测（无 GL 依赖）
#include "../render/PlayfieldMath.h"

#include <cstdio>

static int g_fail = 0;
#define CHECK(cond, msg)                                     \
    do {                                                     \
        if (cond) std::printf("  [通过] %s\n", msg);         \
        else { std::printf("  [失败] %s (line %d)\n", msg, __LINE__); g_fail++; } \
    } while (0)

static bool feq(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; }

// 变换点 → NDC
static arc::Vec3 project(const arc::Mat4& mvp, const arc::Vec3& p) {
    float x = mvp.at(0,0)*p.x + mvp.at(0,1)*p.y + mvp.at(0,2)*p.z + mvp.at(0,3);
    float y = mvp.at(1,0)*p.x + mvp.at(1,1)*p.y + mvp.at(1,2)*p.z + mvp.at(1,3);
    float z = mvp.at(2,0)*p.x + mvp.at(2,1)*p.y + mvp.at(2,2)*p.z + mvp.at(2,3);
    float w = mvp.at(3,0)*p.x + mvp.at(3,1)*p.y + mvp.at(3,2)*p.z + mvp.at(3,3);
    return {x / w, y / w, z / w};
}

int main() {
    using namespace arc;
    std::printf("[场地映射]\n");
    CHECK(feq(laneCenterX(0), -1.5f) && feq(laneCenterX(3), 1.5f), "lane 0..3 → -1.5..1.5");
    CHECK(feq(arcXToFloorX(-0.5f), -4.f) && feq(arcXToFloorX(0.5f), 0.f) && feq(arcXToFloorX(1.5f), 4.f),
          "弧 x [-0.5,1.5] → 地面 [-4,4]");
    CHECK(feq(arcYToHeight(0.f), 0.4f) && feq(arcYToHeight(1.f), 3.f), "弧高 [0,1] → [0.4,3.0]");
    CHECK(feq(timeToZ(1000, 1000, 0.0045f), 0.f) && feq(timeToZ(2000, 1000, 0.0045f), -4.5f),
          "时间→z：当前=判定线，1s后=-4.5（远处为负）");

    std::printf("[投影]\n");
    Mat4 P = perspective(50.f, 0.5625f, 0.1f, 100.f);
    CHECK(feq(P.at(3, 2), -1.f) && feq(P.at(3, 3), 0.f), "P 的 w 生成项 (row3: 0,0,-1,0)");
    Mat4 vp = mat4Mul(P, playfieldView());

    Vec3 judge = project(vp, {0.f, 0.f, 0.f});          // 判定线中心
    CHECK(judge.y < -0.3f && judge.y > -1.f && feq(judge.x, 0.f, 1e-3f),
          "判定线中心在屏幕下方视野内且水平居中");
    Vec3 far1 = project(vp, {0.f, 0.f, -30.f});         // 远处场地
    CHECK(far1.y > judge.y && far1.y < 1.f, "远处更靠屏幕中心（透视收敛）");

    Vec3 laneL = project(vp, {-1.5f, 0.f, 0.f});
    Vec3 laneR = project(vp, {1.5f, 0.f, 0.f});
    CHECK(laneL.x < 0 && laneR.x > 0 && feq(laneL.x + laneR.x, 0.f, 1e-3f),
          "lane0 在左 lane3 在右且对称");

    Vec3 sky = project(vp, {0.f, arcYToHeight(1.f), 0.f}); // 最高弧点
    CHECK(sky.y > judge.y && sky.y < 1.f, "天弧最高点位于判定线上方视野内");

    std::printf("\n===== %s（失败 %d） =====\n", g_fail ? "存在失败" : "全部通过", g_fail);
    return g_fail ? 1 : 0;
}
