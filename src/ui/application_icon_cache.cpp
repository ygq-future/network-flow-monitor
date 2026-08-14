#include "network_flow_monitor/ui/application_icon_cache.h"

#include <algorithm>
#include <limits>
#include <shellapi.h>
#include <string_view>

namespace nfm::ui {
namespace {

class UniqueBitmap final {
  public:
    explicit UniqueBitmap(HBITMAP value) noexcept : value_{value} {}
    ~UniqueBitmap() {
        if (value_ != nullptr) {
            static_cast<void>(::DeleteObject(value_));
        }
    }
    UniqueBitmap(const UniqueBitmap&) = delete;
    UniqueBitmap& operator=(const UniqueBitmap&) = delete;
    UniqueBitmap(UniqueBitmap&&) = delete;
    UniqueBitmap& operator=(UniqueBitmap&&) = delete;
    [[nodiscard]] HBITMAP get() const noexcept {
        return value_;
    }

  private:
    HBITMAP value_{};
};

class UniqueMemoryDc final {
  public:
    explicit UniqueMemoryDc(HDC value) noexcept : value_{value} {}
    ~UniqueMemoryDc() {
        if (value_ != nullptr) {
            static_cast<void>(::DeleteDC(value_));
        }
    }
    UniqueMemoryDc(const UniqueMemoryDc&) = delete;
    UniqueMemoryDc& operator=(const UniqueMemoryDc&) = delete;
    UniqueMemoryDc(UniqueMemoryDc&&) = delete;
    UniqueMemoryDc& operator=(UniqueMemoryDc&&) = delete;
    [[nodiscard]] HDC get() const noexcept {
        return value_;
    }

  private:
    HDC value_{};
};

class UniqueIcon final {
  public:
    explicit UniqueIcon(HICON value) noexcept : value_{value} {}
    ~UniqueIcon() {
        if (value_ != nullptr) {
            static_cast<void>(::DestroyIcon(value_));
        }
    }
    UniqueIcon(const UniqueIcon&) = delete;
    UniqueIcon& operator=(const UniqueIcon&) = delete;
    UniqueIcon(UniqueIcon&&) = delete;
    UniqueIcon& operator=(UniqueIcon&&) = delete;
    [[nodiscard]] HICON get() const noexcept {
        return value_;
    }

  private:
    HICON value_{};
};

class ScopedSelection final {
  public:
    ScopedSelection(HDC context, HGDIOBJ value) noexcept
        : context_{context}, previous_{::SelectObject(context, value)} {}
    ~ScopedSelection() {
        if (context_ != nullptr && previous_ != nullptr && previous_ != HGDI_ERROR) {
            static_cast<void>(::SelectObject(context_, previous_));
        }
    }
    ScopedSelection(const ScopedSelection&) = delete;
    ScopedSelection& operator=(const ScopedSelection&) = delete;
    ScopedSelection(ScopedSelection&&) = delete;
    ScopedSelection& operator=(ScopedSelection&&) = delete;

