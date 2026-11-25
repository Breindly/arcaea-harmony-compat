// napi_init.cpp —— ArkTS ⇄ C++ 桥
// 阶段5：全量曲库 / AVPlayer 音频 / 播放状态机 / 选曲与结算
#include "aff/AffParser.h"
#include "aff/TimingModel.h"
#include "renderer/ArcRenderer.h"

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <hilog/log.h>
#include <memory>
#include <string>

#include "napi/native_api.h"

namespace {

arc::ArcRenderer g_renderer;

std::string GetStringArg(napi_env env, napi_value value) {
    size_t len = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &len) != napi_ok) return "";
    std::string text(len + 1, '\0');
    if (napi_get_value_string_utf8(env, value, text.data(), len + 1, &len) != napi_ok) return "";
    text.resize(len);
    return text;
}

napi_value MakeBool(napi_env env, bool v) {
    napi_value out;
    napi_get_boolean(env, v, &out);
    return out;
}

napi_value ParseChart(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string text = GetStringArg(env, args[0]);

    aff::ParseResult r = aff::parseChart(text);

    napi_value obj;
    napi_create_object(env, &obj);
    napi_value v;
    napi_get_boolean(env, r.ok, &v);
    napi_set_named_property(env, obj, "ok", v);

    napi_create_int32(env, r.chart.meta.audioOffset, &v);
    napi_set_named_property(env, obj, "audioOffset", v);
    napi_create_uint32(env, static_cast<uint32_t>(r.chart.totalPlayableNotes()), &v);
    napi_set_named_property(env, obj, "totalPlayableNotes", v);
    napi_create_uint32(env, static_cast<uint32_t>(r.issues.size()), &v);
    napi_set_named_property(env, obj, "issueCount", v);

    auto putCount = [&](const char* name, size_t n) {
        napi_value c;
        napi_create_uint32(env, static_cast<uint32_t>(n), &c);
        napi_set_named_property(env, obj, name, c);
    };
    size_t taps = r.chart.taps.size(), holds = r.chart.holds.size(),
           arcs = r.chart.arcs.size(), arctaps = 0, timings = r.chart.timings.size();
    for (auto& a : r.chart.arcs) arctaps += a.arcTaps.size();
    for (auto& g : r.chart.groups) {
        taps += g.taps.size();
        holds += g.holds.size();
        arcs += g.arcs.size();
        timings += g.timings.size();
        for (auto& a : g.arcs) arctaps += a.arcTaps.size();
    }
    putCount("taps", taps);
    putCount("holds", holds);
    putCount("arcs", arcs);
    putCount("arcTaps", arctaps);
    putCount("timings", timings);
    putCount("cameras", r.chart.cameras.size());
    putCount("sceneControls", r.chart.sceneControls.size());
    putCount("timingGroups", r.chart.groups.size());

    aff::TimingModel tm(r.chart.timings);
    const aff::TimingEvent* seg = tm.segmentAt(0);
    napi_value bpm0;
    napi_create_double(env, seg ? seg->bpm : 0.0, &bpm0);
    napi_set_named_property(env, obj, "startBpm", bpm0);
    return obj;
}

napi_value CoreVersion(napi_env env, napi_callback_info /*info*/) {
    napi_value v;
    napi_create_string_utf8(env, "arc-core 0.3.0 (library + AVPlayer + play state)",
                            NAPI_AUTO_LENGTH, &v);
    return v;
}

// prepareSong(chartText, audioPath, gaugeKind?, chartConstant?) -> { ok, error }
napi_value PrepareSong(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4] = {nullptr, nullptr, nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string chartText = GetStringArg(env, args[0]);
    std::string audioPath = GetStringArg(env, args[1]);
    int32_t gaugeKind = 0;
    double chartConstant = 0.0;
    if (argc > 2 && args[2] != nullptr) {
        napi_get_value_int32(env, args[2], &gaugeKind);
    }
    if (argc > 3 && args[3] != nullptr) {
        napi_get_value_double(env, args[3], &chartConstant);
    }

    auto r = std::make_shared<aff::ParseResult>(aff::parseChart(chartText));
    bool ok = g_renderer.prepare(r, audioPath, gaugeKind, (float)chartConstant);
    arc::ArcRenderer::PlayState st = g_renderer.state();

    napi_value obj;
    napi_create_object(env, &obj);
    napi_set_named_property(env, obj, "ok", MakeBool(env, ok));
    napi_value err;
    napi_create_string_utf8(env, st.error.c_str(), NAPI_AUTO_LENGTH, &err);
    napi_set_named_property(env, obj, "error", err);
    return obj;
}

