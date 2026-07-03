#include "utils.h"
#include "image_protocol.h"
#include <regex>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <sstream>
#include <cctype>
#include <algorithm>

using namespace ftxui;

namespace leetcli {

namespace {
    // ── palette (mirrors tui.cpp's; kept local since utils.cpp has no
    //    dependency on tui.cpp) ──────────────────────────────────────────────
    const Color kDescText   = Color::RGB(210, 215, 230);
    const Color kDescMuted  = Color::RGB(190, 195, 210);
    const Color kDescAccent = Color::Cyan;
    const Color kCodeFg     = Color::RGB(255, 190, 120);
    const Color kCodeBg     = Color::RGB(40, 42, 54);
    const Color kPreBg      = Color::RGB(26, 28, 38);
    const Color kPreFg      = Color::RGB(200, 220, 255);

    std::string html_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                        [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    std::string html_trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    // Encode a Unicode code point as UTF-8.
    std::string utf8_encode(unsigned int cp) {
        std::string s;
        if (cp <= 0x7F) {
            s += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return s;
    }

    // Decodes the handful of HTML entities LeetCode problem descriptions
    // actually use, plus numeric entities (&#123; / &#x7B;).
    std::string decode_entities(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        size_t i = 0;
        while (i < s.size()) {
            if (s[i] == '&') {
                size_t semi = s.find(';', i);
                if (semi != std::string::npos && semi - i <= 12) {
                    std::string ent = s.substr(i + 1, semi - i - 1);
                    std::string rep;
                    bool matched = true;
                    if (ent == "amp") rep = "&";
                    else if (ent == "lt") rep = "<";
                    else if (ent == "gt") rep = ">";
                    else if (ent == "nbsp") rep = " ";
                    else if (ent == "quot") rep = "\"";
                    else if (ent == "apos" || ent == "#39") rep = "'";
                    else if (ent == "le") rep = utf8_encode(0x2264);
                    else if (ent == "ge") rep = utf8_encode(0x2265);
                    else if (ent == "ne") rep = utf8_encode(0x2260);
                    else if (ent == "times") rep = utf8_encode(0x00D7);
                    else if (ent == "divide") rep = utf8_encode(0x00F7);
                    else if (ent == "minus") rep = utf8_encode(0x2212);
                    else if (ent == "mdash") rep = utf8_encode(0x2014);
                    else if (ent == "ndash") rep = utf8_encode(0x2013);
                    else if (ent == "hellip") rep = utf8_encode(0x2026);
                    else if (ent == "deg") rep = utf8_encode(0x00B0);
                    else if (!ent.empty() && ent[0] == '#') {
                        try {
                            unsigned int code;
                            if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) {
                                code = std::stoul(ent.substr(2), nullptr, 16);
                            } else {
                                code = std::stoul(ent.substr(1));
                            }
                            rep = utf8_encode(code);
                        } catch (...) {
                            matched = false;
                        }
                    } else {
                        matched = false;
                    }

                    if (matched) {
                        out += rep;
                        i = semi + 1;
                        continue;
                    }
                }
            }
            out += s[i++];
        }
        return out;
    }

    // Extracts the lowercase tag name from a raw tag body (the text between
    // '<' and '>', without the brackets), e.g. "/strong", "code class=\"x\"",
    // "br/" -> "strong" (closing flag handled separately), "code", "br".
    std::string tag_name(const std::string& raw, bool& closing) {
        std::string body = raw;
        closing = !body.empty() && body[0] == '/';
        if (closing) body = body.substr(1);
        size_t end = body.find_first_of(" \t\r\n/");
        if (end != std::string::npos) body = body.substr(0, end);
        return html_lower(html_trim(body));
    }

    // Extracts an attribute's value from a raw tag body, e.g.
    // attr_value("img src=\"https://x/y.png\" alt=\"z\"", "src") -> the URL.
    // Requires the attribute name to start at a word boundary so e.g.
    // "data-src=" doesn't get mistaken for "src=".
    std::string attr_value(const std::string& raw_tag, const std::string& attr) {
        std::string lower = html_lower(raw_tag);
        std::string needle = attr + "=";
        size_t pos = 0;
        while (true) {
            pos = lower.find(needle, pos);
            if (pos == std::string::npos) return "";
            bool boundary = (pos == 0) || std::isspace(static_cast<unsigned char>(lower[pos - 1]));
            if (boundary) break;
            pos += needle.size();
        }
        pos += needle.size();
        if (pos >= raw_tag.size()) return "";
        char quote = raw_tag[pos];
        if (quote == '"' || quote == '\'') {
            size_t end = raw_tag.find(quote, pos + 1);
            if (end == std::string::npos) return "";
            return raw_tag.substr(pos + 1, end - pos - 1);
        }
        size_t end = raw_tag.find_first_of(" \t\r\n", pos);
        if (end == std::string::npos) end = raw_tag.size();
        return raw_tag.substr(pos, end - pos);
    }
}  // namespace

Elements html_to_ftxui(const std::string& html, ImageCache& cache,
                        const std::string& disk_cache_dir,
                        const std::function<void()>& on_image_ready,
                        std::deque<ImagePlacement>& out_placements) {
    Elements out;

    int bold_depth = 0, italic_depth = 0, code_depth = 0, sup_depth = 0, heading_depth = 0;
    struct ListCtx { bool ordered; int counter; };
    std::vector<ListCtx> list_stack;

    Elements current_word_parts;
    Elements current_line_words;

    bool in_pre = false;
    std::string pre_buffer;
    // Tracks whether the last thing appended to `out` was a blank-line
    // separator, so back-to-back flush_paragraph(true) calls (e.g. closing
    // one block then opening the next) never stack multiple blank lines.
    bool last_was_blank = true;

    auto flush_word = [&]() {
        if (current_word_parts.empty()) return;
        Element word = current_word_parts.size() == 1
            ? current_word_parts[0]
            : hbox(current_word_parts);
        current_line_words.push_back(word);
        current_word_parts.clear();
    };

    auto flush_paragraph = [&](bool spacer) {
        flush_word();
        if (!current_line_words.empty()) {
            out.push_back(flexbox(current_line_words, FlexboxConfig().SetGap(0, 0)));
            current_line_words.clear();
            last_was_blank = false;
        }
        // Emit the blank-line separator unconditionally when requested, even
        // if there was nothing pending to flush (e.g. closing </ul> right
        // after the last </li> already flushed it) — but never emit two in
        // a row.
        if (spacer && !last_was_blank) {
            out.push_back(text(""));
            last_was_blank = true;
        }
    };

    auto flush_pre = [&]() {
        std::string content = decode_entities(pre_buffer);
        // Trim a single leading/trailing blank line (common LeetCode formatting).
        while (!content.empty() && content.front() == '\n') content.erase(content.begin());
        while (!content.empty() && (content.back() == '\n' || content.back() == '\r'))
            content.pop_back();

        Elements pre_lines;
        std::istringstream iss(content);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pre_lines.push_back(text("  " + line));
        }
        if (pre_lines.empty()) pre_lines.push_back(text("  "));

        if (!last_was_blank) out.push_back(text(""));
        out.push_back(vbox(pre_lines) | color(kPreFg) | bgcolor(kPreBg));
        out.push_back(text(""));
        last_was_blank = true;
        pre_buffer.clear();
    };

