# entry 原生层：NAPI 胶水 + 可移植 aff 核心 + 渲染器
cmake_minimum_required(VERSION 3.16)
project(entry)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH})

# Portable core static library: entry/src/main/cpp -> repository root/core
add_subdirectory(${NATIVERENDER_ROOT_PATH}/../../../../core arc_core_bin)

# 目标名必须与 ArkTS 侧 import 的 'libarcclient.so' 一致（产出 libarcclient.so）
add_library(arcclient SHARED
    napi_init.cpp
    renderer/ArcRenderer.cpp
    audio/SongPlayer.cpp
)
target_link_libraries(arcclient PUBLIC arc_core)
target_compile_features(arcclient PRIVATE cxx_std_17)

target_link_libraries(arcclient PUBLIC
    libace_napi.z.so
    libace_ndk.z.so
    libEGL.so
    libGLESv3.so
    libhilog_ndk.z.so
    libavplayer.so
    libnative_media_core.so
)
