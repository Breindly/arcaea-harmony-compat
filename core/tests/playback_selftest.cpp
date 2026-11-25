// playback_selftest.cpp -- desktop self-test: easing math + playback timeline
// 退出码 0 = 全部通过。
#include "../aff/AffParser.h"
#include "../playback/ChartPlayback.h"

#include <cmath>
#include <cstdio>
#include <string>

static int g_fail = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            std::printf("  [通过] %s\n", msg);                                 \
        } else {                                                               \
            std::printf("  [失败] %s  (%s:%d)\n", msg, __FILE__, __LINE__);    \
            g_fail++;                                                          \
        }                                                                      \
    } while (0)

static bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

static void testEasing() {
    std::printf("[缓动数学]\n");
    CHECK(feq(aff::easeValue(aff::Ease::Linear, 0, 1, 0.5f), 0.5f), "S(0,1,.5)=.5");
    CHECK(feq(aff::easeValue(aff::Ease::SineIn, 0, 1, 0.5f), 0.70711f), "I(0,1,.5)=sin45=.70711");
    CHECK(feq(aff::easeValue(aff::Ease::SineOut, 0, 1, 0.5f), 0.29289f), "O(0,1,.5)=1-cos45=.29289");
    CHECK(feq(aff::easeValue(aff::Ease::Bezier, 0, 1, 0.f), 0.f) &&
              feq(aff::easeValue(aff::Ease::Bezier, 0, 1, 1.f), 1.f), "B 端点 0/1");

    aff::Ease x, y;
    bool ok = aff::parseArcEasing("s", x, y);
    CHECK(ok && x == aff::Ease::Linear && y == aff::Ease::Linear, "s → (L,L)");
    ok = aff::parseArcEasing("b", x, y);
    CHECK(ok && x == aff::Ease::Bezier && y == aff::Ease::Bezier, "b → (B,B)");
    ok = aff::parseArcEasing("si", x, y);
    CHECK(ok && x == aff::Ease::SineIn && y == aff::Ease::SineIn, "si → (I,I)");
    ok = aff::parseArcEasing("so", x, y);
    CHECK(ok && x == aff::Ease::SineOut && y == aff::Ease::SineOut, "so → (O,O)");
    ok = aff::parseArcEasing("sisi", x, y);
    CHECK(ok && x == aff::Ease::SineIn && y == aff::Ease::SineIn, "sisi → X=第1段(I), Y=第2段(I)");
    ok = aff::parseArcEasing("siso", x, y);
    CHECK(ok && x == aff::Ease::SineIn && y == aff::Ease::SineOut, "siso → X=I, Y=O");
    ok = aff::parseArcEasing("sosi", x, y);
    CHECK(ok && x == aff::Ease::SineOut && y == aff::Ease::SineIn, "sosi → X=O, Y=I");
    ok = aff::parseArcEasing("soso", x, y);
    CHECK(ok && x == aff::Ease::SineOut && y == aff::Ease::SineOut, "soso → X=O, Y=O");
    CHECK(!aff::parseArcEasing("xx", x, y), "非法串返回 false");

    CHECK(feq(aff::cameraEase("qi", 0.5f), 0.125f), "qi(.5)=.125");
    CHECK(feq(aff::cameraEase("qo", 0.5f), 0.875f), "qo(.5)=.875 (=1-(1-t)^3)");
    CHECK(feq(aff::cameraEase("reset", 0.1f), 1.f), "reset=瞬移(1)");
    CHECK(feq(aff::arcXToWorld(-0.5f), 8.5f) && feq(aff::arcXToWorld(1.5f), -8.5f),
          "世界X映射 [-0.5,1.5]→[8.5,-8.5]");
    CHECK(feq(aff::arcYToWorld(0.f), 1.f) && feq(aff::arcYToWorld(1.f), 5.5f),
          "世界Y映射 [0,1]→[1,5.5]");
}