    auto emit_text = [&](const std::string& raw) {
        std::string s = decode_entities(raw);
        size_t j = 0;
        while (j < s.size()) {
            if (std::isspace(static_cast<unsigned char>(s[j]))) {
                bool had_word = !current_word_parts.empty();
                bool code_space = code_depth > 0;
                flush_word();
                // Insert the inter-word gap as an explicit element (instead of
                // relying on the flexbox gap) so a bgcolor'd run (e.g. a
                // multi-word <code> span) stays visually contiguous instead of
                // showing an uncolored seam between words.
                if (had_word) {
                    Element sp = text(" ");
                    if (code_space) sp = sp | color(kCodeFg) | bgcolor(kCodeBg);
                    current_line_words.push_back(sp);
                }
                ++j;
                continue;
            }
            size_t start = j;
            while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
            std::string chunk = s.substr(start, j - start);

            Element el = text(chunk) | color(kDescText);
            if (heading_depth > 0) el = el | color(kDescAccent) | bold;
            if (code_depth > 0) el = el | color(kCodeFg) | bgcolor(kCodeBg);
            if (bold_depth > 0) el = el | bold;
            if (italic_depth > 0) el = el | italic;
            if (sup_depth > 0) el = el | dim | italic;
            current_word_parts.push_back(el);
        }
    };

