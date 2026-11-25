// aff_selftest.cpp —— 桌面自测：解析谱面并输出统计/校验
// 用法:
//   aff_selftest <file.aff> [more.aff ...]   —— 解析指定谱面，详细输出
//   aff_selftest --dir <songs目录>            —— 递归全量解析，汇总统计
// 退出码：0 = 全部成功；1 = 存在解析 issue。
#include "../aff/AffParser.h"
#include "../aff/TimingModel.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct CorpusStats {
    size_t files = 0;
    size_t failedFiles = 0;
    size_t taps = 0, holds = 0, arcs = 0, arcTaps = 0;
    size_t timings = 0, cameras = 0, sceneControls = 0, timingGroups = 0;
    size_t totalIssues = 0;
    std::vector<std::string> firstFailures; // 前 20 个失败文件
};

static void addChart(const aff::Chart& c, CorpusStats& s) {
    s.taps += c.taps.size();
    s.holds += c.holds.size();
    s.arcs += c.arcs.size();
    for (auto& a : c.arcs) s.arcTaps += a.arcTaps.size();
    s.timings += c.timings.size();
    for (auto& g : c.groups) s.timings += g.timings.size();
    s.cameras += c.cameras.size();
    s.sceneControls += c.sceneControls.size();
    s.timingGroups += c.groups.size();
    for (auto& g : c.groups) {
        s.taps += g.taps.size();
        s.holds += g.holds.size();
        s.arcs += g.arcs.size();
        for (auto& a : g.arcs) s.arcTaps += a.arcTaps.size();
    }
}

static void parseOne(const std::string& path, CorpusStats& s, bool verbose) {
    bool ok = false;
    std::string text = aff::readFileAsString(path, ok);
    if (!ok) {
        std::printf("[IO错误] %s\n", path.c_str());
        s.failedFiles++;
        s.totalIssues++;
        return;
    }
    aff::ParseResult r = aff::parseChart(text);
    s.files++;
    if (!r.ok) {
        s.failedFiles++;
        s.totalIssues += r.issues.size();
        if (s.firstFailures.size() < 20) {
            std::string msg = r.issues.empty() ? "?" : r.issues.front().message;
            int line = r.issues.empty() ? 0 : r.issues.front().line;
            char buf[512];
            std::snprintf(buf, sizeof(buf), "%s  第%d行: %s", path.c_str(), line, msg.c_str());
            s.firstFailures.push_back(buf);
        }
    }
    addChart(r.chart, s);
    if (verbose) {
        size_t t = 0, h = 0, a = 0, at = 0, tm = 0, cm = 0, sc = 0, tg = 0;
        aff::Chart c2 = r.chart;
        t = c2.taps.size(); h = c2.holds.size(); a = c2.arcs.size();
        for (auto& x : c2.arcs) at += x.arcTaps.size();
        tm = c2.timings.size(); cm = c2.cameras.size(); sc = c2.sceneControls.size();
        tg = c2.groups.size();
        for (auto& g : c2.groups) {
            t += g.taps.size(); h += g.holds.size(); a += g.arcs.size(); tm += g.timings.size();
            for (auto& x : g.arcs) at += x.arcTaps.size();
        }
        std::printf("%s\n  taps=%zu holds=%zu arcs=%zu arctaps=%zu timing=%zu camera=%zu sc=%zu tg=%zu\n"
                    "  可判定音符总数=%zu  audioOffset=%d  issues=%zu\n",
                    path.c_str(), t, h, a, at, tm, cm, sc, tg,
                    c2.totalPlayableNotes(), c2.meta.audioOffset, r.issues.size());
        if (!c2.timings.empty()) {
            aff::TimingModel tm2(c2.timings);
            const auto* seg = tm2.segmentAt(0);
            if (seg) {
                std::printf("  [时轴] 起始BPM=%.2f beatsPerLine=%.2f, beatAt(0)=%.3f\n",
                            seg->bpm, seg->beatsPerLine, tm2.beatAt(0));
            }
        }
    }
}

