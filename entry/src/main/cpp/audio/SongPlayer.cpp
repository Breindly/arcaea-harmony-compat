// SongPlayer.cpp —— AVPlayer 封装（OGG/WAV 等格式由系统解码）
#include "audio/SongPlayer.h"

#include <hilog/log.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

namespace arc {

SongPlayer::~SongPlayer() {
    unload();
}

void SongPlayer::OnInfo(OH_AVPlayer* player, AVPlayerOnInfoType type, OH_AVFormat* /*infoBody*/,
                        void* userData) {
    (void)player;
    if (userData == nullptr) return;
    static_cast<SongPlayer*>(userData)->handleInfo(type);
}

void SongPlayer::OnError(OH_AVPlayer* /*player*/, int32_t errorCode, const char* errorMsg,
                         void* userData) {
    if (userData == nullptr) return;
    static_cast<SongPlayer*>(userData)->handleError(errorCode, errorMsg);
}

void SongPlayer::handleInfo(AVPlayerOnInfoType type) {
    if (type == AV_INFO_TYPE_EOS) {
        completed_.store(true);
        playing_.store(false);
        return;
    }
    if (type != AV_INFO_TYPE_STATE_CHANGE) return;

    AVPlayerState st = AV_IDLE;
    OH_AVPlayer* p = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        p = player_;
    }
    if (p == nullptr) return;
    if (OH_AVPlayer_GetState(p, &st) != AV_ERR_OK) return;

    switch (st) {
        case AV_PREPARED: {
            prepared_.store(true);
            int32_t dur = 0;
            if (OH_AVPlayer_GetDuration(p, &dur) == AV_ERR_OK && dur > 0) {
                durationMs_.store(dur);
            }
            OH_LOG_INFO(LOG_APP, "SongPlayer prepared, duration=%{public}d", dur);
            break;
        }
        case AV_PLAYING:
            playing_.store(true);
            break;
        case AV_PAUSED:
        case AV_STOPPED:
            playing_.store(false);
            break;
        case AV_COMPLETED:
            completed_.store(true);
            playing_.store(false);
            break;
        case AV_ERROR:
            failed_.store(true);
            playing_.store(false);
            break;
        default:
            break;
    }
}

void SongPlayer::handleError(int32_t code, const char* msg) {
    failed_.store(true);
    playing_.store(false);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        error_ = msg != nullptr ? msg : "AVPlayer error";
    }
    OH_LOG_ERROR(LOG_APP, "SongPlayer error %{public}d: %{public}s", code,
                 msg != nullptr ? msg : "");
}

std::string SongPlayer::error() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return error_;
}

bool SongPlayer::loadFile(const std::string& path) {
    unload();
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        std::lock_guard<std::mutex> lk(mtx_);
        error_ = "cannot open audio: " + path;
        failed_.store(true);
        OH_LOG_ERROR(LOG_APP, "SongPlayer open failed: %{public}s", path.c_str());
        return false;
    }
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        std::lock_guard<std::mutex> lk(mtx_);
        error_ = "bad audio file";
        failed_.store(true);
        return false;
    }

    OH_AVPlayer* p = OH_AVPlayer_Create();
    if (p == nullptr) {
        close(fd);
        std::lock_guard<std::mutex> lk(mtx_);
        error_ = "AVPlayer create failed";
        failed_.store(true);
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        player_ = p;
        fd_ = fd;
        fileSize_ = st.st_size;
        error_.clear();
    }
    prepared_.store(false);
    playing_.store(false);
    completed_.store(false);
    failed_.store(false);
    durationMs_.store(0);

    OH_AVPlayer_SetOnInfoCallback(p, &SongPlayer::OnInfo, this);
    OH_AVPlayer_SetOnErrorCallback(p, &SongPlayer::OnError, this);

    OH_AVErrCode ret = OH_AVPlayer_SetFDSource(p, fd, 0, st.st_size);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "SetFDSource failed: %{public}d", (int)ret);
        unload();
        std::lock_guard<std::mutex> lk(mtx_);
        error_ = "SetFDSource failed";
        failed_.store(true);
        return false;
    }
    ret = OH_AVPlayer_Prepare(p);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Prepare failed: %{public}d", (int)ret);
        unload();
        std::lock_guard<std::mutex> lk(mtx_);
        error_ = "Prepare failed";
        failed_.store(true);
        return false;
    }
    OH_LOG_INFO(LOG_APP, "SongPlayer loading %{public}s (%{public}lld bytes)", path.c_str(),
                (long long)st.st_size);
    return true;
}

void SongPlayer::unload() {
    OH_AVPlayer* p = nullptr;
    int fd = -1;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        p = player_;
        player_ = nullptr;
        fd = fd_;
        fd_ = -1;
        fileSize_ = 0;
    }
    if (p != nullptr) {
        OH_AVPlayer_Stop(p);
        OH_AVPlayer_ReleaseSync(p);
    }
    if (fd >= 0) close(fd);
    prepared_.store(false);
    playing_.store(false);
    completed_.store(false);
    failed_.store(false);
    durationMs_.store(0);
}

bool SongPlayer::play() {
    OH_AVPlayer* p = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        p = player_;
    }
    if (p == nullptr) return false;
    if (completed_.load()) {
        OH_AVPlayer_Seek(p, 0, AV_SEEK_CLOSEST);
        completed_.store(false);
    }
    if (!prepared_.load()) return false;
    OH_AVErrCode ret = OH_AVPlayer_Play(p);
    if (ret != AV_ERR_OK) {
        OH_LOG_ERROR(LOG_APP, "Play failed: %{public}d", (int)ret);
        return false;
    }
    playing_.store(true);
    return true;
}

void SongPlayer::pause() {
    OH_AVPlayer* p = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        p = player_;
    }
    if (p == nullptr) return;
    OH_AVPlayer_Pause(p);
    playing_.store(false);
}

bool SongPlayer::seekTo(int32_t ms) {
    OH_AVPlayer* p = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        p = player_;
    }
    if (p == nullptr) return false;
    if (ms < 0) ms = 0;
    OH_AVErrCode ret = OH_AVPlayer_Seek(p, ms, AV_SEEK_CLOSEST);
    completed_.store(false);
    return ret == AV_ERR_OK;
}

void SongPlayer::stop() {
    OH_AVPlayer* p = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        p = player_;
    }
    if (p == nullptr) return;
    OH_AVPlayer_Stop(p);
    playing_.store(false);
}

int32_t SongPlayer::positionMs() const {
    OH_AVPlayer* p = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        p = player_;
    }
    if (p == nullptr) return 0;
    if (completed_.load()) {
        int32_t dur = durationMs_.load();
        return dur > 0 ? dur : 0;
    }
    int32_t pos = 0;
    if (OH_AVPlayer_GetCurrentTime(p, &pos) != AV_ERR_OK || pos < 0) return 0;
    return pos;
}

int32_t SongPlayer::durationMs() const {
    int32_t cached = durationMs_.load();
    if (cached > 0) return cached;
    OH_AVPlayer* p = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        p = player_;
    }
    if (p == nullptr) return 0;
    int32_t dur = 0;
    if (OH_AVPlayer_GetDuration(p, &dur) == AV_ERR_OK && dur > 0) {
        durationMs_.store(dur);
        return dur;
    }
    return 0;
}

int32_t SongPlayer::playerState() const {
    OH_AVPlayer* p = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        p = player_;
    }
    if (p == nullptr) return -1;
    AVPlayerState st = AV_IDLE;
    if (OH_AVPlayer_GetState(p, &st) != AV_ERR_OK) return -1;
    return (int32_t)st;
}

} // namespace arc