  private:
    HDC context_{};
    HGDIOBJ previous_{};
};

[[nodiscard]] std::shared_ptr<const ApplicationIconPixels> MakeFallback() {
    auto icon = std::make_shared<ApplicationIconPixels>();
    for (std::uint32_t y = 0; y < kApplicationIconSize; ++y) {
        for (std::uint32_t x = 0; x < kApplicationIconSize; ++x) {
            const auto dx = static_cast<int>(x) - 8;
            const auto dy = static_cast<int>(y) - 8;
            if ((dx * dx) + (dy * dy) <= 49) {
                icon->bgra.at((y * kApplicationIconSize) + x) = 0xFFEB6325U;
            }
        }
    }
    return icon;
}

[[nodiscard]] std::wstring WidePath(std::string_view value) {
    if (value.empty() || value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }
    const std::string input{value};
    const auto size = static_cast<int>(value.size());
    const auto required = ::MultiByteToWideChar(CP_UTF8, 0, input.c_str(), size, nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    return ::MultiByteToWideChar(CP_UTF8, 0, input.c_str(), size, result.data(), required) ==
                   required
               ? result
               : std::wstring{};
}

[[nodiscard]] std::shared_ptr<const ApplicationIconPixels> Extract(std::string_view icon_key) {
    const auto path = WidePath(icon_key);
    if (path.empty()) {
        return {};
    }
    SHFILEINFOW info{};
    if (::SHGetFileInfoW(path.c_str(), 0U, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON) ==
            0 ||
        info.hIcon == nullptr) {
        return {};
    }
    const UniqueIcon icon{info.hIcon};

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = static_cast<LONG>(kApplicationIconSize);
    bitmap_info.bmiHeader.biHeight = -static_cast<LONG>(kApplicationIconSize);
    bitmap_info.bmiHeader.biPlanes = 1U;
    bitmap_info.bmiHeader.biBitCount = 32U;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    void* raw_pixels = nullptr;
    const UniqueBitmap bitmap{
        ::CreateDIBSection(nullptr, &bitmap_info, DIB_RGB_COLORS, &raw_pixels, nullptr, 0U)};
    const UniqueMemoryDc context{::CreateCompatibleDC(nullptr)};
    auto result = std::shared_ptr<ApplicationIconPixels>{};
    if (bitmap.get() != nullptr && context.get() != nullptr && raw_pixels != nullptr) {
        const ScopedSelection selected{context.get(), bitmap.get()};
        static_cast<void>(::PatBlt(context.get(), 0, 0, static_cast<int>(kApplicationIconSize),
                                   static_cast<int>(kApplicationIconSize), BLACKNESS));
        if (::DrawIconEx(context.get(), 0, 0, icon.get(), static_cast<int>(kApplicationIconSize),
                         static_cast<int>(kApplicationIconSize), 0U, nullptr, DI_NORMAL) != FALSE) {
            result = std::make_shared<ApplicationIconPixels>();
            std::copy_n(static_cast<const std::uint32_t*>(raw_pixels), result->bgra.size(),
                        result->bgra.begin());
            result->fallback = false;
        }
    }
    return result;
}

} // namespace

ApplicationIconCache::ApplicationIconCache() : fallback_{MakeFallback()} {
    worker_ = std::jthread{[this](const std::stop_token& stop_token) { Worker(stop_token); }};
}

ApplicationIconCache::~ApplicationIconCache() {
    SetNotifyWindow(nullptr);
    worker_.request_stop();
    wake_.notify_all();
}

void ApplicationIconCache::SetNotifyWindow(HWND window) noexcept {
    try {
        const std::scoped_lock lock{notification_mutex_};
        notify_window_ = window;
    } catch (...) {
        degraded_.store(true, std::memory_order_release);
    }
}

void ApplicationIconCache::Request(const std::string& icon_key) noexcept {
    if (icon_key.empty()) {
        return;
    }
    try {
        const std::scoped_lock lock{mutex_};
        if (icons_.contains(icon_key) || pending_.contains(icon_key)) {
            return;
        }
        pending_.insert(icon_key);
        requests_.push_back(icon_key);
        wake_.notify_one();
    } catch (...) {
        degraded_.store(true, std::memory_order_release);
    }
}

std::shared_ptr<const ApplicationIconPixels> ApplicationIconCache::Lookup(
    const std::string& icon_key) const noexcept {
    try {
        const std::scoped_lock lock{mutex_};
        const auto found = icons_.find(icon_key);
        return found == icons_.end() ? fallback_ : found->second;
    } catch (...) {
        return fallback_;
    }
}

void ApplicationIconCache::Worker(const std::stop_token& stop_token) noexcept {
    while (!stop_token.stop_requested()) {
        try {
            std::string key;
            {
                std::unique_lock lock{mutex_};
                wake_.wait(lock, stop_token, [this] { return !requests_.empty(); });
                if (stop_token.stop_requested()) {
                    return;
                }
                key = std::move(requests_.front());
                requests_.pop_front();
            }
            std::shared_ptr<const ApplicationIconPixels> icon;
            try {
                icon = Extract(key);
            } catch (...) {
                icon.reset();
            }
            const std::scoped_lock lock{mutex_};
            icons_.insert_or_assign(key, icon != nullptr ? std::move(icon) : fallback_);
            pending_.erase(key);
            {
                const std::scoped_lock notification_lock{notification_mutex_};
                if (notify_window_ != nullptr) {
                    static_cast<void>(
                        ::PostMessageW(notify_window_, kApplicationIconReadyMessage, 0U, 0));
                }
            }
        } catch (...) {
            degraded_.store(true, std::memory_order_release);
        }
    }
}

} // namespace nfm::ui