napi_value PlaySong(napi_env env, napi_callback_info /*info*/) {
    g_renderer.play();
    return MakeBool(env, true);
}

napi_value PauseSong(napi_env env, napi_callback_info /*info*/) {
    g_renderer.pause();
    return MakeBool(env, true);
}

napi_value RestartSong(napi_env env, napi_callback_info /*info*/) {
    g_renderer.restart();
    return MakeBool(env, true);
}

napi_value StopSong(napi_env env, napi_callback_info /*info*/) {
    g_renderer.clear();
    return MakeBool(env, true);
}

napi_value SetUserOffset(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int32_t ms = 0;
    if (napi_get_value_int32(env, args[0], &ms) == napi_ok) {
        g_renderer.setUserOffset(ms);
    }
    return MakeBool(env, true);
}

napi_value SetBackground(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr, nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    void* data = nullptr;
    size_t len = 0;
    if (napi_get_arraybuffer_info(env, args[0], &data, &len) != napi_ok || data == nullptr) {
        return MakeBool(env, false);
    }
    int32_t w = 0, h = 0;
    if (napi_get_value_int32(env, args[1], &w) != napi_ok ||
        napi_get_value_int32(env, args[2], &h) != napi_ok) {
        return MakeBool(env, false);
    }
    g_renderer.setBackground(data, len, w, h);
    return MakeBool(env, true);
}

napi_value ClearBackground(napi_env env, napi_callback_info /*info*/) {
    g_renderer.clearBackground();
    return MakeBool(env, true);
}

napi_value SetNoteSpeed(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    double speed = 3.0;
    if (napi_get_value_double(env, args[0], &speed) == napi_ok) {
        g_renderer.setNoteSpeed((float)speed);
    }
    return MakeBool(env, true);
}

napi_value GetPlayState(napi_env env, napi_callback_info /*info*/) {
    arc::ArcRenderer::PlayState s = g_renderer.state();
    napi_value obj;
    napi_create_object(env, &obj);
    auto putI32 = [&](const char* k, int32_t v) {
        napi_value nv;
        napi_create_int32(env, v, &nv);
        napi_set_named_property(env, obj, k, nv);
    };
    auto putU32 = [&](const char* k, uint32_t v) {
        napi_value nv;
        napi_create_uint32(env, v, &nv);
        napi_set_named_property(env, obj, k, nv);
    };
    putI32("phase", s.phase);
    putI32("time", s.time);
    putI32("duration", s.duration);
    putI32("score", s.score);
    napi_value gaugeFVal;
    napi_create_double(env, (double)s.gaugeF, &gaugeFVal);
    napi_set_named_property(env, obj, "gaugeF", gaugeFVal);
    putI32("gauge", s.gauge);
    putI32("surfaceW", s.surfaceW);
    putI32("surfaceH", s.surfaceH);
    napi_value lostFlag;
    napi_get_boolean(env, s.trackLost, &lostFlag);
    napi_set_named_property(env, obj, "trackLost", lostFlag);
    putU32("combo", s.combo);
    putU32("maxCombo", s.maxCombo);
    putU32("pure", s.pure);
    putU32("far", s.far);
    putU32("lost", s.lost);
    putU32("total", s.total);
    putU32("judgeSerial", s.judgeSerial);
    putI32("lastGrade", s.lastGrade);
    putI32("lastDelta", s.lastDelta);
    napi_value ready;
    napi_get_boolean(env, s.audioReady, &ready);
    napi_set_named_property(env, obj, "audioReady", ready);
    napi_value rendererReady;
    napi_get_boolean(env, s.rendererReady, &rendererReady);
    napi_set_named_property(env, obj, "rendererReady", rendererReady);
    napi_value err;
    napi_create_string_utf8(env, s.error.c_str(), NAPI_AUTO_LENGTH, &err);
    napi_set_named_property(env, obj, "error", err);
    return obj;
}

// ――― XComponent 生命周期 ―――