    size_t i = 0;
    while (i < html.size()) {
        if (html[i] == '<') {
            size_t end = html.find('>', i);
            if (end == std::string::npos) break;  // malformed tail, stop parsing
            std::string raw_tag = html.substr(i + 1, end - i - 1);
            i = end + 1;

            if (in_pre) {
                bool closing;
                std::string name = tag_name(raw_tag, closing);
                if (closing && name == "pre") {
                    in_pre = false;
                    flush_pre();
                }
                // Any other tag inside <pre> (e.g. <code>) is dropped; its
                // text content is preserved via the raw-text branch below.
                continue;
            }

            bool closing;
            std::string name = tag_name(raw_tag, closing);

            if (name == "p") {
                flush_paragraph(true);
            } else if (name.size() == 2 && name[0] == 'h' && std::isdigit(static_cast<unsigned char>(name[1]))) {
                if (!closing) { flush_paragraph(true); heading_depth++; }
                else { heading_depth = std::max(0, heading_depth - 1); flush_paragraph(true); }
            } else if (name == "br") {
                flush_word();
                if (!current_line_words.empty()) {
                    out.push_back(flexbox(current_line_words, FlexboxConfig().SetGap(0, 0)));
                    current_line_words.clear();
                }
            } else if (name == "strong" || name == "b") {
                bold_depth += closing ? -1 : 1;
                bold_depth = std::max(0, bold_depth);
            } else if (name == "em" || name == "i") {
                italic_depth += closing ? -1 : 1;
                italic_depth = std::max(0, italic_depth);
            } else if (name == "sup") {
                sup_depth += closing ? -1 : 1;
                sup_depth = std::max(0, sup_depth);
            } else if (name == "code") {
                code_depth += closing ? -1 : 1;
                code_depth = std::max(0, code_depth);
            } else if (name == "pre") {
                if (!closing) {
                    flush_paragraph(false);
                    in_pre = true;
                    pre_buffer.clear();
                }
            } else if (name == "ul" || name == "ol") {
                if (!closing) {
                    flush_paragraph(list_stack.empty());
                    list_stack.push_back({name == "ol", 0});
                } else if (!list_stack.empty()) {
                    list_stack.pop_back();
                    if (list_stack.empty()) flush_paragraph(true);
                }
            } else if (name == "li") {
                if (!closing) {
                    flush_paragraph(false);
                    int depth = static_cast<int>(list_stack.size());
                    std::string indent((std::max(0, depth - 1)) * 2, ' ');
                    std::string marker = "•";
                    if (!list_stack.empty()) {
                        if (list_stack.back().ordered) {
                            list_stack.back().counter++;
                            marker = std::to_string(list_stack.back().counter) + ".";
                        }
                    }
                    current_line_words.push_back(text(indent + marker) | color(kDescAccent) | bold);
                    current_line_words.push_back(text(" "));
                } else {
                    flush_paragraph(false);
                }
            } else if (name == "img") {
                if (!closing) {
                    std::string src = attr_value(raw_tag, "src");
                    if (!src.empty()) {
                        flush_paragraph(false);
                        if (!last_was_blank) out.push_back(text(""));
                        const DecodedImage* decoded = cache.get_or_fetch(src, disk_cache_dir, on_image_ready);
                        if (decoded) {
                            out_placements.push_back(
                                ImagePlacement{src, unset_image_box(), next_kitty_image_id(), decoded});
                            int cols, rows;
                            fit_image_to_cells(decoded->width, decoded->height, kImagePlaceholderColumns,
                                                kImagePlaceholderMaxRows, cols, rows);
                            Elements blank_lines(rows, text(""));
                            out.push_back(vbox(blank_lines)
                                              | size(WIDTH, EQUAL, cols)
                                              | size(HEIGHT, EQUAL, rows)
                                              | reflect(out_placements.back().box));
                        } else {
                            out.push_back(text("[loading image]") | color(kDescMuted) | italic);
                        }
                        out.push_back(text(""));
                        last_was_blank = true;
                    }
                }
                // </img> never occurs (void element) — nothing to do on close.
            }
            // Any other/unknown tag (div, span, a, table, ...): ignore the
            // tag boundary itself but keep flowing its inner text.
            continue;
        }

        size_t next = html.find('<', i);
        if (next == std::string::npos) next = html.size();
        std::string raw_text = html.substr(i, next - i);
        i = next;

        if (in_pre) {
            pre_buffer += raw_text;
        } else {
            emit_text(raw_text);
        }
    }

