// AffParser.cpp —— .aff 词法 + 递归下降解析器实现
#include "AffParser.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace aff {
namespace {

// ――――――― 词法 ―――――――

enum class TokKind { End, Ident, Number, Punct };

struct Token {
    TokKind kind = TokKind::End;
    std::string text;   // ident 原文 / 数码原文 / 单个标点
    double num = 0.0;
    bool isInt = false;
    int line = 0;
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : s_(src) {
        // 跳过 UTF-8 BOM
        if (s_.size() >= 3 && (unsigned char)s_[0] == 0xEF && (unsigned char)s_[1] == 0xBB &&
            (unsigned char)s_[2] == 0xBF) {
            pos_ = 3;
        }
    }

    Token next() {
        skipSpaceAndComments();
        Token t;
        t.line = line_;
        if (pos_ >= s_.size()) return t; // End

        char c = s_[pos_];

        if (std::isalpha((unsigned char)c) || c == '_') {
            while (pos_ < s_.size() &&
                   (std::isalnum((unsigned char)s_[pos_]) || s_[pos_] == '_')) {
                t.text += s_[pos_++];
            }
            t.kind = TokKind::Ident;
            return t;
        }

        // 数码：仅当真正以数字开头（负号在 parseNumber 显式处理）
        if (std::isdigit((unsigned char)c) || (c == '.' && pos_ + 1 < s_.size() &&
                                               std::isdigit((unsigned char)s_[pos_ + 1]))) {
            return lexNumber(t);
        }

        // 单字符标点
        t.kind = TokKind::Punct;
        t.text = std::string(1, c);
        pos_++;
        return t;
    }

private:
    void skipSpaceAndComments() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == '\n') {
                line_++;
                pos_++;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                pos_++;
            } else {
                return;
            }
        }
    }

    Token lexNumber(Token t) {
        bool hasDot = false;
        bool hasDigit = false;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (std::isdigit((unsigned char)c)) {
                hasDigit = true;
                t.text += c;
                pos_++;
            } else if (c == '.' && !hasDot && pos_ + 1 < s_.size() &&
                       std::isdigit((unsigned char)s_[pos_ + 1])) {
                // 允许前导点小数（.5）；入口条件已保证点后是数字，
                // 故此处必消费至少 1 字符 —— 零宽度 token 会令 advance() 死循环（模糊测试实测）
                hasDot = true;
                t.text += c;
                pos_++;
            } else {
                break;
            }
        }
        if (!hasDigit) {
            // 防御：任何情况下不允许零消费的 Number token
            t.kind = TokKind::Punct;
            t.text = std::string(1, s_[pos_]);
            pos_++;
            return t;
        }
        t.kind = TokKind::Number;
        t.isInt = !hasDot;
        t.num = 0.0;
        std::sscanf(t.text.c_str(), "%lf", &t.num);
        return t;
    }

    const std::string& s_;
    size_t pos_ = 0;
    int line_ = 1;
};

// ――――――― 解析器 ―――――――

class Parser {
public:
    Parser(const std::string& src, ParseResult& out) : lex_(src), out_(out) {
        ahead_ = lex_.next();
    }

    void run() {
        parseHeader();
        // 剩余全部是语句，直到 EOF
        while (!checkEnd()) {
            parseStatement(out_.chart, nullptr);
        }
    }

private:
    // ―― 头部：AudioOffset:N / TimingPointDensityFactor:F，直到 '-' 分隔线 ――
    void parseHeader() {
        while (true) {
            if (ahead_.kind == TokKind::Punct && ahead_.text == "-") {
                advance();
                return;
            }
            if (ahead_.kind != TokKind::Ident) {
                if (ahead_.kind == TokKind::End) return; // 无分隔线的空谱
                issue("头部出现意外符号，跳到 '-' 分隔线");
                skipUntilDashOrEnd();
                if (ahead_.kind == TokKind::Punct && ahead_.text == "-") advance();
                return;
            }
            std::string key = ahead_.text;
            advance();
            if (!(ahead_.kind == TokKind::Punct && ahead_.text == ":")) {
                issue("头部键后应为 ':'，跳到 '-' 分隔线");
                skipUntilDashOrEnd();
                if (ahead_.kind == TokKind::Punct && ahead_.text == "-") advance();
                return;
            }
            advance();
            double v = expectNumber("头部值");
            if (key == "AudioOffset") {
                out_.chart.meta.audioOffset = (int32_t)std::llround(v);
            } else if (key == "TimingPointDensityFactor") {
                out_.chart.meta.timingPointDensityFactor = v;
            } else {
                issue("未知头部键: " + key);
            }
        }
    }

