// SongPlayer.h —— 基于系统 AVPlayer 的曲目播放（OGG/WAV 等，无需解码到 PCM）
// 支持异步 prepare、播放/暂停/seek、完成回调与毫秒级播放位置查询。
#pragma once

#include <multimedia/player_framework/avplayer.h>
#include <multimedia/player_framework/avplayer_base.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace arc {

class SongPlayer {
public:
    ~SongPlayer();

    // 打开文件并异步 Prepare。成功后 prepared() 变 true（回调或轮询）。
    bool loadFile(const std::string& path);
    void unload();

    bool play();
    void pause();
    bool seekTo(int32_t ms);
    void stop();

    bool prepared() const { return prepared_.load(); }
    bool playing() const { return playing_.load(); }
    bool completed() const { return completed_.load(); }
    bool failed() const { return failed_.load(); }
    std::string error() const;

    int32_t positionMs() const;
    int32_t durationMs() const;
    int32_t playerState() const;

private:
    static void OnInfo(OH_AVPlayer* player, AVPlayerOnInfoType type, OH_AVFormat* infoBody,
                       void* userData);
    static void OnError(OH_AVPlayer* player, int32_t errorCode, const char* errorMsg,
                        void* userData);

    void handleInfo(AVPlayerOnInfoType type);
    void handleError(int32_t code, const char* msg);
    void closeFd();

    OH_AVPlayer* player_ = nullptr;
    int fd_ = -1;
    int64_t fileSize_ = 0;
    mutable std::mutex mtx_;
    std::string error_;

    std::atomic<bool> prepared_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> completed_{false};
    std::atomic<bool> failed_{false};
    mutable std::atomic<int32_t> durationMs_{0};
};

} // namespace arc
