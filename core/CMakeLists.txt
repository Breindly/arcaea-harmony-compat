# core 静态库：可移植 .aff 解析 + 时轴模型（桌面自测与 OHOS NDK 共用）
cmake_minimum_required(VERSION 3.16)
project(arc_core LANGUAGES CXX)

add_library(arc_core STATIC
    aff/AffParser.cpp
)
target_include_directories(arc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(arc_core PUBLIC cxx_std_17)