    // ―― 语句分派（tg 内外通用；group 非空时 note 落组） ――
    void parseStatement(Chart& ch, TimingGroup* tg) {
        if (ahead_.kind == TokKind::End) return;

        if (ahead_.kind == TokKind::Punct && ahead_.text == "(") {
            parseTap(ch, tg);
            return;
        }
        if (ahead_.kind != TokKind::Ident) {
            issue("语句应以标识符或 '(' 开始，跳过至 ';' 或 '}'");
            skipStatement();
            return;
        }
        const std::string kw = ahead_.text;
        if (kw == "hold") parseHold(ch, tg);
        else if (kw == "arc") parseArc(ch, tg);
        else if (kw == "timing") parseTiming(ch, tg);
        else if (kw == "camera") parseCamera(ch);
        else if (kw == "scenecontrol") parseSceneControl(ch);
        else if (kw == "timinggroup") parseTimingGroup(ch);
        else {
            issue("未知语句类型: " + kw);
            skipStatement();
        }
    }

    void parseTap(Chart& ch, TimingGroup* tg) {
        int line0 = ahead_.line;
        advance(); // '('
        int64_t t = expectInt("tap 时间");
        expectComma();
        int64_t lane = expectInt("tap 轨道");
        expectCloseParen();
        expectSemi();
        Tap n{(int32_t)t, (int32_t)lane};
        if (tg) tg->taps.push_back(n); else ch.taps.push_back(n);
        (void)line0;
    }

    void parseHold(Chart& ch, TimingGroup* tg) {
        advance(); // 'hold'
        expectOpenParen();
        int64_t t0 = expectInt("hold 起始");
        expectComma();
        int64_t t1 = expectInt("hold 结束");
        expectComma();
        int64_t lane = expectInt("hold 轨道");
        expectCloseParen();
        expectSemi();
        Hold n{(int32_t)t0, (int32_t)t1, (int32_t)lane};
        if (tg) tg->holds.push_back(n); else ch.holds.push_back(n);
    }

    void parseArc(Chart& ch, TimingGroup* tg) {
        advance(); // 'arc'
        expectOpenParen();
        int64_t t0 = expectInt("arc 起始");
        expectComma();
        int64_t t1 = expectInt("arc 结束");
        expectComma();
        double x0 = expectNumber("arc x0");
        expectComma();
        double x1 = expectNumber("arc x1");
        expectComma();
        std::string easing = expectIdentOrNumber("arc 缓动");
        expectComma();
        double y0 = expectNumber("arc y0");
        expectComma();
        double y1 = expectNumber("arc y1");
        expectComma();
        int64_t color = expectInt("arc 颜色");
        expectComma();
        std::string skyline = expectIdentOrNumber("arc skyline");
        expectComma();
        bool trace = expectTrace("arc isTrace");

        // 容忍 isTrace 之后的扩展字段（如 altergate 的第 11 参数）
        while (ahead_.kind == TokKind::Punct && ahead_.text == ",") {
            advance();
            expectIdentOrNumber("arc 扩展字段");
        }
        expectCloseParen();

        Arc a;
        a.time = (int32_t)t0;
        a.endTime = (int32_t)t1;
        a.xFrom = (float)x0;
        a.xTo = (float)x1;
        a.easing = easing;
        a.yFrom = (float)y0;
        a.yTo = (float)y1;
        a.color = (int32_t)color;
        a.skyline = skyline;
        a.isTrace = trace;

        // 可选 [arctap(t),arctap(t)...]
        if (ahead_.kind == TokKind::Punct && ahead_.text == "[") {
            advance();
            while (true) {
                if (ahead_.kind == TokKind::Ident && ahead_.text == "arctap") {
                    advance();
                    expectOpenParen();
                    int64_t at = expectInt("arctap 时间");
                    expectCloseParen();
                    a.arcTaps.push_back(ArcTap{(int32_t)at});
                } else {
                    issue("arctap 列表中预期 arctap");
                    break;
                }
                if (ahead_.kind == TokKind::Punct && ahead_.text == ",") {
                    advance();
                    continue;
                }
                break;
            }
            if (ahead_.kind == TokKind::Punct && ahead_.text == "]") {
                advance();
            } else {
                issue("arctap 列表未以 ']' 收束");
            }
        }

        expectSemi();
        if (tg) tg->arcs.push_back(a); else ch.arcs.push_back(a);
    }

    void parseTiming(Chart& ch, TimingGroup* tg) {
        advance(); // 'timing'
        expectOpenParen();
        int64_t t = expectInt("timing 时间");
        expectComma();
        double bpm = expectNumber("BPM");
        expectComma();
        double bpl = expectNumber("每行拍数");
        expectCloseParen();
        expectSemi();
        TimingEvent ev{(int32_t)t, bpm, bpl};
        if (tg) tg->timings.push_back(ev); // 组内独立变速，不进全局时轴
        else ch.timings.push_back(ev);
    }

