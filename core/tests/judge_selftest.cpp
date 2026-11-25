// judge_selftest.cpp —— 判定核心桌面自测
// 退出码 0 = 全部通过。
#include "../aff/AffParser.h"
#include "../judge/NoteJudge.h"

#include <cstdio>
#include <filesystem>
#include <string>

static int g_fail = 0;
#define CHECK(cond, msg)                                     \
    do {                                                     \
        if (cond) std::printf("  [通过] %s\n", msg);         \
        else { std::printf("  [失败] %s (line %d)\n", msg, __LINE__); g_fail++; } \
    } while (0)

using namespace aff;

static Chart makeChart() {
    const char* src =
        "AudioOffset:0\n"
        "-\n"
        "timing(0,120,4);\n"
        "(1000,0);\n"                                    // tap lane0 @1000
        "(1000,2);\n"                                    // tap lane2 @1000
        "(2000,1);\n"                                    // tap lane1 @2000
        "hold(3000,3500,3);\n"                           // hold lane3 @3000
        "arc(4000,5000,0.00,1.00,s,0.00,1.00,0,none,true);\n"   // 影弧（不判定）
        "arc(6000,7000,0.00,1.00,s,0.00,1.00,1,none,false)[arctap(6500)];\n"
        "(8000,0);\n"
        "timinggroup(noinput){\n"
        "(9000,0);\n"
        "arc(9100,9500,0.00,1.00,s,0.00,1.00,0,none,false)[arctap(9200)];\n"
        "};\n";
    ParseResult r = parseChart(src);
    return std::move(r.chart);
}