static void testPlaybackFixture(const char* path) {
    std::printf("[fixture playback %s]\n", path);
    bool ok = false;
    std::string text = aff::readFileAsString(path, ok);
    if (!ok) { std::printf("  [IO错误]\n"); g_fail++; return; }
    aff::ParseResult r = aff::parseChart(text);
    CHECK(r.ok && r.issues.empty(), "谱面解析 0 issue");

    aff::ChartPlayback pb(r.chart);
    // This compatibility fixture documents the expected legacy event shape.
    CHECK(pb.allNotes().size() == 430 + 53 + 1758 + 464, "fixture event count matches");

    bool sorted = true;
    for (size_t i = 1; i < pb.allNotes().size(); i++)
        if (pb.allNotes()[i].time < pb.allNotes()[i - 1].time) { sorted = false; break; }
    CHECK(sorted, "事件按时间有序");

    // 可见窗口 [900,2000]
    std::vector<const aff::RenderNote*> vis;
    pb.collectVisible(1000, 1000, 100, vis);
    bool windowOk = true;
    int holdOrArcExtended = 0;
    for (auto* n : vis) {
        if (n->time < 900 || n->time > 2000) windowOk = false;
        if (n->time < 900 && n->endTime >= 900) holdOrArcExtended++;
    }
    CHECK(windowOk, "可见事件全部落在窗口内");
    char buf[128];
    std::snprintf(buf, sizeof(buf), "窗口非空 (n=%zu, 跨界长条/弧=%d)", vis.size(), holdOrArcExtended);
    CHECK(!vis.empty(), buf);

    // Arc endpoint and easing checks for the compatibility fixture.
    const aff::Arc& a0 = r.chart.arcs[0];
    float x, y;
    pb.arcPosAt(a0, 0, x, y);
    CHECK(feq(x, -0.5f) && feq(y, 0.f), "弧头 t=0 → 起点(-0.5,0)");
    pb.arcPosAt(a0, 1111, x, y);
    CHECK(feq(x, 1.0f) && feq(y, 0.f), "弧头 t=1111 → 终点(1.0,0)");
    pb.arcPosAt(a0, 555, x, y); // u=555/1111=0.49955（半程为555.5ms），容差放宽到1e-3
    CHECK(feq(x, 0.56066f, 1e-3f), "弧头 t=555 → si 中点 x≈0.56066");
    pb.arcPosAt(a0, 5000, x, y); // 越界钳制
    CHECK(feq(x, 1.0f), "弧头 t 越界钳制到终点");

    // The fixture's second arc exercises different easing on each axis.
    const aff::Arc& a1 = r.chart.arcs[1];
    pb.arcPosAt(a1, 555, x, y); // y: 0 + 0.5*sin45，容差1e-3（u 非精确0.5）
    CHECK(feq(y, 0.35355f, 1e-3f), "sisi 弧 Y 用第二段(si) 中点 y≈0.35355");

    // hold 激活边界：hold#0 = hold(1111,1945,2)
    const aff::Hold& h0 = r.chart.holds[0];
    CHECK(aff::ChartPlayback::holdActiveAt(h0, 1111) && aff::ChartPlayback::holdActiveAt(h0, 1945),
          "hold 首尾时刻激活");
    CHECK(!aff::ChartPlayback::holdActiveAt(h0, 1110) && !aff::ChartPlayback::holdActiveAt(h0, 1946),
          "hold 边界外不激活");

    // 可见段折线
    std::vector<float> xs, ys;
    std::vector<int32_t> ts;
    pb.arcVisiblePolyline(a0, 0, 16, xs, ys, ts);
    CHECK(xs.size() == 16 && ts.front() == 0 && ts.back() == 1111, "折线端点=弧起止");
    bool mono = true;
    for (size_t i = 1; i < ts.size(); i++) if (ts[i] < ts[i-1]) { mono = false; break; }
    CHECK(mono, "折线时间单调");
    pb.arcVisiblePolyline(a0, 800, 16, xs, ys, ts);
    CHECK(!ts.empty() && ts.front() == 800 && ts.back() == 1111, "t=800 时只画剩余段 [800,1111]");
}

int main(int argc, char** argv) {
    testEasing();
    if (argc > 1) {
        testPlaybackFixture(argv[1]);
    } else {
        std::printf("[skip] no external compatibility fixture supplied\n");
    }
    std::printf("\n===== %s（失败 %d 项） =====\n", g_fail == 0 ? "全部通过" : "存在失败", g_fail);
    return g_fail == 0 ? 0 : 1;
}