    void parseCamera(Chart& ch) {
        advance(); // 'camera'
        expectOpenParen();
        int64_t t = expectInt("camera 时间");
        double x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
        expectComma();
        x = expectNumber("x"); expectComma();
        y = expectNumber("y"); expectComma();
        z = expectNumber("z"); expectComma();
        rx = expectNumber("rx"); expectComma();
        ry = expectNumber("ry"); expectComma();
        rz = expectNumber("rz"); expectComma();
        std::string easing = expectIdentOrNumber("camera 缓动");
        expectComma();
        int64_t dur = expectInt("camera 时长");
        expectCloseParen();
        expectSemi();
        ch.cameras.push_back(CameraEvent{(int32_t)t, (float)x, (float)y, (float)z,
                                         (float)rx, (float)ry, (float)rz, easing, (int32_t)dur});
    }

    void parseSceneControl(Chart& ch) {
        advance(); // 'scenecontrol'
        expectOpenParen();
        int64_t t = expectInt("scenecontrol 时间");
        expectComma();
        std::string type = expectIdentOrNumber("scenecontrol 类型");
        // 参数可选：存在 scenecontrol(t,type) 两参简写（如 trackhide）
        double fp = 0.0, ip = 0.0;
        if (ahead_.kind == TokKind::Punct && ahead_.text == ",") {
            advance();
            fp = expectNumber("scenecontrol 浮点参数");
            if (ahead_.kind == TokKind::Punct && ahead_.text == ",") {
                advance();
                ip = expectNumber("scenecontrol 整数参数");
                while (ahead_.kind == TokKind::Punct && ahead_.text == ",") {
                    advance();
                    expectIdentOrNumber("scenecontrol 附加参数");
                }
            }
        }
        expectCloseParen();
        expectSemi();
        ch.sceneControls.push_back(
            SceneControlEvent{(int32_t)t, type, (float)fp, (int32_t)std::llround(ip)});
    }

    void parseTimingGroup(Chart& ch) {
        advance(); // 'timinggroup'
        TimingGroup tg;
        // 可选 (flags)
        if (ahead_.kind == TokKind::Punct && ahead_.text == "(") {
            advance();
            if (!(ahead_.kind == TokKind::Punct && ahead_.text == ")")) {
                // 标志以 '_' 连接，也可能作为 ident 序列 + '_' 标点出现：
                // 词法器把 '_' 当 ident 字符，故 noinput_anglex75 是一个 ident
                std::string flags;
                while (true) {
                    if (ahead_.kind == TokKind::Ident) {
                        flags += ahead_.text;
                        advance();
                    } else if (ahead_.kind == TokKind::Punct && ahead_.text == "_") {
                        flags += "_";
                        advance();
                    } else if (ahead_.kind == TokKind::Number) {
                        flags += ahead_.text;
                        advance();
                    } else {
                        break;
                    }
                }
                applyGroupFlags(tg.flags, flags);
            }
            if (ahead_.kind == TokKind::Punct && ahead_.text == ")") advance();
            else issue("timinggroup 标志未以 ')' 收束");
        }
        if (!(ahead_.kind == TokKind::Punct && ahead_.text == "{")) {
            issue("timinggroup 应以 '{' 开始");
            skipStatement();
            ch.groups.push_back(std::move(tg));
            return;
        }
        advance();
        while (true) {
            if (ahead_.kind == TokKind::End) {
                issue("timinggroup 未闭合（EOF）");
                break;
            }
            if (ahead_.kind == TokKind::Punct && ahead_.text == "}") {
                advance();
                break;
            }
            parseStatement(ch, &tg);
        }
        expectSemi();
        ch.groups.push_back(std::move(tg));
    }

    static void applyGroupFlags(TimingGroupFlags& f, const std::string& raw) {
        size_t i = 0;
        const size_t n = raw.size();
        while (i < n) {
            if (raw.compare(i, 7, "noinput") == 0 && !followedByDigit(raw, i + 7)) {
                f.noInput = true; i += 7;
            } else if (raw.compare(i, 11, "fadingholds") == 0) {
                f.fadingHolds = true; i += 11;
            } else if (raw.compare(i, 7, "noclap_") == 0 || raw.compare(i, 6, "noclap") == 0) {
                f.noClap = true; i += (raw[i + 4] == 'l') ? 6 : 7; // 宽容
            } else if (raw.compare(i, 8, "noshadow") == 0) {
                f.noShadow = true; i += 8;
            } else if (raw.compare(i, 9, "noinput_e") == 0) {
                f.noInputEase = true; i += 9;
            } else if (raw.compare(i, 7, "anglex") == 0 || raw.compare(i, 7, "angley") == 0) {
                const bool isY = raw[i + 5] == 'y';
                size_t j = i + 6;
                size_t digitsStart = j;
                while (j < n && (std::isdigit((unsigned char)raw[j]))) j++;
                if (j > digitsStart) {
                    float deg = (float)std::atof(raw.substr(digitsStart, j - digitsStart).c_str()) / 10.f;
                    if (isY) f.angleY = deg; else f.angleX = deg;
                }
                i = j;
            } else {
                // 未知标志：跳一个字符避免死循环（语料外兼容位）
                i++;
            }
        }
    }