int main(int argc, char** argv) {
    Chart ch = makeChart();
    ChartPlayback pb(ch);

    std::printf("[构造]\n");
    NoteJudge jd(pb);
    CHECK(jd.totalJudgeable() == 7, "可判定数=7 (tap×4 + hold头 + 非影弧头 + arctap；影弧排除)");

    std::printf("[精准判定]\n");
    CHECK(jd.tapLane(1000, 0) == Grade::Pure, "tap lane0 精准 → Pure");
    CHECK(jd.tapLane(1030, 2) == Grade::Far, "tap lane2 偏差30ms → Far");
    CHECK(jd.tapLane(1040, 0) == Grade::None, "lane0 无可击 note（已判）→ None");
    CHECK(jd.combo() == 2 && jd.pure() == 1 && jd.far() == 1, "combo=2 pure=1 far=1");
    CHECK(jd.lastGrade() == (uint8_t)Grade::Far && jd.lastDelta() == -30,
          "判定事件：Far + late 30ms");
    CHECK(jd.judgeSerial() == 2, "判定事件序号随每次结算递增");

    std::printf("[窗口外/错误轨道]\n");
    CHECK(jd.tapLane(2200, 1) == Grade::None, "偏差200ms > lost窗 → None");

    std::printf("[超时 Lost]\n");
    jd.update(2110); // 2000 的 tap 超时（>100ms）
    CHECK(jd.lost() == 1 && jd.combo() == 0, "2000ms tap 超时 → Lost, combo 断");
    CHECK(jd.lastGrade() == (uint8_t)Grade::Lost, "超时判定事件为 Lost");
    CHECK(jd.tapLane(2001, 1) == Grade::None, "已 Lost 的 note 不可再击");

    std::printf("[天空判定]\n");
    CHECK(jd.tapSky(6500, 0.5f) == Grade::Pure, "arctap 精准 → Pure");
    CHECK(jd.tapSky(6560, 0.5f) == Grade::None, "arctap 偏差60ms 超出 Far 窗 → None");
    CHECK(jd.combo() == 1, "天空判定接 combo");

    std::printf("[连续弧判定]\n");
    {
        const char* arcSrc =
            "AudioOffset:0\n-\ntiming(0,120,4);\n"
            "arc(1000,2000,0.00,1.00,s,0.00,1.00,1,none,false);\n";
        ParseResult ar = parseChart(arcSrc);
        ChartPlayback pba(ar.chart);
        NoteJudge ja(pba);
        ja.update(1000, {0.0f});
        CHECK(ja.isHolding(0), "弧头 x 附近按下 → 开始按住");
        ja.update(1200, {0.2f});
        CHECK(ja.isHolding(0), "跟随弧 x（容差内）保持按住");
        ja.update(2000, {1.0f});
        CHECK(ja.pure() == 1 && ja.lost() == 0, "弧保持到尾部 → 结算 Pure");

        NoteJudge jb(pba);
        jb.update(1000, {0.0f});
        jb.update(1101, {});
        CHECK(jb.lost() == 1, "脱离弧超过 100ms → Lost");
    }

    std::printf("[影弧不可击]\n");
    NoteJudge jd2(pb);
    CHECK(jd2.tapSky(4000, 0.0f) == Grade::None, "影弧头 tapSky 不命中（不在候选）");
    jd2.update(5110); // 扫到 ≤5010：tap1000×2 + tap2000 + hold3000 = 4 个超时，影弧@4000 被排除
    CHECK(jd2.lost() == 4, "超时只计 4 个（影弧不计 Lost）");

    std::printf("[noinput 组]\n");
    NoteJudge jdNo(pb);
    CHECK(jdNo.totalJudgeable() == 7, "noinput 组内 note 不计入可判定总数");
    CHECK(jdNo.tapLane(9000, 0) == Grade::None, "noinput 地面 tap 不可击");
    CHECK(jdNo.tapSky(9200, 0.25f) == Grade::None, "noinput arctap 不可击");
    for (int32_t t = 0; t <= 12000; t += 50) jdNo.update(t);
    CHECK(jdNo.lost() == 7, "noinput 组内 note 永不产生 Lost");
    CHECK(jdNo.combo() == 0, "全放空 combo=0（noinput 不参与）");

    std::printf("[外部谱面回归]\n");
    if (argc > 1) {
        bool ok = false;
        std::string text = readFileAsString(argv[1], ok);
        if (!ok) {
            std::printf("  [跳过] 外部谱面不可读: %s\n", argv[1]);
        }
        else {
        ParseResult r = parseChart(text);
        ChartPlayback pb2(r.chart);
        NoteJudge jd3(pb2);
        // 期望值独立遍历 Chart 模型计算（顶层 + timinggroup 内），与 NoteJudge 内部逻辑互证
        size_t expTotal = 0;
        auto acc = [&](const std::vector<Tap>& ts, const std::vector<Hold>& hs,
                       const std::vector<Arc>& as) {
            expTotal += ts.size() + hs.size();
            for (auto& a : as) {
                expTotal += a.arcTaps.size();
                if (!a.isTrace) expTotal++;
            }
        };
        acc(r.chart.taps, r.chart.holds, r.chart.arcs);
        for (auto& g : r.chart.groups) {
            if (g.flags.noInput) continue;
            acc(g.taps, g.holds, g.arcs);
        }
        CHECK(jd3.totalJudgeable() == expTotal, "外部谱面可判定数与独立遍历一致");
        // 全 Miss 走一遍
        int32_t end = pb2.allNotes().back().time + 500;
        uint32_t lost = 0;
        for (int32_t t = 0; t < end; t += 50) lost += (uint32_t)jd3.update(t);
        CHECK(lost == jd3.totalJudgeable() && jd3.lost() == jd3.totalJudgeable(),
              "全放空：Lost 总数=可判定总数");
        CHECK(jd3.combo() == 0, "全放空 combo=0");
        }
    } else {
        std::printf("  [跳过] 未提供外部谱面路径\n");
    }

    std::printf("[血条 Recollection Rate]\n");
    {
        auto makeTaps = [](int n) {
            std::string s = "AudioOffset:0\n-\ntiming(0,120,4);\n";
            for (int i = 0; i < n; i++) s += "(" + std::to_string(1000 + i * 1000) + ",0);\n";
            return s;
        };
        // RF 分段（可判定 note 数 TN 决定）
        {
            ParseResult r = parseChart(makeTaps(100));
            ChartPlayback p(r.chart);
            NoteJudge j(p);
            CHECK(std::fabs(j.recollectionFactor() - 1.0f) < 1e-4f, "RF: TN=100 → 0.2+80/100=1.0");
            for (int i = 0; i < 100; i++) j.tapLane(1000 + i * 1000, 0);
            CHECK(std::fabs(j.gaugeF() - 100.f) < 1e-3f, "全 Pure：RR 恰好到 100（上限钳制）");
            j.finalize();
            CHECK(!j.trackLost(), "终曲 RR=100 ≥ 70 → Track Complete");
        }
        {
            ParseResult r = parseChart(makeTaps(400));
            ChartPlayback p(r.chart);
            NoteJudge j(p);
            CHECK(std::fabs(j.recollectionFactor() - 0.28f) < 1e-4f, "RF: TN=400 → 0.2+32/400=0.28");
        }
        {
            ParseResult r = parseChart(makeTaps(800));
            ChartPlayback p(r.chart);
            NoteJudge j10(p, JudgeWindows{}, GaugeSetup{GaugeKind::Normal, 10});
            NoteJudge j11(p, JudgeWindows{}, GaugeSetup{GaugeKind::Normal, 11});
            CHECK(std::fabs(j10.recollectionFactor() - (0.08f + 96.f / 800.f)) < 1e-5f,
                  "RF: TN=800 Lv10 → 0.08+96/800");
            CHECK(std::fabs(j11.recollectionFactor() - (0.08f + 96.f / 800.f) * 0.8f) < 1e-5f,
                  "RF: TN=800 Lv11 → ×0.8");
        }
        // Normal：归零不中途死；终曲结算清线
        {
            ParseResult r = parseChart(makeTaps(600));
            ChartPlayback p(r.chart);
            NoteJudge j(p); // RF = 0.08+96/600 = 0.24
            CHECK(std::fabs(j.recollectionFactor() - 0.24f) < 1e-4f, "RF: TN=600 → 0.24");
            for (int i = 0; i < 50; i++) j.update(1000 + i * 1000 + 150);
            CHECK(j.lost() == 50 && j.gaugeF() == 0.f, "50 连 Lost：RR 钳在 0");
            CHECK(!j.trackLost(), "Normal 归零不中途 Track Lost");
            for (int i = 50; i < 600; i++) j.tapLane(1000 + i * 1000, 0);
            j.finalize();
            CHECK(!j.trackLost(), "剩余全 Pure 拉回 100 → 终曲 Track Complete");
        }
        {
            ParseResult r = parseChart(makeTaps(600));
            ChartPlayback p(r.chart);
            NoteJudge j(p);
            for (int i = 0; i < 300; i++) j.update(1000 + i * 1000 + 150); // 300 Lost
            for (int i = 300; i < 500; i++) j.tapLane(1000 + i * 1000, 0); // 200 Pure
            CHECK(!j.trackLost(), "终曲前不判 Track Lost");
            j.finalize(); // 剩 100 个未击 → Lost；RR = 200×0.24 = 48 < 70
            CHECK(j.trackLost(), "终曲 RR<70 → Track Lost");
        }
        // Easy：Lost −1.2，归零不中途死
        {
            ParseResult r = parseChart(makeTaps(600));
            ChartPlayback p(r.chart);
            NoteJudge j(p, JudgeWindows{}, GaugeSetup{GaugeKind::Easy, 9});
            for (int i = 0; i < 10; i++) j.tapLane(1000 + i * 1000, 0); // +0.24×10
            for (int i = 10; i < 12; i++) j.update(1000 + i * 1000 + 150); // −1.2×2
            CHECK(std::fabs(j.gaugeF() - 0.f) < 1e-4f, "Easy：2.4 − 1.2×2 = 0");
            CHECK(!j.trackLost(), "Easy 归零不中途 Track Lost");
        }
        // Hard：100 起，Lost 按 RR 分段（≥40:-9 / 30~40:线性 / ≤30:-5），归零立即死
        {
            ParseResult r = parseChart(makeTaps(600));
            ChartPlayback p(r.chart);
            NoteJudge j(p, JudgeWindows{}, GaugeSetup{GaugeKind::Hard, 9});
            j.update(1150); // 1 Lost
            CHECK(std::fabs(j.gaugeF() - 91.f) < 1e-4f, "Hard：100 − 9 = 91");
            for (int i = 1; i < 14; i++) j.update(1000 + i * 1000 + 150);
            CHECK(j.trackLost(), "Hard 连续 Lost 至归零 → 立即 Track Lost");
        }
        {
            ParseResult r = parseChart(makeTaps(600));
            ChartPlayback p(r.chart);
            NoteJudge j(p, JudgeWindows{}, GaugeSetup{GaugeKind::Hard, 9});
            j.tapLane(1000, 0); // 1 Pure
            for (int i = 1; i < 600; i++) j.tapLane(1000 + i * 1000, 0);
            j.finalize();
            CHECK(!j.trackLost(), "Hard 全程存活（>0）→ Track Complete（清线只对 Normal/Easy）");
        }
    }

    std::printf("[noinput 全语料]\n");
    {
        namespace fs = std::filesystem;
        const fs::path root = argc > 2 ? fs::path(argv[2]) : fs::path{};
        size_t charts = 0, noinputCharts = 0, mismatch = 0;
        if (!root.empty() && fs::exists(root)) {
            for (const auto& ent : fs::recursive_directory_iterator(root)) {
                if (!ent.is_regular_file() || ent.path().extension() != ".aff") continue;
                bool readOk = false;
                std::string t = readFileAsString(ent.path().string(), readOk);
                if (!readOk) continue;
                charts++;
                ParseResult pr = parseChart(t);
                size_t exp = 0;
                bool hasNoInput = false;
                auto acc = [&](const std::vector<Tap>& ts, const std::vector<Hold>& hs,
                               const std::vector<Arc>& as) {
                    exp += ts.size() + hs.size();
                    for (auto& a : as) {
                        exp += a.arcTaps.size();
                        if (!a.isTrace) exp++;
                    }
                };
                acc(pr.chart.taps, pr.chart.holds, pr.chart.arcs);
                for (auto& g : pr.chart.groups) {
                    if (g.flags.noInput) {
                        hasNoInput = true;
                        continue;
                    }
                    acc(g.taps, g.holds, g.arcs);
                }
                ChartPlayback pbc(pr.chart);
                NoteJudge jdc(pbc);
                if (jdc.totalJudgeable() != exp) mismatch++;
                if (hasNoInput) noinputCharts++;
            }
            std::printf("  谱面 %zu 张，其中 noinput 组 %zu 张，计数不一致 %zu 张\n", charts,
                        noinputCharts, mismatch);
            CHECK(charts > 1000 && noinputCharts > 0 && mismatch == 0,
                  "全语料 noinput 可判定计数一致");
        } else {
            std::printf("  [跳过] 曲库目录不存在\n");
        }
    }

    std::printf("\n===== %s（失败 %d） =====\n", g_fail ? "存在失败" : "全部通过", g_fail);
    return g_fail ? 1 : 0;
}