static void dumpChart(const std::string& path) {
    bool ok = false;
    std::string text = aff::readFileAsString(path, ok);
    if (!ok) { std::printf("[IO错误] %s\n", path.c_str()); return; }
    aff::ParseResult r = aff::parseChart(text);
    const aff::Chart& c = r.chart;
    std::printf("== %s  issues=%zu ==\n", path.c_str(), r.issues.size());
    std::printf("meta: audioOffset=%d densityFactor=%.2f\n", c.meta.audioOffset,
                c.meta.timingPointDensityFactor);
    for (size_t i = 0; i < c.timings.size() && i < 3; i++)
        std::printf("timing#%zu: t=%d bpm=%.2f bpl=%.2f\n", i, c.timings[i].time,
                    c.timings[i].bpm, c.timings[i].beatsPerLine);
    for (size_t i = 0; i < c.taps.size() && i < 3; i++)
        std::printf("tap#%zu: t=%d lane=%d\n", i, c.taps[i].time, c.taps[i].lane);
    for (size_t i = 0; i < c.holds.size() && i < 3; i++)
        std::printf("hold#%zu: t=%d..%d lane=%d\n", i, c.holds[i].time, c.holds[i].endTime,
                    c.holds[i].lane);
    for (size_t i = 0; i < c.arcs.size() && i < 3; i++) {
        const aff::Arc& a = c.arcs[i];
        std::printf("arc#%zu: t=%d..%d x=%.2f..%.2f ease=%s y=%.2f..%.2f color=%d sky=%s trace=%d",
                    i, a.time, a.endTime, a.xFrom, a.xTo, a.easing.c_str(), a.yFrom, a.yTo,
                    a.color, a.skyline.c_str(), (int)a.isTrace);
        if (!a.arcTaps.empty()) {
            std::printf(" arctaps=[");
            for (size_t j = 0; j < a.arcTaps.size() && j < 5; j++)
                std::printf("%s%d", j ? "," : "", a.arcTaps[j].time);
            std::printf("]");
        }
        std::printf("\n");
    }
    for (size_t i = 0; i < c.cameras.size() && i < 2; i++)
        std::printf("camera#%zu: t=%d xyz=(%.1f,%.1f,%.1f) rxy=(%.1f,%.1f,%.1f) ease=%s dur=%d\n",
                    i, c.cameras[i].time, c.cameras[i].x, c.cameras[i].y, c.cameras[i].z,
                    c.cameras[i].rx, c.cameras[i].ry, c.cameras[i].rz,
                    c.cameras[i].easing.c_str(), c.cameras[i].duration);
    for (size_t i = 0; i < c.sceneControls.size() && i < 2; i++)
        std::printf("sc#%zu: t=%d type=%s fp=%.2f ip=%d\n", i, c.sceneControls[i].time,
                    c.sceneControls[i].type.c_str(), c.sceneControls[i].fparam,
                    c.sceneControls[i].iparam);
    for (size_t i = 0; i < c.groups.size() && i < 2; i++) {
        const aff::TimingGroup& g = c.groups[i];
        std::printf("tg#%zu: noinput=%d fading=%d anglex=%.1f angley=%.1f notes=%zu\n", i,
                    (int)g.flags.noInput, (int)g.flags.fadingHolds, g.flags.angleX, g.flags.angleY,
                    g.taps.size() + g.holds.size() + g.arcs.size());
    }
    if (!c.timings.empty()) {
        aff::TimingModel tm(c.timings);
        auto bars = tm.barLineTimes(0, 20000);
        std::printf("小节线(0..20000ms) 前6条:");
        for (size_t i = 0; i < bars.size() && i < 6; i++) std::printf(" %d", bars[i]);
        if (!bars.empty() && c.timings[0].bpm > 0) {
            double expect = 60000.0 / c.timings[0].bpm * c.timings[0].beatsPerLine;
            std::printf("  (期望间隔 %.2fms)", expect);
        }
        std::printf("\n");
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("用法: aff_selftest <file.aff>... | --dir <songs目录> [--verbose] | --dump <file.aff>\n");
        return 2;
    }
    if (std::strcmp(argv[1], "--dump") == 0) {
        if (argc < 3) { std::printf("--dump 需要文件参数\n"); return 2; }
        for (int i = 2; i < argc; i++) dumpChart(argv[i]);
        return 0;
    }
    CorpusStats s;
    bool verbose = false;
    std::vector<std::string> files;
    if (std::strcmp(argv[1], "--dir") == 0) {
        if (argc < 3) { std::printf("--dir 需要目录参数\n"); return 2; }
        for (int i = 3; i < argc; i++) if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;
        fs::path root(argv[2]);
        std::error_code ec;
        fs::recursive_directory_iterator it(root, ec), end;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file(ec) && it->path().extension() == ".aff") {
                files.push_back(it->path().string());
            }
        }
        std::printf("扫描到 %zu 个 .aff 文件\n", files.size());
    } else {
        for (int i = 1; i < argc; i++) {
            if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;
            else files.push_back(argv[i]);
        }
    }
    for (auto& f : files) parseOne(f, s, verbose);

    std::printf("\n===== 汇总 =====\n");
    std::printf("文件: %zu (失败 %zu)\n", s.files, s.failedFiles);
    std::printf("tap=%zu hold=%zu arc=%zu arctap=%zu timing=%zu camera=%zu scenecontrol=%zu timinggroup=%zu\n",
                s.taps, s.holds, s.arcs, s.arcTaps, s.timings, s.cameras, s.sceneControls,
                s.timingGroups);
    std::printf("issue 总数: %zu\n", s.totalIssues);
    if (!s.firstFailures.empty()) {
        std::printf("前 %zu 个失败:\n", s.firstFailures.size());
        for (auto& f : s.firstFailures) std::printf("  %s\n", f.c_str());
    }
    return s.failedFiles == 0 ? 0 : 1;
}