void OnSurfaceCreatedCB(OH_NativeXComponent* component, void* window) {
    uint64_t w = 0, h = 0;
    if (component) {
        OH_NativeXComponent_GetXComponentSize(component, window, &w, &h);
    }
    if (!g_renderer.start(window, (int)w, (int)h)) {
        OH_LOG_ERROR(LOG_APP, "renderer start failed");
    } else {
        OH_LOG_ERROR(LOG_APP, "renderer started %{public}d x %{public}d", (int)w, (int)h);
    }
}

void OnSurfaceChangedCB(OH_NativeXComponent* /*component*/, void* /*window*/) {}

void OnSurfaceDestroyedCB(OH_NativeXComponent* /*component*/, void* /*window*/) {
    g_renderer.stop();
}

void OnDispatchTouchEventCB(OH_NativeXComponent* component, void* window) {
    if (component == nullptr || window == nullptr) return;
    OH_NativeXComponent_TouchEvent ev;
    if (OH_NativeXComponent_GetTouchEvent(component, window, &ev) != 0) return;
    uint64_t w = 0, h = 0;
    if (OH_NativeXComponent_GetXComponentSize(component, window, &w, &h) != 0 || w == 0 || h == 0)
        return;
    for (uint32_t i = 0; i < ev.numPoints && i < OH_NATIVE_XCOMPONENT_MAX_TOUCH_POINTS_NUMBER;
         i++) {
        const OH_NativeXComponent_TouchPoint& p = ev.touchPoints[i];
        const float x = p.x / (float)w;
        const float y = p.y / (float)h;
        if (p.type == OH_NATIVEXCOMPONENT_DOWN) {
            g_renderer.onTouchDown(p.id, x, y);
        } else if (p.type == OH_NATIVEXCOMPONENT_MOVE) {
            g_renderer.onTouchMove(p.id, x, y);
        } else if (p.type == OH_NATIVEXCOMPONENT_UP ||
                   p.type == OH_NATIVEXCOMPONENT_CANCEL) {
            g_renderer.onTouchUp(p.id, x, y);
        }
    }
}

} // namespace

extern "C" napi_value InitModule(napi_env env, napi_value exports);

static napi_module g_arcclientModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = InitModule,
    .nm_modname = "arcclient",
    .nm_priv = nullptr,
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterArcclientModule(void) {
    napi_module_register(&g_arcclientModule);
}

extern "C" __attribute__((visibility("default"))) napi_value InitModule(napi_env env,
                                                                        napi_value exports) {
    // XComponent 注册（ArkTS 侧 libraryname='arcclient' + id='playfield'）
    napi_value exportInstance = nullptr;
    OH_NativeXComponent* nativeXComponent = nullptr;
    bool hasXc = false;
    if (napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance) ==
            napi_ok &&
        napi_unwrap(env, exportInstance, reinterpret_cast<void**>(&nativeXComponent)) == napi_ok &&
        nativeXComponent != nullptr) {
        hasXc = true;
    }
    if (hasXc) {
        static OH_NativeXComponent_Callback cb;
        cb.OnSurfaceCreated = OnSurfaceCreatedCB;
        cb.OnSurfaceChanged = OnSurfaceChangedCB;
        cb.OnSurfaceDestroyed = OnSurfaceDestroyedCB;
        cb.DispatchTouchEvent = OnDispatchTouchEventCB;
        OH_NativeXComponent_RegisterCallback(nativeXComponent, &cb);
    }

    napi_property_descriptor desc[] = {
        {"parseChart", nullptr, ParseChart, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"coreVersion", nullptr, CoreVersion, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"prepareSong", nullptr, PrepareSong, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"play", nullptr, PlaySong, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"pause", nullptr, PauseSong, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"restart", nullptr, RestartSong, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stop", nullptr, StopSong, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setUserOffset", nullptr, SetUserOffset, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setNoteSpeed", nullptr, SetNoteSpeed, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setBackground", nullptr, SetBackground, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"clearBackground", nullptr, ClearBackground, nullptr, nullptr, nullptr, napi_default,
         nullptr},
        {"getPlayState", nullptr, GetPlayState, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    size_t count = sizeof(desc) / sizeof(desc[0]);
    napi_define_properties(env, exports, count, desc);
    return exports;
}