    flush_paragraph(false);

    if (out.empty()) out.push_back(text("(no description)") | color(kDescMuted) | italic);
    return out;
}

    void set_gemini_key(const std::string& key) {
        nlohmann::json config;
        std::filesystem::path path = std::filesystem::path(get_home()) / ".leetcli/config.json";

        // If config already exists, preserve other values
        if (std::ifstream in(path); in) {
            in >> config;
        }

        config["gemini_key"] = key;

        std::ofstream out(path);
        out << config.dump(2);
        std::cout << "✅ Gemini key saved to " << path << "\n";
    }

    std::string get_gemini_key() {
        std::ifstream in(std::filesystem::path(get_home()) / ".leetcli/config.json");
        if (!in) throw std::runtime_error("No config file found");

        nlohmann::json config;
        in >> config;

        if (!config.contains("gemini_key")) {
            throw std::runtime_error("Gamini key not set in config");
        }

        return config["gemini_key"];
    }

    void fetch_testcases(const std::string& slug, const std::string& folder_path) {
        std::string session = get_session_cookie();
        std::string csrf = get_csrf_token();

        // GraphQL query to extract exampleTestcaseList
        nlohmann::json request_body = {
            {"operationName", "consolePanelConfig"},
            {"query", R"(
            query consolePanelConfig($titleSlug: String!) {
                question(titleSlug: $titleSlug) {
                    exampleTestcaseList
                }
            })"},
            {"variables", {{"titleSlug", slug}}}
        };

        cpr::Response response = cpr::Post(
            cpr::Url{"https://leetcode.com/graphql"},
            cpr::Header{
                {"Content-Type", "application/json"},
                {"x-csrftoken", csrf},
                {"Cookie", "LEETCODE_SESSION=" + session + "; csrftoken=" + csrf},
                {"Referer", "https://leetcode.com/problems/" + slug + "/"}
            },
            cpr::Body{request_body.dump()}
        );

        if (response.status_code != 200) {
            std::cerr << "❌ Failed to fetch testcases: " << response.status_code << "\n";
            return;
        }

        auto json = nlohmann::json::parse(response.text);
        auto testcases_json = json["data"]["question"]["exampleTestcaseList"];
        if (!testcases_json.is_array()) {
            std::cerr << "❌ No testcases found in response.\n";
            return;
        }

        // Create file path and directory
        std::filesystem::path dir = folder_path;
        std::filesystem::create_directories(dir);
        std::ofstream outfile(dir / "testcases.txt");

        for (size_t i = 0; i < testcases_json.size(); ++i) {
            outfile << testcases_json[i].get<std::string>();
            if (i + 1 != testcases_json.size())
                outfile << "\n---\n";  // separator between test cases
        }

        std::cout << "✅ Saved testcases to " << (dir / "testcases.txt") << "\n";
    }

