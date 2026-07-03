#include "image_cache.h"
#include <cpr/cpr.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"  // vendored in third_party/ — no package manager ships this consistently

namespace leetcli {

namespace {
    // Stable-enough (not cryptographic) filename for a URL's disk cache entry.
    std::string hash_url(const std::string& url) {
        std::ostringstream oss;
        oss << std::hex << std::hash<std::string>{}(url);
        return oss.str();
    }
}  // namespace

bool decode_image(const std::vector<uint8_t>& bytes, DecodedImage& out) {
    if (bytes.empty()) return false;
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                                   &w, &h, &channels, 4);
    if (!pixels) return false;
    out.width = w;
    out.height = h;
    out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    out.raw_bytes = bytes;
    return true;
}

const DecodedImage* ImageCache::get_or_fetch(const std::string& url, const std::string& disk_cache_dir,
                                              std::function<void()> on_ready) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = ready_.find(url);
        if (it != ready_.end()) return &it->second;
        if (in_flight_.count(url)) return nullptr;
        in_flight_.insert(url);
    }

    std::thread([this, url, disk_cache_dir, on_ready]() {
        std::vector<uint8_t> bytes;
        std::string cache_path;
        if (!disk_cache_dir.empty()) {
            cache_path = disk_cache_dir + "/" + hash_url(url) + ".bin";
            std::ifstream in(cache_path, std::ios::binary);
            if (in) {
                bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }
        }

        if (bytes.empty()) {
            cpr::Response r = cpr::Get(cpr::Url{url});
            if (r.status_code == 200 && !r.text.empty()) {
                bytes.assign(r.text.begin(), r.text.end());
                if (!disk_cache_dir.empty()) {
                    std::error_code ec;
                    std::filesystem::create_directories(disk_cache_dir, ec);
                    std::ofstream out(cache_path, std::ios::binary);
                    out.write(reinterpret_cast<const char*>(bytes.data()),
                              static_cast<std::streamsize>(bytes.size()));
                }
            }
        }

        DecodedImage decoded;
        bool ok = decode_image(bytes, decoded);

        {
            std::lock_guard<std::mutex> lock(mu_);
            in_flight_.erase(url);
            if (ok) ready_[url] = std::move(decoded);
        }
        if (ok && on_ready) on_ready();
    }).detach();

    return nullptr;
}

}  // namespace leetcli
