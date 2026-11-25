// SongAudio.h —— 曲目音频（阶段3）：WAV(PCM16) + OHAudio 原生渲染 + 音频时钟
// 数据流：ArkTS 读 rawfile WAV → NAPI loadSong(buffer) → 回调填充 PCM。
// 时钟：优先 OH_AudioRenderer_GetTimestamp 已播帧号，取不到退回已喂帧数估计。
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostreambuilder.h>

namespace arc {

class SongAudio {
public:
    ~SongAudio() { unload(); }

    // 解析 RIFF/WAV（PCM16）。返回 false = 非法数据。
    bool loadWav(const uint8_t* data, size_t size);
    void unload();

    bool start();   // 创建 renderer 并开声
    void stop();    // 停声（可再次 start 从头播）
    bool armed() const { return !pcm_.empty(); }
    bool playing() const { return playing_.load(); }

    // 音频时钟：已播位置（毫秒，相对曲目开头）
    int32_t positionMs() const;
    int32_t durationMs() const { return durationMs_; }
    int32_t sampleRate() const { return sampleRate_; }

    // 写回调（OHAudio 调用）
    int32_t onWriteData(void* buffer, int32_t frameCount);

private:
    std::vector<int16_t> pcm_; // 交织声道
    int32_t channels_ = 0;
    int32_t sampleRate_ = 0;
    int32_t durationMs_ = 0;

    OH_AudioStreamBuilder* builder_ = nullptr;
    OH_AudioRenderer* renderer_ = nullptr;

    std::atomic<bool> playing_{false};
    std::atomic<int64_t> framesFed_{0}; // 已喂帧（回调计数）
};

} // namespace arc
