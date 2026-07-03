#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace leetcli {

    enum class ImageProtocol { None, Kitty, ITerm2 };

    // Detects the current terminal's inline-image protocol support via env
    // vars (KITTY_WINDOW_ID, TERM_PROGRAM, TERM). Purely a terminal-capability
    // check — callers that want to honor a user "off" preference should skip
    // calling this and use ImageProtocol::None directly instead.
    ImageProtocol detect_image_protocol();

    // Builds the raw escape-sequence bytes to transmit+display an image
    // stretched to fill exactly `columns` x `rows` terminal cells — this
    // must match the size of the ftxui placeholder box the caller reserved
    // for it (kImagePlaceholderColumns/Rows), otherwise the terminal ends up
    // drawing the image larger/taller than the space actually reserved for
    // it, spilling over the rest of the UI. Not aspect-corrected (see
    // image_cache.h's placeholder-size doc comment for why). Kitty transmits
    // the raw decoded `rgba` pixels (needs `width`/`height`); iTerm2 decodes
    // `raw_bytes` (the original downloaded PNG/JPEG/etc. file) itself, so
    // `rgba`/`width`/`height` are ignored for that protocol. `id` is the
    // Kitty image id, reused later to delete this placement; ignored for
    // iTerm2 (no equivalent primitive). Returns "" for ImageProtocol::None.
    std::string build_display_sequence(ImageProtocol proto,
                                        const std::vector<uint8_t>& raw_bytes,
                                        const std::vector<uint8_t>& rgba,
                                        int width, int height, int columns, int rows, uint32_t id);

    // Builds the escape sequence to remove a previously displayed image.
    // Empty string for iTerm2 (no delete primitive) and ImageProtocol::None.
    std::string build_delete_sequence(ImageProtocol proto, uint32_t id);

    // Process-wide monotonically increasing id for Kitty image placements
    // (ignored by iTerm2). Thread-safe.
    uint32_t next_kitty_image_id();

}  // namespace leetcli
