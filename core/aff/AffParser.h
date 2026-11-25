// AffParser.h —— .aff 解析器（词法 + 递归下降，对照原生层管线：
// 文件入口 FUN_00d7e484 → 词法 FUN_016c3d78 → 分发 FUN_0135d048 → 各 handler）
#pragma once

#include "AffTypes.h"

#include <string>

namespace aff {

// 解析整张谱面。text 为文件全文（容忍 CRLF/BOM/缩进/多余空行）。
// 严格模式：遇无法识别的语句记 issue 并跳过该行，保证全语料可跑完。
ParseResult parseChart(const std::string& text);

// 便捷：整文件读入（桌面自测用；OHOS 侧由资源管理器喂 buffer）
std::string readFileAsString(const std::string& utf8Path, bool& okOut);

} // namespace aff