    std::string get_question_id(const std::string& slug, const std::string& session, const std::string& csrf) {
        nlohmann::json payload = {
            {"operationName", "getQuestionDetail"},
            {"query", R"(
            query getQuestionDetail($titleSlug: String!) {
                question(titleSlug: $titleSlug) {
                    questionId
                }
            }
        )"},
            {"variables", {{"titleSlug", slug}}}
        };

        cpr::Response r = cpr::Post(
            cpr::Url{"https://leetcode.com/graphql"},
            cpr::Header{
                {"Content-Type", "application/json"},
                {"x-csrftoken", csrf},
                {"Cookie", "LEETCODE_SESSION=" + session + "; csrftoken=" + csrf}
            },
            cpr::Body{payload.dump()}
        );

        if (r.status_code != 200) throw std::runtime_error("Failed to get questionId");
        auto json = nlohmann::json::parse(r.text);
        return json["data"]["question"]["questionId"];
    }

    std::string get_file_extension(const std::string& filename) {
        size_t dot_pos = filename.rfind('.');
        if (dot_pos == std::string::npos || dot_pos == filename.length() - 1) {
            return ""; // No extension or empty extension
        }
        return filename.substr(dot_pos + 1);
    }

    static std::filesystem::path get_home() {
        if (auto *h = std::getenv("HOME"); h && *h) return h;
        if (auto *u = std::getenv("USERPROFILE"); u && *u) return u;
        std::cerr << "Fatal: cannot determine home directory.\n";
        std::exit(1);
    }

    std::string get_preferred_language() {
        std::filesystem::path config_path = std::filesystem::path(get_home()) / ".leetcli/config.json";
        if (!std::filesystem::exists(config_path)) {
            std::cerr << "Error: config not found. Run `leetcli init` first.\n";
            std::exit(1);
        }

        std::ifstream in(config_path);
        nlohmann::json config;
        in >> config;
        return config.value("lang", "cpp"); // fallback to cpp
    }

    std::string get_image_rendering_pref() {
        std::filesystem::path config_path = std::filesystem::path(get_home()) / ".leetcli/config.json";
        std::ifstream in(config_path);
        if (!in) return "auto";
        nlohmann::json config;
        in >> config;
        return config.value("image_rendering", "auto"); // "auto" or "off"
    }

    std::string get_problems_dir() {
        std::filesystem::path config_path = get_home() / ".leetcli/config.json";
        if (!std::filesystem::exists(config_path)) {
            std::cerr << "Error:  not found. Run `leetcli init` first.\n";
            std::exit(1);
        }

        std::ifstream in(config_path);
        nlohmann::json config;
        in >> config;
        return config["problems_dir"];
    }

    void init_problems_folder() {
        std::filesystem::path home = get_home();
        std::cout << "home: " << home << "\n";
        std::filesystem::path config_dir = home / ".leetcli";
        std::filesystem::path config_path = config_dir / "config.json";

        if (std::filesystem::exists(config_path)) {
            std::cerr << "leetcli is already initialized.\n";
            std::cerr << "To reset: delete " << config_path << "\n";
            return;
        }

        // Create config and problems dir
        std::string default_path = std::filesystem::current_path().string() + "/problems/";
        std::filesystem::create_directories(config_dir);
        std::filesystem::create_directories(default_path);

        // Ask for preferred language
        std::string lang;
        std::cout << "Enter your preferred language (e.g., cpp, python3, java): ";
        std::getline(std::cin, lang);
        if (lang != "python3" && lang != "cpp" && lang != "java") {
            std::cout << "Language is not supported";
            return;
        }
        nlohmann::json config = {
            {"problems_dir", default_path},
            {"lang", lang}
        };

        std::ofstream out(config_path);
        if (!out) {
            std::cerr << "Failed to write config: " << config_path << "\n";
            return;
        }

        out << config.dump(4);
        std::cout << "leetcli initialized.\nProblems will be saved to:\n  " << default_path << "\n";
    }

