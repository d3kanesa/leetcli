#include "image_protocol.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace leetcli {

namespace {
    std::string base64_encode(const std::vector<uint8_t>& data) {
        static const char* tbl =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((data.size() + 2) / 3) * 4);
        size_t i = 0;
        for (; i + 2 < data.size(); i += 3) {
            uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                         (static_cast<uint32_t>(data[i + 1]) << 8) |
                         static_cast<uint32_t>(data[i + 2]);
            out += tbl[(n >> 18) & 0x3F];
            out += tbl[(n >> 12) & 0x3F];
            out += tbl[(n >> 6) & 0x3F];
            out += tbl[n & 0x3F];
        }
        size_t rem = data.size() - i;
        if (rem == 1) {
            uint32_t n = static_cast<uint32_t>(data[i]) << 16;
            out += tbl[(n >> 18) & 0x3F];
            out += tbl[(n >> 12) & 0x3F];
            out += "==";
        } else if (rem == 2) {
            uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                         (static_cast<uint32_t>(data[i + 1]) << 8);
            out += tbl[(n >> 18) & 0x3F];
            out += tbl[(n >> 12) & 0x3F];
            out += tbl[(n >> 6) & 0x3F];
            out += "=";
        }
        return out;
    }

    // Kitty graphics protocol (https://sw.kovidgoyal.net/kitty/graphics-protocol/):
    // APC-framed control data + base64 payload, chunked to <=4096 bytes/chunk
    // with m=1 on all but the final chunk. Only the first chunk carries the
    // format/action/dimension/id keys.
    std::string build_kitty_display(const std::vector<uint8_t>& rgba, int width, int height,
                                     int columns, int rows, uint32_t id) {
        if (rgba.empty() || width <= 0 || height <= 0) return "";
        std::string b64 = base64_encode(rgba);
        constexpr size_t kChunk = 4096;
        std::string out;
        size_t total = b64.size();
        size_t offset = 0;
        bool first = true;
        do {
            size_t len = std::min(kChunk, total - offset);
            bool last = (offset + len) >= total;
            out += "\x1b_G";
            if (first) {
                // c= and r= given together: stretch to fill exactly that
                // many cells (rather than specifying only one and letting
                // the terminal preserve aspect ratio) — must match the fixed
                // ftxui placeholder box size, see the header doc comment.
                // q=2 suppresses the terminal's OK/error acknowledgement
                // reply. Without it, Kitty/Ghostty answer each image with an
                // APC "\x1b_Gi=<id>;OK\x1b\\" response on stdin, which leaks
                // into the TUI's input bar as stray "Gi=2;OK" text.
                out += "f=32,a=T,t=d,s=" + std::to_string(width) + ",v=" + std::to_string(height) +
                       ",c=" + std::to_string(columns) + ",r=" + std::to_string(rows) +
                       ",i=" + std::to_string(id) + ",q=2,m=" + (last ? "0" : "1");
            } else {
                out += "m=";
                out += (last ? "0" : "1");
            }
            out += ";";
            out.append(b64, offset, len);
            out += "\x1b\\";
            offset += len;
            first = false;
        } while (offset < total);
        return out;
    }

    // iTerm2 inline images protocol (https://iterm2.com/documentation-images.html):
    // OSC 1337 ; File=... : <base64 of the *original file bytes*> BEL.
    // iTerm2 decodes the image itself, so this is format-agnostic (PNG/JPEG/GIF).
    std::string build_iterm2_display(const std::vector<uint8_t>& raw_bytes, int columns, int rows) {
        if (raw_bytes.empty()) return "";
        std::string b64 = base64_encode(raw_bytes);
        // Explicit width+height (not "auto") with preserveAspectRatio=0, to
        // match Kitty's stretch-to-fit behavior above — both must fill
        // exactly the fixed ftxui placeholder box, not preserve aspect.
        return "\x1b]1337;File=inline=1;width=" + std::to_string(columns) +
               ";height=" + std::to_string(rows) +
               ";preserveAspectRatio=0;size=" + std::to_string(raw_bytes.size()) +
               ":" + b64 + "\x07";
    }
}  // namespace

ImageProtocol detect_image_protocol() {
    if (auto* v = std::getenv("KITTY_WINDOW_ID"); v && *v) return ImageProtocol::Kitty;
    if (auto* tp = std::getenv("TERM_PROGRAM"); tp && *tp) {
        std::string prog = tp;
        if (prog == "WezTerm") return ImageProtocol::Kitty;  // WezTerm implements the Kitty graphics protocol
        if (prog == "WarpTerminal") return ImageProtocol::Kitty;  // Warp also implements it
        if (prog == "ghostty") return ImageProtocol::Kitty;  // Ghostty implements the Kitty graphics protocol
        if (prog == "iTerm.app") return ImageProtocol::ITerm2;
    }
    // Ghostty ships terminfo as "xterm-ghostty" and doesn't set KITTY_WINDOW_ID;
    // GHOSTTY_RESOURCES_DIR is its reliable marker when TERM_PROGRAM is stripped
    // (e.g. over ssh/tmux/sudo).
    if (auto* g = std::getenv("GHOSTTY_RESOURCES_DIR"); g && *g) return ImageProtocol::Kitty;
    if (auto* term = std::getenv("TERM"); term &&
        (std::strcmp(term, "xterm-kitty") == 0 || std::strcmp(term, "xterm-ghostty") == 0)) {
        return ImageProtocol::Kitty;
    }
    return ImageProtocol::None;
}

std::string build_display_sequence(ImageProtocol proto,
                                    const std::vector<uint8_t>& raw_bytes,
                                    const std::vector<uint8_t>& rgba,
                                    int width, int height, int columns, int rows, uint32_t id) {
    switch (proto) {
        case ImageProtocol::Kitty:  return build_kitty_display(rgba, width, height, columns, rows, id);
        case ImageProtocol::ITerm2: return build_iterm2_display(raw_bytes, columns, rows);
        case ImageProtocol::None:   return "";
    }
    return "";
}

std::string build_delete_sequence(ImageProtocol proto, uint32_t id) {
    if (proto != ImageProtocol::Kitty) return "";
    return "\x1b_Ga=d,d=i,i=" + std::to_string(id) + ",q=2\x1b\\";
}

uint32_t next_kitty_image_id() {
    static std::atomic<uint32_t> counter{1};  // 0 is reserved for "transient" per the protocol
    return counter.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace leetcli
