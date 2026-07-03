#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace leetcli {

    // Placeholder sizing budget (in terminal cells) for a rendered image —
    // the largest box an image is allowed to occupy, so one diagram can't
    // dominate the whole description panel.
    constexpr int kImagePlaceholderColumns = 40;
    constexpr int kImagePlaceholderMinRows = 4;
    constexpr int kImagePlaceholderMaxRows = 24;

    // Terminal cells are roughly twice as tall as they are wide in pixels,
    // so a 1:1 mapping of image pixels to cells would look stretched.
    constexpr double kTerminalCellAspect = 0.5;  // cell_width / cell_height

    // Fits an image into a `max_columns` x `max_rows` box while exactly
    // preserving its visual aspect ratio (like CSS's object-fit: contain) —
    // i.e. shrinks whichever dimension is the binding constraint, rather
    // than independently deriving one dimension from the other and then
    // clamping it, which silently distorts the image whenever the clamp
    // actually kicks in. Always returns at least 1x1.
    inline void fit_image_to_cells(int img_width, int img_height, int max_columns, int max_rows,
                                    int& out_columns, int& out_rows) {
        max_columns = std::max(1, max_columns);
        max_rows = std::max(1, max_rows);
        if (img_width <= 0 || img_height <= 0) {
            out_columns = max_columns;
            out_rows = max_rows;
            return;
        }
        // columns-per-row ratio that preserves visual aspect ratio, after
        // correcting for the cell aspect ratio above.
        double col_per_row = (static_cast<double>(img_width) / img_height) / kTerminalCellAspect;

        double rows_at_full_width = max_columns / col_per_row;
        if (rows_at_full_width <= max_rows) {
            // Width is the binding constraint.
            out_columns = max_columns;
            out_rows = std::max(1, static_cast<int>(rows_at_full_width));
        } else {
            // Height is the binding constraint.
            out_rows = max_rows;
            out_columns = std::max(1, static_cast<int>(max_rows * col_per_row));
        }
    }

    struct DecodedImage {
        std::vector<uint8_t> raw_bytes;  // original downloaded file (PNG/JPEG/GIF/...), for iTerm2
        std::vector<uint8_t> rgba;       // decoded RGBA8 pixels, for Kitty's raw transmission mode
        int width = 0;
        int height = 0;
    };

    // Decodes an in-memory image file (PNG/JPEG/GIF/BMP/...) via stb_image.
    // Animated GIFs decode to their first frame only. Returns false on
    // unsupported/corrupt data.
    bool decode_image(const std::vector<uint8_t>& bytes, DecodedImage& out);

    // Fetch+decode+cache for problem-description images, shared across both
    // TUI tabs' lifetime. Safe to call from any thread.
    class ImageCache {
    public:
        // Non-blocking. Returns the cached decode if already present. If not,
        // and no fetch for `url` is already in flight, kicks off a detached
        // background thread that downloads (or reads from
        // `disk_cache_dir`/<hash>.bin if present), decodes, caches, and then
        // invokes `on_ready` — the caller is expected to wire `on_ready` to
        // trigger a redraw (screen.Post + PostEvent, same pattern as the
        // existing browse_desc_loading spinner). Returns nullptr in that case.
        // `disk_cache_dir` == "" means memory-only caching (used for the
        // Browse tab's not-yet-fetched previews, which are never written to
        // disk).
        const DecodedImage* get_or_fetch(const std::string& url, const std::string& disk_cache_dir,
                                          std::function<void()> on_ready);

    private:
        std::mutex mu_;
        std::unordered_map<std::string, DecodedImage> ready_;
        std::unordered_set<std::string> in_flight_;
    };

}  // namespace leetcli