    bool is_leetcli_initialized() {
        return std::filesystem::exists(get_home() / ".leetcli" / "config.json");
    }

    void write_initial_config(const std::string &problems_dir, const std::string &lang) {
        std::filesystem::path config_dir = get_home() / ".leetcli";
        std::filesystem::create_directories(config_dir);
        std::filesystem::create_directories(problems_dir);

        nlohmann::json config = {
            {"problems_dir", problems_dir},
            {"lang", lang}
        };
        std::ofstream out(config_dir / "config.json");
        out << config.dump(4);
    }

    void save_session_tokens(const std::string &session, const std::string &csrf) {
        std::filesystem::path config_path = get_home() / ".leetcli" / "config.json";
        nlohmann::json config;
        std::ifstream in(config_path);
        if (in) in >> config;

        config["leetcode_session"] = session;
        config["csrf_token"] = csrf;

        std::ofstream out(config_path);
        out << config.dump(4);
    }

    std::string html_to_text(const std::string &html) {
        std::string text = html;
        text = std::regex_replace(text, std::regex("<h1[^>]*>"), "# ");
        text = std::regex_replace(text, std::regex("<h2[^>]*>"), "## ");
        text = std::regex_replace(text, std::regex("</h[1-6]>"), "\n");
        text = std::regex_replace(text, std::regex("<p[^>]*>"), "\n");
        text = std::regex_replace(text, std::regex("</p>"), "\n");
        text = std::regex_replace(text, std::regex("<br[^>]*>"), "\n");
        text = std::regex_replace(text, std::regex("<li[^>]*>"), " - ");
        text = std::regex_replace(text, std::regex("</li>"), "\n");
        text = std::regex_replace(text, std::regex("<pre[^>]*><code[^>]*>"), "```\n");
        text = std::regex_replace(text, std::regex("</code></pre>"), "\n```");
        text = std::regex_replace(text, std::regex("<code[^>]*>"), "`");
        text = std::regex_replace(text, std::regex("</code>"), "`");
        text = std::regex_replace(text, std::regex("<b[^>]*>"), "**");
        text = std::regex_replace(text, std::regex("</b>"), "**");
        text = std::regex_replace(text, std::regex("<strong[^>]*>"), "**");
        text = std::regex_replace(text, std::regex("</strong>"), "**");
        text = std::regex_replace(text, std::regex("<[^>]*>"), "");

        text = std::regex_replace(text, std::regex("&nbsp;"), " ");
        text = std::regex_replace(text, std::regex("&lt;"), "<");
        text = std::regex_replace(text, std::regex("&gt;"), ">");
        text = std::regex_replace(text, std::regex("&amp;"), "&");
        text = std::regex_replace(text, std::regex("&quot;"), "\"");

        return text;
    }

    void write_markdown_file(const std::string &path, const std::string &title, const std::string &markdown) {
        std::ofstream out(path);
        if (!out) {
            std::cerr << "Failed to write markdown: " << path << "\n";
            return;
        }
        out << "# " << title << "\n\n" << markdown << "\n";
        out.close();
    }

    void write_html_file(const std::string &path, const std::string &html) {
        std::ofstream out(path);
        if (!out) {
            std::cerr << "Failed to write html: " << path << "\n";
            return;
        }
        out << html;
        out.close();
    }

    void write_lines_file(const std::string &path, const std::vector<std::string> &lines) {
        std::ofstream out(path);
        if (!out) {
            std::cerr << "Failed to write: " << path << "\n";
            return;
        }
        for (const auto &line : lines) out << line << "\n";
        out.close();
    }

    void write_solution_file(const std::string &path, const std::string &code) {
        if (std::filesystem::exists(path)) {
            return;
        }
        std::ofstream out(path);
        if (!out) {
            std::cerr << "Failed to write solution: " << path << "\n";
            return;
        }
        out << code << "\n";
        out.close();
    }

