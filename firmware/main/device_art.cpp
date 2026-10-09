#include "device_art.hpp"

#include "background_decode.hpp"
#include "forms.hpp"
#include "local_form_art.hpp"
#include "local_form_facing.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/tjpgd.h"
#endif

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace digivice::device {
namespace {
struct Scene { const char* name; const char* id; const char* shortName; };
constexpr Scene scenes[]{
    {"meadow","scene-meadow-412-v1","MEADOW"}, {"forest","scene-forest-412-v1","FOREST"},
    {"beach","scene-beach-412-v1","BEACH"}, {"ruins","scene-ruins-412-v1","RUINS"},
    {"cavern","scene-cavern-412-v1","CAVERN"}, {"snow","scene-snow-412-v1","SNOW"},
    {"volcanic","scene-volcanic-412-v1","VOLCANIC"}, {"digital","scene-digital-412-v1","DIGITAL"},
};
int sceneIndex(const char* id) {
    if (id) for (unsigned i = 0; i < sizeof(scenes)/sizeof(scenes[0]); ++i)
        if (!std::strcmp(id,scenes[i].id)) return static_cast<int>(i);
    return -1;
}
struct File {
    int fd = -1;
    ~File() { if (fd >= 0) ::close(fd); }
};
struct Memory {
    void* data = nullptr;
    ~Memory() { heap_caps_free(data); }
};
constexpr auto psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr std::size_t backgroundBytes = assets::kBackgroundPixels*sizeof(std::uint16_t);

#if CONFIG_IDF_TARGET_ESP32S3
struct JpegContext {
    const std::uint8_t* data = nullptr;
    std::size_t bytes = 0, offset = 0, written = 0;
    std::uint16_t* output = nullptr;
    assets::JpegInfo info{};
    std::int64_t deadline = 0;
    bool failed = false;
};
UINT jpegInput(JDEC* decoder, BYTE* output, UINT bytes) {
    auto& context = *static_cast<JpegContext*>(decoder->device);
    if (context.failed || esp_timer_get_time() > context.deadline || context.offset > context.bytes) {
        context.failed = true; return 0;
    }
    // The last512-byte refill is normally short; report the actual count.
    const auto count = static_cast<UINT>(std::min(static_cast<std::size_t>(bytes),context.bytes-context.offset));
    if (output && count) std::memcpy(output,context.data+context.offset,count);
    context.offset += count;
    return count;
}
UINT jpegOutput(JDEC* decoder, void* rgb, JRECT* rectangle) {
    auto& context = *static_cast<JpegContext*>(decoder->device);
    if (context.failed || !rectangle || esp_timer_get_time() > context.deadline ||
        rectangle->right < rectangle->left || rectangle->bottom < rectangle->top) {
        context.failed = true; return 0;
    }
    const std::size_t bytes = static_cast<std::size_t>(rectangle->right-rectangle->left+1)*
        (rectangle->bottom-rectangle->top+1)*3;
    std::size_t written = 0;
    if (!assets::writeBackgroundBlock(context.info,rectangle->left,rectangle->top,rectangle->right,
        rectangle->bottom,static_cast<const std::uint8_t*>(rgb),bytes,context.output,assets::kBackgroundPixels,written) ||
        written > assets::kBackgroundPixels-context.written) {
        context.failed = true; return 0;
    }
    context.written += written;
    return 1;
}
bool decodeJpeg(const std::uint8_t* data, std::size_t bytes, std::uint16_t* output) {
    assets::JpegInfo info;
    if (!assets::inspectBackgroundJpeg(data,bytes,info)) return false;
    // ESP-IDF5.3.6 S3 ROM ABI emits RGB888 MCUs (JD_FORMAT=0), not RGB565.
    // 4KiB bounds the decoder pool; it rejects tables that cannot fit.
    Memory work{heap_caps_malloc(4096,MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)};
    if (!work.data) return false;
    JpegContext context{data,bytes,0,0,output,info,esp_timer_get_time()+2000000,false};
    JDEC decoder{};
    if (jd_prepare(&decoder,jpegInput,work.data,4096,&context) != JDR_OK ||
        decoder.width != info.width || decoder.height != info.height || context.failed) return false;
    std::memset(output,0,backgroundBytes);
    return jd_decomp(&decoder,jpegOutput,0) == JDR_OK && !context.failed &&
        context.written == assets::kBackgroundPixels;
}
#endif
}

std::size_t Art::allocatedBytes() const { return spriteBytes_ + (background_ ? backgroundBytes : 0); }
void Art::retry() { requestedForm_ = UINT32_MAX; requestedScene_ = -2; }

void Art::loadSprite(std::uint32_t formId) {
    requestedForm_ = formId; loadedForm_ = 0; spriteInfo_ = {};
    for (auto& bounds : spriteBounds_) bounds = {};
    heap_caps_free(spritePixels_); spritePixels_ = nullptr; spriteMasks_ = nullptr; spriteBytes_ = 0;
    if (!formId) { spriteDiagnostic_ = "no exact-form artwork requested"; return; }
    if (!forms::productionForm(formId)) {
        spriteDiagnostic_ = "test or unknown form artwork excluded"; return;
    }
#if defined(CONFIG_DIGIVICE_PRIVATE_SD_ART) && CONFIG_DIGIVICE_PRIVATE_SD_ART
    sprite::LocalFormArt source;
    auto result = source.open(formId); // Numeric root8.3 filename, whole DVA CRC validated.
    if (result != sprite::Result::Ok) { spriteDiagnostic_ = source.diagnostic(); return; }
    const auto info = source.info();
    const auto pixels = static_cast<std::size_t>(info.width)*info.height;
    const auto pixelBytes = pixels*sizeof(std::uint16_t)*info.frameCount;
    const auto maskBytes = pixels/8*info.frameCount;
    Memory decoded{heap_caps_malloc(pixelBytes+maskBytes,psram)};
    if (!decoded.data) { spriteDiagnostic_ = "exact-form PSRAM allocation failed"; return; }
    auto* values = static_cast<std::uint16_t*>(decoded.data);
    auto* masks = reinterpret_cast<std::uint8_t*>(decoded.data)+pixelBytes;
    for (unsigned animation = 0; animation < sprite::kAnimationCount; ++animation) {
        const auto clip = info.clips[animation];
        for (unsigned frame = 0; frame < clip.frames; ++frame) {
            const auto index = static_cast<std::size_t>(clip.firstFrame)+frame;
            result = source.decode(static_cast<sprite::Animation>(animation),frame,
                values+index*pixels,pixels,masks+index*(pixels/8),pixels/8);
            if (result != sprite::Result::Ok) {
                spriteDiagnostic_ = "exact-form decode failed; artwork unavailable"; return;
            }
        }
        if (!sprite::opaqueClipBounds(masks,maskBytes,info.width,info.height,
                                      info.frameCount,clip,spriteBounds_[animation])) {
            spriteDiagnostic_ = "exact-form bounds invalid; artwork unavailable"; return;
        }
    }
    source.close(); // No SD descriptor survives publication or a power barrier.
    spritePixels_ = values; spriteMasks_ = masks; spriteBytes_ = pixelBytes+maskBytes;
    spriteInfo_ = info; loadedForm_ = formId; decoded.data = nullptr;
    spriteDiagnostic_ = "local exact-form DVA verified and decoded";
#else
    spriteDiagnostic_ = "private SD artwork disabled in this build";
#endif
}

void Art::loadBackground(int scene) {
    requestedScene_ = scene; loadedScene_ = -1;
    if (scene < 0) { backgroundDiagnostic_ = "unknown/no scene; resident fallback"; return; }
#if CONFIG_IDF_TARGET_ESP32S3
    const auto& selected = scenes[scene];
    // Fixed allowlist, no scans or caller-supplied paths. Long paths require
    // FatFS LFN support; short root aliases work with an8.3-only card build.
    const char* patterns[]{"/sdcard/%s.JPG", "/sdcard/%s.jpg", "/sdcard/assets/device/%s.jpg",
        "/sdcard/assets/backgrounds/jpeg/%s-480.jpg", "/sdcard/%s-480.jpg"};
    const char* names[]{selected.shortName, selected.id, selected.id, selected.name, selected.name};
    for (unsigned candidate = 0; candidate < sizeof(patterns)/sizeof(patterns[0]); ++candidate) {
        char path[112]{};
        const auto length = std::snprintf(path,sizeof(path),patterns[candidate],names[candidate]);
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof(path)) continue;
        int flags = O_RDONLY;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        File file{::open(path,flags)};
        if (file.fd < 0) continue;
        struct stat status{};
        if (::fstat(file.fd,&status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 32 ||
            static_cast<std::uint64_t>(status.st_size) > assets::kMaximumJpegBytes) {
            backgroundDiagnostic_ = "local scene type/size invalid; resident fallback"; return;
        }
        const auto bytes = static_cast<std::size_t>(status.st_size);
        Memory encoded{heap_caps_malloc(bytes,psram)};
        if (!encoded.data) { backgroundDiagnostic_ = "scene input PSRAM allocation failed"; return; }
        auto* data = static_cast<std::uint8_t*>(encoded.data);
        std::size_t offset = 0;
        unsigned interruptions = 0;
        const auto deadline = esp_timer_get_time()+2000000;
        while (offset < bytes && esp_timer_get_time() <= deadline) {
            const auto count = ::read(file.fd,data+offset,std::min(std::size_t(4096),bytes-offset));
            if (count < 0 && errno == EINTR && interruptions++ < 3) continue;
            if (count <= 0) break;
            offset += static_cast<std::size_t>(count);
        }
        ::close(file.fd); file.fd = -1;
        if (offset != bytes) { backgroundDiagnostic_ = "scene read failed; resident fallback"; return; }
        if (!background_) background_ = static_cast<std::uint16_t*>(heap_caps_malloc(backgroundBytes,psram));
        if (!background_) { backgroundDiagnostic_ = "scene output PSRAM allocation failed"; return; }
        if (!decodeJpeg(data,bytes,background_)) {
            backgroundDiagnostic_ = "scene baseline JPEG decode failed; resident fallback"; return;
        }
        loadedScene_ = scene;
        backgroundDiagnostic_ = "local scene JPEG decoded; RGB565412px dimmed";
        return;
    }
    backgroundDiagnostic_ = "allowlisted local scene missing; resident fallback";
#else
    backgroundDiagnostic_ = "ROM scene decoder unavailable on this target";
#endif
}

deviceui::Artwork Art::prepare(const deviceui::ArtRequest& request, bool storageReady) {
    deviceui::Artwork artwork;
    if (paused_) return artwork;
    if (storageReady != previousStorageReady_) {
        previousStorageReady_ = storageReady; retry();
    }
    if (!storageReady) return artwork;
    const auto scene = sceneIndex(request.sceneId);
    loading_ = true;
    if (request.formId != requestedForm_) loadSprite(request.formId);
    if (scene != requestedScene_) loadBackground(scene);
    loading_ = false;
    if (loadedScene_ >= 0 && loadedScene_ == scene) {
        artwork.background = background_; artwork.backgroundPixels = assets::kBackgroundPixels;
        artwork.backgroundId = scenes[scene].id;
    }
    const auto animation = static_cast<unsigned>(request.animation);
    if (loadedForm_ && request.formId == loadedForm_ && animation < sprite::kAnimationCount) {
        const auto clip = spriteInfo_.clips[animation];
        const auto index = clip.firstFrame+(request.elapsedMs/clip.frameMs)%clip.frames;
        const auto pixels = static_cast<std::size_t>(spriteInfo_.width)*spriteInfo_.height;
        artwork.sprite.formId = loadedForm_; artwork.sprite.animation = request.animation;
        artwork.sprite.nativeFacing = sprite::localFormFacing(loadedForm_);
        artwork.sprite.pixels = spritePixels_+index*pixels; artwork.sprite.pixelCount = pixels;
        artwork.sprite.mask = spriteMasks_+index*(pixels/8); artwork.sprite.maskBytes = pixels/8;
        artwork.sprite.width = spriteInfo_.width; artwork.sprite.height = spriteInfo_.height;
        const auto bounds = spriteBounds_[animation];
        artwork.sprite.contentX = bounds.x; artwork.sprite.contentY = bounds.y;
        artwork.sprite.contentWidth = bounds.width; artwork.sprite.contentHeight = bounds.height;
    }
    return artwork;
}
} // namespace digivice::device
