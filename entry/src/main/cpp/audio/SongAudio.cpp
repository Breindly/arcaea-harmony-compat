// SongAudio.cpp —— WAV 解析 + OHAudio 写回调渲染 + 时钟实现
#include "audio/SongAudio.h"

#include <hilog/log.h>
#include <ctime>
#include <cstring>

namespace arc {
namespace {

int32_t OnWriteDataTramp(OH_AudioRenderer* renderer, void* userData, void* buffer,
                         int32_t length) {
    (void)renderer;
    return static_cast<SongAudio*>(userData)->onWriteData(buffer, length);
}

uint32_t rdU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t rdU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

} // namespace

bool SongAudio::loadWav(const uint8_t* data, size_t size) {
    unload();
    if (size < 44 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        OH_LOG_ERROR(LOG_APP, "loadWav: not RIFF/WAVE");
        return false;
    }
    size_t pos = 12;
    bool gotFmt = false, gotData = false;
    uint16_t fmt = 0, bits = 0;
    while (pos + 8 <= size) {
        uint32_t ck = rdU32(data + pos);
        uint32_t csz = rdU32(data + pos + 4);
        const uint8_t* body = data + pos + 8;
        if (ck == 0x20746D66 && csz >= 16) { // "fmt "
            fmt = rdU16(body);
            channels_ = rdU16(body + 2);
            sampleRate_ = (int32_t)rdU32(body + 4);
            bits = rdU16(body + 14);
            gotFmt = true;
        } else if (ck == 0x61746164) { // "data"
            size_t avail = size - (pos + 8);
            size_t bytes = csz < avail ? csz : avail;
            if (gotFmt && fmt == 1 && bits == 16) {
                pcm_.resize(bytes / 2);
                std::memcpy(pcm_.data(), body, pcm_.size() * 2);
                gotData = true;
            }
        }
        pos += 8 + csz + (csz & 1);
        if (gotFmt && gotData) break;
    }
    if (!gotFmt || !gotData || channels_ < 1 || sampleRate_ <= 0 || pcm_.empty()) {
        OH_LOG_ERROR(LOG_APP, "loadWav: unsupported (fmt=%{public}d bits=%{public}d)", fmt, bits);
        pcm_.clear();
        return false;
    }
    durationMs_ = (int32_t)((int64_t)(pcm_.size() / channels_) * 1000 / sampleRate_);
    OH_LOG_INFO(LOG_APP, "loadWav ok: %{public}ch %{public}Hz %{public}dms", channels_, sampleRate_,
                durationMs_);
    return true;
}

void SongAudio::unload() {
    stop();
    pcm_.clear();
    channels_ = 0;
    sampleRate_ = 0;
    durationMs_ = 0;
    if (builder_) {
        OH_AudioStreamBuilder_Destroy(builder_);
        builder_ = nullptr;
    }
}

bool SongAudio::start() {
    if (!armed() || playing_.load()) return playing_.load();

    OH_AudioStreamBuilder* builder = nullptr;
    if (OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER) != AUDIOSTREAM_SUCCESS ||
        OH_AudioStreamBuilder_SetSamplingRate(builder, sampleRate_) != AUDIOSTREAM_SUCCESS ||
        OH_AudioStreamBuilder_SetChannelCount(builder, channels_) != AUDIOSTREAM_SUCCESS ||
        OH_AudioStreamBuilder_SetSampleFormat(builder, AUDIOSTREAM_SAMPLE_S16LE) !=
            AUDIOSTREAM_SUCCESS ||
        OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW) !=
            AUDIOSTREAM_SUCCESS) {
        OH_LOG_ERROR(LOG_APP, "audio builder setup failed");
        OH_AudioStreamBuilder_Destroy(builder);
        return false;
    }
    OH_AudioRenderer_Callbacks cb{};
    cb.OH_AudioRenderer_OnWriteData = OnWriteDataTramp;
    OH_AudioStreamBuilder_SetRendererCallback(builder, cb, this);

    OH_AudioRenderer* renderer = nullptr;
    if (OH_AudioStreamBuilder_GenerateRenderer(builder, &renderer) != AUDIOSTREAM_SUCCESS ||
        renderer == nullptr) {
        OH_LOG_ERROR(LOG_APP, "GenerateRenderer failed");
        OH_AudioStreamBuilder_Destroy(builder);
        return false;
    }
    framesFed_.store(0);
    builder_ = builder;
    renderer_ = renderer;
    playing_.store(true);
    if (OH_AudioRenderer_Start(renderer) != AUDIOSTREAM_SUCCESS) {
        OH_LOG_ERROR(LOG_APP, "audio start failed");
        playing_.store(false);
        OH_AudioRenderer_Release(renderer_);
        renderer_ = nullptr;
        OH_AudioStreamBuilder_Destroy(builder_);
        builder_ = nullptr;
        return false;
    }
    return true;
}

void SongAudio::stop() {
    playing_.store(false);
    if (renderer_) {
        OH_AudioRenderer_Stop(renderer_);
        OH_AudioRenderer_Release(renderer_);
        renderer_ = nullptr;
    }
    if (builder_) {
        OH_AudioStreamBuilder_Destroy(builder_);
        builder_ = nullptr;
    }
}

int32_t SongAudio::onWriteData(void* buffer, int32_t frameCount) {
    if (!playing_.load()) {
        std::memset(buffer, 0, (size_t)frameCount * channels_ * sizeof(int16_t));
        return 0;
    }
    int64_t totalFrames = (int64_t)(pcm_.size() / channels_);
    int64_t pos = framesFed_.load();
    int32_t toFill = frameCount;
    int16_t* out = (int16_t*)buffer;
    if (pos >= totalFrames) { // 播完 → 静音（循环由渲染层控制重启）
        std::memset(out, 0, (size_t)frameCount * channels_ * sizeof(int16_t));
        return 0;
    }
    int64_t avail = totalFrames - pos;
    if (avail < toFill) toFill = (int32_t)avail;
    std::memcpy(out, pcm_.data() + pos * channels_, (size_t)toFill * channels_ * sizeof(int16_t));
    if (toFill < frameCount) {
        std::memset(out + (size_t)toFill * channels_, 0,
                    (size_t)(frameCount - toFill) * channels_ * sizeof(int16_t));
    }
    framesFed_.store(pos + toFill);
    return 0;
}

int32_t SongAudio::positionMs() const {
    if (!playing_.load()) return 0;
    // 首选：已播时间戳（更贴近实际出声位置）
    int64_t frameIndex = 0, tsNs = 0;
    if (renderer_ != nullptr &&
        OH_AudioRenderer_GetTimestamp(renderer_, CLOCK_MONOTONIC, &frameIndex, &tsNs) ==
            AUDIOSTREAM_SUCCESS) {
        return (int32_t)(frameIndex * 1000 / sampleRate_);
    }
    return (int32_t)(framesFed_.load() * 1000 / sampleRate_);
}

} // namespace arc