    // Empty return means "no preference" — config.json has no "editor" key
    // and $EDITOR isn't set — letting launch_in_editor() apply its own
    // platform-specific fallback rather than hardcoding one here.
    std::string get_preferred_editor() {
        std::filesystem::path config_path = get_home() / ".leetcli/config.json";
        if (std::filesystem::exists(config_path)) {
            std::ifstream in(config_path);
            nlohmann::json config;
            in >> config;
            if (config.contains("editor") && config["editor"].is_string() &&
                !config["editor"].get<std::string>().empty())
                return config["editor"];
        }
        const char* editor_env = std::getenv("EDITOR");
        return (editor_env && *editor_env) ? editor_env : "";
    }

    void set_editor_preference(const std::string &editor) {
        std::filesystem::path config_path = get_home() / ".leetcli/config.json";
        nlohmann::json config;
        std::ifstream in(config_path);
        if (in) in >> config;
        config["editor"] = editor;
        std::ofstream out(config_path);
        out << config.dump(4);
    }

    void launch_in_editor(const std::string &path) {
        std::string editor = get_preferred_editor();
        #ifdef _WIN32
                if (!editor.empty()) {
                    std::string command = editor + " \"" + path + "\"";
                    std::system(command.c_str());
                } else {
                    std::string command = "start \"\" \"" + path + "\""; // Default program
                    std::system(command.c_str());
                }
        #else
                if (editor.empty()) editor = "vi"; // or nano or sensible-editor
                std::string command = editor + " \"" + path + "\"";
                std::system(command.c_str());
        #endif
    }

    void set_session_cookie() {
        std::filesystem::path config_path = get_home() / ".leetcli/config.json";

        if (!std::filesystem::exists(config_path)) {
            std::cerr << "Run `leetcli init` first.\n";
            std::exit(1);
        }

        std::ifstream in(config_path);
        nlohmann::json config;
        in >> config;

        std::string session, csrf;
        std::cout << "Paste your LEETCODE_SESSION cookie (Dev Tools -> Application -> Cookies):\n> ";
        std::getline(std::cin, session);
        std::cout << "Paste your csrftoken cookie (Dev Tools -> Application -> Cookies):\n> ";
        std::getline(std::cin, csrf);

        config["leetcode_session"] = session;
        config["csrf_token"] = csrf;

        std::ofstream out(config_path);
        out << config.dump(4);
        std::cout << "Session & CSRF token saved.\n";
    }

    std::string get_session_cookie() {
        std::filesystem::path config_path = get_home() / ".leetcli/config.json";

        if (!std::filesystem::exists(config_path)) {
            std::cerr << "Error: config not found. Run `leetcli init` first.\n";
            std::exit(1);
        }

        std::ifstream in(config_path);
        nlohmann::json config;
        in >> config;

        if (!config.contains("leetcode_session")) {
            std::cerr << "Error: No session cookie set. Run `leetcli login`.\n";
            std::exit(1);
        }

        return config["leetcode_session"];
    }

    std::string get_csrf_token() {
        std::filesystem::path config_path = get_home() / ".leetcli/config.json";

        std::ifstream in(config_path);
        nlohmann::json config;
        in >> config;

        if (!config.contains("csrf_token")) {
            std::cerr << "No CSRF token found. Run `leetcli login`.\n";
            std::exit(1);
        }

        return config["csrf_token"];
    }

    int get_solution_folder(const std::string &slug, std::string &folder_path) {
        // Step 1: Query LeetCode to get the ID and Title
        nlohmann::json query = {
            {
                "query", R"(
            query getQuestionDetail($titleSlug: String!) {
                question(titleSlug: $titleSlug) {
                    title
                    questionId
                }
            }
        )"
            },
            {"variables", {{"titleSlug", slug}}}
        };

        cpr::Response r = cpr::Post(
            cpr::Url{"https://leetcode.com/graphql"},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{query.dump()}
        );

        if (r.status_code != 200) {
            std::cerr << "Failed to query problem info.\n";
            return 1;
        }