    static bool followedByDigit(const std::string& s, size_t pos) {
        return pos < s.size() && std::isdigit((unsigned char)s[pos]);
    }

    // ―― 基础原语 ――
    void advance() { ahead_ = lex_.next(); }

    bool checkEnd() { return ahead_.kind == TokKind::End; }

    void issue(const std::string& msg) {
        out_.ok = false;
        ParseIssue pi;
        pi.line = ahead_.line;
        pi.message = msg;
        out_.issues.push_back(pi);
    }

    void skipStatement() {
        int guard = 0;
        while (ahead_.kind != TokKind::End) {
            if (ahead_.kind == TokKind::Punct) {
                if (ahead_.text == ";" && guard == 0) { advance(); return; }
                if (ahead_.text == "{") guard++;
                if (ahead_.text == "}") {
                    // guard==0 的游离 '}' 也吞掉：保证错误恢复路径至少消费 1 个 token，
                    // 否则顶层循环会因 skipStatement 原地返回而死循环（随机字节实测触发）
                    if (guard == 0) { advance(); return; }
                    guard--;
                }
            }
            advance();
        }
    }

    void skipUntilDashOrEnd() {
        while (ahead_.kind != TokKind::End &&
               !(ahead_.kind == TokKind::Punct && ahead_.text == "-")) {
            advance();
        }
    }

    void expectOpenParen() {
        if (ahead_.kind == TokKind::Punct && ahead_.text == "(") advance();
        else issue("预期 '('");
    }
    void expectCloseParen() {
        if (ahead_.kind == TokKind::Punct && ahead_.text == ")") advance();
        else issue("预期 ')'");
    }
    void expectComma() {
        if (ahead_.kind == TokKind::Punct && ahead_.text == ",") advance();
        else issue("预期 ','");
    }
    void expectSemi() {
        if (ahead_.kind == TokKind::Punct && ahead_.text == ";") advance();
        else issue("预期 ';'");
    }

    // 值读取统一走 ahead_ 单一 token 流；负号在值位置与后续数码合并
    int64_t expectInt(const char* what) {
        return (int64_t)std::llround(expectNumber(what));
    }
    double expectNumber(const char* what) {
        bool neg = false;
        if (ahead_.kind == TokKind::Punct && (ahead_.text == "-" || ahead_.text == "+")) {
            neg = ahead_.text == "-";
            advance();
        }
        if (ahead_.kind != TokKind::Number) {
            issue(std::string("预期数值(") + what + ")");
            return 0.0;
        }
        double v = ahead_.num;
        advance();
        return neg ? -v : v;
    }
    std::string expectIdentOrNumber(const char* what) {
        if (ahead_.kind == TokKind::Ident || ahead_.kind == TokKind::Number) {
            std::string s = ahead_.text;
            advance();
            return s;
        }
        issue(std::string("预期标识符(") + what + ")");
        return {};
    }
    // isTrace supports true/false and extended identifier values as non-trace.
    bool expectTrace(const char* what) {
        if (ahead_.kind == TokKind::Ident &&
            (ahead_.text == "true" || ahead_.text == "false")) {
            bool v = ahead_.text == "true";
            advance();
            return v;
        }
        if (ahead_.kind == TokKind::Ident) {
            advance(); // 未知扩展值：吞掉不报错，视作 false
            return false;
        }
        issue(std::string("预期 true/false(") + what + ")");
        return false;
    }

    Lexer lex_;
    Token ahead_;
    ParseResult& out_;
};

} // namespace

size_t Chart::totalPlayableNotes() const {
    size_t n = taps.size() + holds.size();
    for (const auto& a : arcs) {
        if (!a.isTrace) n++;
        n += a.arcTaps.size();
    }
    for (const auto& g : groups) {
        if (g.flags.noInput) continue;
        n += g.taps.size() + g.holds.size();
        for (const auto& a : g.arcs) {
            if (!a.isTrace) n++;
            n += a.arcTaps.size();
        }
    }
    return n;
}

ParseResult parseChart(const std::string& text) {
    ParseResult out;
    Parser p(text, out);
    p.run();
    return out;
}

std::string readFileAsString(const std::string& utf8Path, bool& okOut) {
    std::ifstream f(utf8Path, std::ios::binary);
    if (!f) {
        okOut = false;
        return {};
    }
    okOut = true;
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace aff