        auto json = nlohmann::json::parse(r.text);
        auto question = json["data"]["question"];
        std::string id = question["questionId"];
        std::string title = question["title"];
        std::string safe_title = std::regex_replace(title, std::regex("[\\\\/:*?\"<>|]"), "");

        // Step 2: Build the folder path
        std::string folder = get_problems_dir() + "/" + id + ". " + safe_title;

        folder_path = folder;
        return 0;
    }

    int get_solution_filepath(const std::string &slug, std::string &solution_file, const std::optional<std::string> &language) {
        // Step 1: Query LeetCode to get the ID and Title
        nlohmann::json query = {
            {
                "query", R"(
                    query getQuestionDetail($titleSlug: String!) {
                        question(titleSlug: $titleSlug) {
                            title
                            questionId
                        }
                    }
                )"
            },
            {"variables", {{"titleSlug", slug}}}
        };

        cpr::Response r = cpr::Post(
            cpr::Url{"https://leetcode.com/graphql"},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{query.dump()}
        );

        if (r.status_code != 200) {
            std::cerr << "Failed to query problem info.\n";
            return 1;
        }

        auto json = nlohmann::json::parse(r.text);
        auto question = json["data"]["question"];
        std::string id = question["questionId"];
        std::string title = question["title"];
        std::string safe_title = std::regex_replace(title, std::regex("[\\\\/:*?\"<>|]"), "");

        // Step 2: Build the folder path
        std::string folder = get_problems_dir() + "/" + id + ". " + safe_title;

        if (!std::filesystem::exists(folder)) {
            std::cerr << "Folder not found. Run: leetcli fetch " << slug << "\n";
            return 1;
        }

        // Step 3: Map supported languages to file extensions
        std::map<std::string, std::string> lang_to_ext = {
            {"cpp", ".cpp"},
            {"python3", ".py"},
            {"java", ".java"},
            {"javascript", ".js"},
            {"csharp", ".cs"}
        };

        std::string lang;
        if (language.has_value()) {
            lang = language.value();
        } else {
            // Load config to get default language
            std::filesystem::path config_path = std::filesystem::path(get_home()) / ".leetcli/config.json";
            std::ifstream in(config_path);
            if (!in) {
                std::cerr << "Could not read config file at " << config_path << "\n";
                return 1;
            }
            nlohmann::json config;
            in >> config;
            lang = config["lang"];
        }
        if (lang_to_ext.find(lang) == lang_to_ext.end()) {
            std::cerr << "Unsupported language: " << lang << "\n";
            return 1;
        }

        std::string ext = lang_to_ext[lang];
        std::filesystem::path candidate = folder + "/solution" + ext;

        if (!std::filesystem::exists(candidate)) {
            std::cerr << "Solution file not found for language '" << lang << "' in: " << candidate << "\n";
            return 1;
        }

        solution_file = candidate.string();
        return 0;
    }
    // Loads testcases from testcases.txt and returns them as a vector of strings
    std::vector<std::string> load_testcases(const std::string& filepath) {
        std::ifstream file(filepath);
        std::vector<std::string> testcases;

        if (!file) {
            std::cerr << "Could not open " << filepath << "\n";
            return testcases;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();  // Read the whole file into a string
        std::string content = buffer.str();

        size_t start = 0;
        size_t end;

        while ((end = content.find("\n---\n", start)) != std::string::npos) {
            testcases.push_back(content.substr(start, end - start));
            start = end + 5;  // length of "\n---\n"
        }

        // Last test case (or only one if no separator)
        if (start < content.size()) {
            testcases.push_back(content.substr(start));
        }

        return testcases;
    }
    void handle_config_command(const std::vector<std::string> &args) {
        if (args.size() == 3 && args[1] == "set-gemini-key") {
            set_gemini_key(args[2]);
        } else {
            std::cerr << "Usage: leetcli config set-gemini-key <your-api-key>\n";
        }
    }

} // namespace leetcli
