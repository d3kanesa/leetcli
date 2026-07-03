#include "tui.h"
#include "leetcode_api.h"
#include "utils.h"
#include "image_cache.h"
#include "image_protocol.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/linear_gradient.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/animation.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <functional>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace ftxui;
namespace fs = std::filesystem;

namespace leetcli {
namespace {

// ── helpers ────────────────────────────────────────────────────────────────

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::string> read_file_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    if (!in) return lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::string find_solution_file(const std::string& folder) {
    if (!fs::exists(folder)) return "";
    for (const auto& e : fs::directory_iterator(folder))
        if (e.is_regular_file() && e.path().stem() == "solution")
            return e.path().string();
    return "";
}

// "Last worked on" for a problem folder: the solution file's mtime — that's
// the one thing that changes specifically because you edited/started a
// solution, as opposed to README.md/description.html/testcases.txt/etc.,
// which are all rewritten together on every re-fetch regardless of whether
// you've touched the solution. Falls back to the folder's own mtime (fetch
// time) when there's no solution file yet.
fs::file_time_type last_worked_on(const std::string& folder) {
    std::error_code ec;
    std::string sol = find_solution_file(folder);
    if (!sol.empty()) {
        auto t = fs::last_write_time(sol, ec);
        if (!ec) return t;
    }
    auto t = fs::last_write_time(folder, ec);
    return ec ? fs::file_time_type::min() : t;
}

// True if `name` resolves to an executable file somewhere on $PATH. Used by
// the onboarding wizard to only offer editors that are actually installed,
// rather than a fixed list that may not match the user's machine.
bool command_exists(const std::string& name) {
    const char* path_env = std::getenv("PATH");
    if (!path_env) return false;
#ifdef _WIN32
    const char path_delim = ';';
#else
    const char path_delim = ':';
#endif
    std::stringstream ss{std::string(path_env)};
    std::string dir;
    while (std::getline(ss, dir, path_delim)) {
        if (dir.empty()) continue;
        std::error_code ec;
        if (fs::exists(fs::path(dir) / name, ec) && !ec) return true;
#ifdef _WIN32
        for (const char* ext : {".exe", ".cmd", ".bat"})
            if (fs::exists(fs::path(dir) / (name + ext), ec) && !ec) return true;
#endif
    }
    return false;
}

struct EditorOption { std::string cmd, label; };
// Checked in this order; only ones actually found on $PATH are offered.
const EditorOption kKnownEditors[] = {
    {"nvim",          "Neovim"},
    {"vim",           "Vim"},
    {"code",          "VS Code"},
    {"code-insiders", "VS Code Insiders"},
    {"subl",          "Sublime Text"},
    {"emacs",         "Emacs"},
    {"hx",            "Helix"},
    {"kak",           "Kakoune"},
    {"micro",         "Micro"},
    {"nano",          "Nano"},
    {"atom",          "Atom"},
    {"gedit",         "gedit"},
};
constexpr int kKnownEditorCount = sizeof(kKnownEditors) / sizeof(kKnownEditors[0]);

// Collapses embedded newlines (testcase/output blocks are often multi-line,
// e.g. "[2,7,11,15]\n9") into a compact single display line.
std::string oneline(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\n') out += ", ";
        else out += c;
    }
    return out;
}

std::string fmt1(double v) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(1);
    oss << v;
    return oss.str();
}

bool has_leetcode_session() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) home = std::getenv("USERPROFILE");
    if (!home || !*home) return false;
    std::ifstream in(fs::path(home) / ".leetcli" / "config.json");
    if (!in) return false;
    std::string c((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return c.find("leetcode_session") != std::string::npos;
}

// difficulty is empty for folders fetched before .difficulty existed —
// falls back to a blank badge slot rather than trying to backfill it (that'd
// mean firing a network request per visible row just from opening the tab).
// solved reflects only submissions made through this app's Submit tab (a
// local <folder>/.solved marker written on Accepted) — not your real
// LeetCode account history, which would need a network query per problem.
struct MyProblem { std::string label, folder, slug, difficulty; bool solved; bool attempted = false; };

// One entry in the contextual Actions menu (opened with Space). `key` is the
// accelerator that also triggers the action directly with the menu closed;
// `danger` renders it in the warning color (destructive / consequential).
struct MenuAction {
    char key;
    std::string label;
    bool danger = false;
    std::function<void()> run;
};

// Every language LeetCode's codeSnippets can return (langSlug), offered by
// the "Start solution" dropdown. `ext` is the sole source of truth for the
// solution file's extension — write_starter_solution() takes it as a plain
// parameter rather than re-deriving it, so this is the only place the
// mapping is defined. Legacy "python" (Python 2) is omitted: it shares
// LeetCode's .py convention with python3, and offering both would let one
// silently overwrite the other's solution file.
struct LangOption { std::string id, ext, label; };
const LangOption kLangOptions[] = {
    {"cpp",        ".cpp",    "C++"},
    {"java",       ".java",   "Java"},
    {"python3",    ".py",     "Python3"},
    {"c",          ".c",      "C"},
    {"csharp",     ".cs",     "C#"},
    {"javascript", ".js",     "JavaScript"},
    {"typescript", ".ts",     "TypeScript"},
    {"php",        ".php",    "PHP"},
    {"swift",      ".swift",  "Swift"},
    {"kotlin",     ".kt",     "Kotlin"},
    {"dart",       ".dart",   "Dart"},
    {"golang",     ".go",     "Go"},
    {"ruby",       ".rb",     "Ruby"},
    {"scala",      ".scala",  "Scala"},
    {"rust",       ".rs",     "Rust"},
    {"racket",     ".rkt",    "Racket"},
    {"erlang",     ".erl",    "Erlang"},
    {"elixir",     ".ex",     "Elixir"},
};
constexpr int kLangOptionCount = sizeof(kLangOptions) / sizeof(kLangOptions[0]);

int lang_option_index(const std::string& lang_id) {
    for (int i = 0; i < kLangOptionCount; ++i)
        if (kLangOptions[i].id == lang_id) return i;
    return 0;
}

// Maps an existing solution file's extension (e.g. ".py") back to its
// LeetCode langSlug (e.g. "python3"), for Run/Submit — which need the slug
// of whatever language the solution was already started in, not a picked one.
std::string lang_id_for_ext(const std::string& ext) {
    for (int i = 0; i < kLangOptionCount; ++i)
        if (kLangOptions[i].ext == ext) return kLangOptions[i].id;
    return "cpp";
}

// ── visual helpers ─────────────────────────────────────────────────────────

// Palette
const Color kAccent     = Color::Cyan;
const Color kAccentDark = Color::RGB(0, 150, 180);
const Color kBg         = Color::RGB(12, 14, 22);
const Color kBgPanel    = Color::RGB(18, 20, 32);
const Color kBorder     = Color::RGB(130, 145, 180);
const Color kEasy       = Color::RGB(40, 210, 120);
const Color kMedium     = Color::RGB(240, 180, 40);
const Color kHard       = Color::RGB(230, 70, 70);
const Color kFetched    = Color::RGB(80, 220, 140);
const Color kMuted      = Color::RGB(190, 195, 210);

// Nerd Font "calendar" glyph (nf-fa-calendar), used to mark the pinned daily
// challenge — a monochrome icon rather than a colored emoji. Requires a Nerd
// Font-patched terminal font; falls back to a blank box if unavailable.
const std::string kDailyIcon = "";
const Color kText       = Color::RGB(210, 215, 230);
const Color kHoverBg    = Color::RGB(45, 50, 66);

Color diff_color(const std::string& d) {
    if (d == "Easy")   return kEasy;
    if (d == "Medium") return kMedium;
    if (d == "Hard")   return kHard;
    return kMuted;
}

Element diff_badge(const std::string& d) {
    return text(" " + d + " ") | bold | color(Color::Black) | bgcolor(diff_color(d));
}

// A styled keyboard hint: [key] desc
Element kbd(const std::string& key, const std::string& desc = "") {
    Element k = hbox({text("["), text(key) | bold, text("]")}) | color(kAccent);
    if (desc.empty()) return k;
    return hbox({k, text(" " + desc) | color(kMuted), text("  ")});
}

// Render a scrollable list of lines using paragraph() for word-wrap.
Elements lines_to_paragraphs(const std::vector<std::string>& lines, int offset = 0) {
    Elements out;
    for (int i = offset; i < (int)lines.size(); ++i) {
        const auto& l = lines[i];
        // Style markdown-ish headers and code fences
        if (l.rfind("# ", 0) == 0)
            out.push_back(paragraph(l.substr(2)) | bold | color(kAccent));
        else if (l.rfind("## ", 0) == 0)
            out.push_back(paragraph(l.substr(3)) | bold | color(kText));
        else if (l == "```" || l.rfind("```", 0) == 0)
            out.push_back(separatorEmpty());
        else if (l.empty())
            out.push_back(text(""));
        else
            out.push_back(paragraph(l) | color(kText));
    }
    if (out.empty()) out.push_back(text("(no description)") | color(kMuted) | italic);
    return out;
}

// Wraps content in the same "small centered card on a full screen"
// treatment for both onboarding steps: a branded header, a step subtitle,
// then the card itself, all centered both ways via filler().
Element onboarding_frame(const std::string& subtitle, Element card) {
    Element header = hbox({
        filler(),
        text("* ") | bold | color(kMedium),
        text("leetcli") | bold | color(kAccent),
        text(" setup") | color(kText),
        filler(),
    });
    Element sub = hbox({filler(), text(subtitle) | color(kMuted) | italic, filler()});
    return vbox({
        filler(),
        header,
        text(""),
        sub,
        text(""),
        hbox({filler(), card, filler()}),
        filler(),
    }) | borderStyled(ROUNDED, kBorder);
}

// First-launch onboarding: a folder picker (own little file explorer,
// defaulting to $HOME) followed by a login form for LEETCODE_SESSION/
// csrftoken. Runs in its own ScreenInteractive loop, before run_tui()'s main
// one starts — there's no app state (my_all, browse_problems, ...) to share
// with it yet, so keeping it fully separate is simpler than threading a
// "first run" mode through the whole main loop.
void run_onboarding_wizard() {
    auto screen = ScreenInteractive::Fullscreen();

    const char* home_env = std::getenv("HOME");
    if (!home_env || !*home_env) home_env = std::getenv("USERPROFILE");
    fs::path current_dir = (home_env && *home_env) ? fs::path(home_env) : fs::current_path();

    // ── Step 0: folder picker state ─────────────────────────────────────
    int phase = 0;  // 0 = browsing, 1 = naming a new folder, 2 = editor pick, 3 = login form
    std::vector<std::string> entries;  // ".." (if not at root) + sorted subdirs
    int hovered = 0;
    int list_offset = 0;
    std::string dir_error;
    std::vector<Box> entry_boxes;
    Box list_box, use_button_box;

    auto refresh_entries = [&]() {
        entries.clear();
        dir_error.clear();
        if (current_dir.has_relative_path() && current_dir != current_dir.root_path())
            entries.push_back("..");
        std::vector<std::string> subs;
        std::error_code ec;
        fs::directory_iterator it(current_dir, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            dir_error = "Can't read this folder: " + ec.message();
        } else {
            for (const auto& e : it) {
                std::error_code ec2;
                bool is_dir = e.is_directory(ec2);
                if (ec2 || !is_dir) continue;
                std::string name = e.path().filename().string();
                if (name.empty() || name[0] == '.') continue;  // hide dotfiles
                subs.push_back(name);
            }
        }
        std::sort(subs.begin(), subs.end());
        for (auto& s : subs) entries.push_back(s);
        hovered = std::clamp(hovered, 0, std::max(0, (int)entries.size() - 1));
        list_offset = 0;
    };
    refresh_entries();

    auto descend = [&](const std::string& name) {
        current_dir = (name == "..") ? current_dir.parent_path() : current_dir / name;
        refresh_entries();
    };

    // Rather than storing problems directly in whatever folder the user
    // happens to be browsing, "Create new folder here" prompts for a name
    // and creates <current_dir>/<name> — keeps a general-purpose folder
    // (e.g. Documents, $HOME) from getting a pile of numbered problem
    // folders dumped straight into it.
    std::string chosen_dir;
    std::string new_folder_name;
    std::string folder_name_error;
    auto begin_create_folder = [&]() {
        new_folder_name = "problems";
        folder_name_error.clear();
        phase = 1;
    };
    auto confirm_new_folder = [&]() {
        std::string name = trim(new_folder_name);
        if (name.empty()) {
            folder_name_error = "Folder name can't be empty.";
            return;
        }
        fs::path target = current_dir / name;
        std::error_code ec;
        fs::create_directories(target, ec);
        if (ec) {
            folder_name_error = "Couldn't create that folder: " + ec.message();
            return;
        }
        chosen_dir = target.string();
        phase = 2;
    };
    bool aborted = false;

    InputOption name_input_opt;
    name_input_opt.multiline = false;
    name_input_opt.transform = [](InputState s) { return s.element | color(kText); };
    name_input_opt.on_change = [&] { folder_name_error.clear(); };
    name_input_opt.on_enter = [&] { confirm_new_folder(); };
    auto folder_name_input = Input(&new_folder_name, name_input_opt);

    // ── Step 2: editor picker state ─────────────────────────────────────
    // Only offers editors actually found on $PATH — a fixed list would
    // include ones the user doesn't have installed.
    std::vector<EditorOption> available_editors;
    for (const auto& e : kKnownEditors)
        if (command_exists(e.cmd)) available_editors.push_back(e);
    if (const char* editor_env = std::getenv("EDITOR"); editor_env && *editor_env) {
        bool already_listed = false;
        for (const auto& e : available_editors)
            if (e.cmd == editor_env) already_listed = true;
        if (!already_listed)
            available_editors.insert(available_editors.begin(), {editor_env, std::string(editor_env) + " ($EDITOR)"});
    }
    if (available_editors.empty()) available_editors.push_back({"vi", "vi"});
    int editor_hovered = 0;
    std::string chosen_editor;
    std::vector<Box> editor_row_boxes;

    auto confirm_editor = [&](int idx) {
        if (idx < 0 || idx >= (int)available_editors.size()) return;
        chosen_editor = available_editors[idx].cmd;
        phase = 3;
    };

    // ── Step 3: login form state ────────────────────────────────────────
    std::string session_text, csrf_text;
    int field_focus = 0;
    std::string login_error;

    InputOption input_opt;
    input_opt.multiline = false;
    input_opt.transform = [](InputState s) { return s.element | color(kText); };
    input_opt.on_change = [&] { login_error.clear(); };
    input_opt.on_enter = [&] {
        if (field_focus == 0) {
            field_focus = 1;
            return;
        }
        if (trim(session_text).empty() || trim(csrf_text).empty()) {
            login_error = "Both fields are required — this step can't be skipped.";
            return;
        }
        screen.Exit();
    };
    auto session_input = Input(&session_text, input_opt);
    auto csrf_input = Input(&csrf_text, input_opt);
    auto login_inputs = Container::Tab({session_input, csrf_input}, &field_focus);
    Box session_row_box, csrf_row_box;

    auto idle = Container::Vertical({});
    auto idle2 = Container::Vertical({});
    auto root_component = Container::Tab({idle, folder_name_input, idle2, login_inputs}, &phase);

    auto render = [&]() -> Element {
        if (phase == 0 || phase == 1) {
            Elements rows;
            entry_boxes.assign(entries.size(), Box{});
            for (int i = 0; i < (int)entries.size(); ++i) {
                bool hov = (i == hovered);
                bool is_up = (entries[i] == "..");
                std::string label = is_up ? ".. (up one level)" : entries[i] + "/";
                Element row = hbox({
                    hov ? text(" > ") | bold | color(Color::White) : text("   "),
                    text(label) | (is_up ? italic : bold) | color(hov ? Color::White : (is_up ? kMuted : kText)),
                });
                if (hov) row = row | bgcolor(kHoverBg);
                rows.push_back(row | reflect(entry_boxes[i]));
            }
            if (!dir_error.empty())
                rows.push_back(text("  ! " + dir_error) | color(kHard) | italic);
            else if (entries.empty())
                rows.push_back(text("  (no subfolders here)") | dim | color(kMuted) | italic);

            int visible = std::max(1, list_box.y_max - list_box.y_min + 1);
            int max_offset = std::max(0, (int)rows.size() - visible);
            list_offset = std::clamp(list_offset, 0, max_offset);
            Elements visible_rows(rows.begin() + list_offset, rows.end());

            Element action_area;
            if (phase == 0) {
                action_area = hbox({
                    text("  "),
                    text(" Create new folder here ") | bold | color(Color::Black) | bgcolor(kAccent)
                        | reflect(use_button_box),
                    text(" "),
                    kbd("s"),
                    text("   "),
                    kbd("↑↓", "browse"), kbd("Enter", "open"), kbd("q", "quit"),
                });
            } else {
                Element name_box = hbox({text(" "), folder_name_input->Render() | flex})
                    | border | color(kAccent);
                Elements name_rows = {
                    text("  New folder name, created inside:") | color(kMuted),
                    hbox({text("    "), paragraph(current_dir.string()) | color(kAccent) | flex}),
                    text(""),
                    hbox({text("  "), name_box | flex}),
                };
                if (!folder_name_error.empty())
                    name_rows.push_back(text("  ! " + folder_name_error) | color(kHard) | italic);
                name_rows.push_back(hbox({text("  "), kbd("Enter", "create"), kbd("Esc", "cancel")}));
                action_area = vbox(std::move(name_rows));
            }

            Element card = window(
                text(" " + current_dir.string() + " ") | bold | color(kAccent),
                vbox({
                    separatorEmpty(),
                    vbox(std::move(visible_rows)) | reflect(list_box) | size(HEIGHT, EQUAL, 12),
                    separatorEmpty(),
                    action_area,
                    separatorEmpty(),
                })
            ) | color(kBorder) | size(WIDTH, EQUAL, 78);

            return onboarding_frame("Step 1 of 3 — choose where to save your problems", card);
        }

        if (phase == 2) {
            Elements rows;
            editor_row_boxes.assign(available_editors.size(), Box{});
            for (int i = 0; i < (int)available_editors.size(); ++i) {
                bool hov = (i == editor_hovered);
                Element row = hbox({
                    hov ? text(" > ") | bold | color(Color::White) : text("   "),
                    text(available_editors[i].label) | (hov ? bold : dim) | color(hov ? Color::White : kText),
                });
                if (hov) row = row | bgcolor(kHoverBg);
                rows.push_back(row | reflect(editor_row_boxes[i]));
            }

            Element card = window(
                text(" Pick your editor ") | bold | color(kAccent),
                vbox({
                    separatorEmpty(),
                    text("  Used to open solution files ('e' in My Problems).") | color(kMuted),
                    separatorEmpty(),
                    vbox(std::move(rows)),
                    separatorEmpty(),
                    hbox({text("  "), kbd("↑↓", "browse"), kbd("Enter", "select"), kbd("q", "quit")}),
                    separatorEmpty(),
                })
            ) | color(kBorder) | size(WIDTH, EQUAL, 78);

            return onboarding_frame("Step 2 of 3 — pick your editor", card);
        }

        Element session_box = hbox({text(" "), session_input->Render() | flex})
            | border | color(field_focus == 0 ? kAccent : kBorder);
        Element csrf_box = hbox({text(" "), csrf_input->Render() | flex})
            | border | color(field_focus == 1 ? kAccent : kBorder);

        Element card = window(
            text(" Connect your LeetCode account ") | bold | color(kAccent),
            vbox({
                separatorEmpty(),
                paragraph("Open leetcode.com in your browser (make sure you're logged in), then "
                          "Dev Tools -> Application/Storage -> Cookies -> https://leetcode.com, "
                          "and copy these two cookie values.") | color(kMuted),
                separatorEmpty(),
                text(" LEETCODE_SESSION") | bold | color(kText),
                session_box | reflect(session_row_box),
                separatorEmpty(),
                text(" csrftoken") | bold | color(kText),
                csrf_box | reflect(csrf_row_box),
                separatorEmpty(),
                login_error.empty() ? text("") : text("  ! " + login_error) | color(kHard) | italic,
                hbox({text("  "), kbd("Tab", "switch field"), kbd("Enter", "continue")}),
                separatorEmpty(),
            })
        ) | color(kBorder) | size(WIDTH, EQUAL, 78);

        return onboarding_frame("Step 3 of 3 — connect your LeetCode account", card);
    };

    auto root = CatchEvent(Renderer(root_component, render), [&](Event e) -> bool {
        if (phase == 3) {
            if (e == Event::Tab) { field_focus = 1 - field_focus; login_error.clear(); return true; }
            if (e.is_mouse()) {
                const Mouse& m = e.mouse();
                if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
                    if (session_row_box.Contain(m.x, m.y)) { field_focus = 0; return true; }
                    if (csrf_row_box.Contain(m.x, m.y)) { field_focus = 1; return true; }
                }
                return true;
            }
            return false;  // let the focused Input handle typing/Enter
        }

        if (phase == 2) {
            if (e == Event::Character('q')) { aborted = true; screen.Exit(); return true; }
            if (e.is_mouse()) {
                const Mouse& m = e.mouse();
                if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
                    for (size_t i = 0; i < editor_row_boxes.size(); ++i)
                        if (editor_row_boxes[i].Contain(m.x, m.y)) { confirm_editor((int)i); return true; }
                }
                return true;
            }
            if (e == Event::ArrowUp || e == Event::Character('k')) {
                editor_hovered = std::clamp(editor_hovered - 1, 0, (int)available_editors.size() - 1);
                return true;
            }
            if (e == Event::ArrowDown || e == Event::Character('j')) {
                editor_hovered = std::clamp(editor_hovered + 1, 0, (int)available_editors.size() - 1);
                return true;
            }
            if (e == Event::Return) { confirm_editor(editor_hovered); return true; }
            return true;
        }

        if (phase == 1) {
            if (e == Event::Escape) { phase = 0; return true; }
            return false;  // let folder_name_input handle typing/Enter/clicks
        }

        // Phase 0: folder picker — plain key handling (no text field active).
        if (e.is_mouse()) {
            const Mouse& m = e.mouse();
            if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
                int dir = (m.button == Mouse::WheelUp) ? -1 : +1;
                int visible = std::max(1, list_box.y_max - list_box.y_min + 1);
                int max_offset = std::max(0, (int)entries.size() - visible);
                list_offset = std::clamp(list_offset + dir * 2, 0, max_offset);
                return true;
            }
            if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
                if (use_button_box.Contain(m.x, m.y)) { begin_create_folder(); return true; }
                for (size_t i = 0; i < entry_boxes.size(); ++i) {
                    if (entry_boxes[i].Contain(m.x, m.y)) { descend(entries[i]); return true; }
                }
            }
            return true;
        }

        if (e == Event::Character('q')) { aborted = true; screen.Exit(); return true; }
        if (e == Event::ArrowUp || e == Event::Character('k')) {
            hovered = std::clamp(hovered - 1, 0, std::max(0, (int)entries.size() - 1));
            return true;
        }
        if (e == Event::ArrowDown || e == Event::Character('j')) {
            hovered = std::clamp(hovered + 1, 0, std::max(0, (int)entries.size() - 1));
            return true;
        }
        if (e == Event::Return && !entries.empty()) { descend(entries[hovered]); return true; }
        if (e == Event::Character('s')) { begin_create_folder(); return true; }
        return true;
    });

    screen.Loop(root);

    // Only exit here, after Loop() has returned and ScreenInteractive has
    // already restored the terminal (raw mode, mouse tracking, ...) —
    // exiting mid-loop would skip that cleanup and leave the terminal broken.
    if (aborted) std::exit(0);

    write_initial_config(chosen_dir.empty() ? current_dir.string() : chosen_dir, "cpp");
    set_editor_preference(chosen_editor);
    save_session_tokens(session_text, csrf_text);
}

}  // namespace

void run_tui() {
    if (!is_leetcli_initialized()) run_onboarding_wizard();

    const std::string problems_dir = get_problems_dir();

    // ── My Problems state ──────────────────────────────────────────────────
    std::vector<MyProblem> my_all;
    std::vector<int> my_filtered;
    std::string my_search;
    std::string my_search_cache = "\x01";
    // Mirrors Browse All's hover/select split for interaction consistency,
    // even though there's no network cost here to avoid: my_hovered is the
    // arrow-key/mouse cursor (grey highlight), my_selected is the deliberate
    // choice (Enter or click) that drives the description panel and every
    // row-scoped action (edit/topics/hints/run/submit all key off
    // my_all[my_filtered[my_selected]], same as before).
    int my_hovered      = 0;
    int my_selected     = 0;
    int my_list_offset  = 0;

    // ── Browse All state ───────────────────────────────────────────────────
    std::vector<ProblemSummary> browse_problems;
    int browse_total    = 0;
    int browse_page     = 0;
    // browse_hovered is the list cursor (moves on every arrow key/mouse
    // move, like my_selected); browse_selected is a separate, deliberate
    // choice (Enter or click) that drives what's previewed on the right —
    // arrow-key browsing through a page of 100 problems shouldn't fire off
    // a network fetch per row. -1 = nothing selected yet.
    int browse_hovered  = 0;
    int browse_selected = -1;
    int browse_list_offset = 0;
    std::atomic<bool>   browse_loading{false};
    std::atomic<size_t> spinner_idx{0};
    std::string browse_error;
    std::string browse_keyword;
    std::string daily_slug;  // slug of the daily challenge pinned atop the browse list

    // In-memory-only preview of the selected Browse-All problem's
    // description — never written to disk. Cleared/replaced whenever
    // browse_selected changes; persists to My Problems only via an actual
    // fetch ('f').
    Elements browse_desc_cache;
    std::string browse_desc_cache_slug = "\x01";
    std::atomic<bool> browse_desc_loading{false};
    std::string browse_desc_error;
    int browse_desc_offset = 0;
    Box browse_desc_box;
    bool browse_is_search = false;
    // <img> placements found in browse_desc_cache/cached_desc, paired with
    // each the same way *_desc_cache_slug/cached_key are. deque (not vector)
    // so reflect()'s captured Box& references stay valid as more images are
    // appended mid-parse (see ImagePlacement's doc comment in utils.h).
    std::deque<ImagePlacement> browse_image_placements;
    std::deque<ImagePlacement> cached_image_placements;
    ImageCache image_cache;
    ImageProtocol img_protocol = (get_image_rendering_pref() == "off")
                                      ? ImageProtocol::None
                                      : detect_image_protocol();
    // Raw HTML each *_desc_cache was last built from, kept around so it can
    // be cheaply re-parsed once a pending image finishes loading — the
    // "[loading image]" placeholder baked into the first parse otherwise
    // never gets replaced, since nothing else re-triggers html_to_ftxui
    // outside of an actual selection change. images_ready_version bumps on
    // every completed fetch (see on_image_ready below); each tab tracks the
    // version it last rebuilt against and re-parses its stored HTML when
    // they drift apart.
    std::string cached_html;
    std::string browse_desc_html;
    std::atomic<int> images_ready_version{0};  // bumped from ImageCache's background fetch thread
    int my_images_version = -1;
    int browse_images_version = -1;
    // What's actually been written to the real terminal via a graphics
    // protocol right now, so draw_pending_images() only re-emits on change
    // instead of redrawing every frame.
    struct DrawnImage { uint32_t id; Box box; };
    std::vector<DrawnImage> drawn_images;

    int tab_selected = 0;
    int desc_offset  = 0;

    // Right-panel cache
    std::string cached_key = "\x01";
    Elements cached_desc;
    std::vector<std::string> cached_solution;
    std::string cached_solution_name;
    bool cached_has_solution = false;

    // "Start solution" prompt (shown in the solution panel in place of the
    // code view when the current problem has no solution file yet). Plain
    // Elements + manual mouse hit-testing, same reasoning as topics/hints.
    // start_lang_idx is the confirmed selection; while the dropdown is open,
    // start_lang_highlight tracks the row the user is arrowing over, and is
    // only committed to start_lang_idx on Enter/click.
    int start_lang_idx = 0;
    int start_lang_highlight = 0;
    bool start_lang_open = false;
    std::atomic<bool> start_loading{false};
    std::string start_error;
    Box start_lang_header_box;
    Box start_button_box;
    std::vector<Box> start_lang_row_boxes;

    // Solution panel tabs (Code / Run / Submit), shown once a solution file
    // exists. Run/Submit each hold a single cached result (like
    // topics/hints, not per-problem — switching problems clears them).
    int sol_tab = 0;  // 0 = Code, 1 = Run, 2 = Submit
    Box sol_tab_code_box, sol_tab_run_box, sol_tab_submit_box;

    // Contextual Actions menu (Space). The item list is rebuilt each frame
    // from the current state by build_actions (assigned once, further down,
    // after the action lambdas it calls exist). Declared here as a
    // std::function so the render lambda — defined before that assignment —
    // can still capture and call it; it's populated before the event loop runs.
    bool actions_open = false;
    int actions_highlight = 0;
    std::vector<Box> actions_row_boxes;
    std::function<std::vector<MenuAction>()> build_actions;

    std::atomic<bool> run_loading{false};
    bool run_has_result = false;
    RunResult cached_run_result;

    std::atomic<bool> submit_loading{false};
    bool submit_has_result = false;
    SubmitResult cached_submit_result;

    // Sync (download all solved + attempted problems). Runs on a background
    // thread; sync_problems' progress callback posts updates back to these
    // plain fields via screen.Post (so they're only ever touched on the UI
    // thread). sync_running gates re-entry and drives the progress banner.
    std::atomic<bool> sync_running{false};
    int sync_done = 0, sync_total = 0;
    std::string sync_current, sync_last, sync_error;

    // Topics / Hints (dropdown accordions, lazily loaded and cached to disk).
    // These are rendered as plain Elements (not ftxui Components) with
    // manual mouse hit-testing — see topics_row_box / hint_row_boxes below.
    // They deliberately avoid real ftxui Checkbox/Container components: those
    // are always the tree's "active/focused" child, and the description
    // panel's yframe auto-scrolls to keep the focused child visible, which
    // fought with the manual desc_offset scrolling and made the top of the
    // description unreachable.
    std::vector<std::string> cached_topics;
    std::vector<std::string> cached_hints;
    std::vector<bool> hint_open;
    bool topics_open   = false;
    bool topics_loaded = false;
    bool hints_loaded  = false;
    std::atomic<bool> meta_loading{false};
    std::string meta_error;

    // Mouse hit-testing: screen-space boxes captured during render via
    // reflect(), then checked against click/wheel coordinates in CatchEvent.
    Box tab_my_box, tab_browse_box;
    Box left_list_box, desc_box;
    Box my_items_box, browse_items_box;
    std::vector<Box> my_row_boxes;
    std::vector<Box> browse_row_boxes;
    Box topics_row_box;
    std::vector<Box> hint_row_boxes;

    // ── helpers ────────────────────────────────────────────────────────────
    auto load_my_problems = [&]() {
        my_all.clear();
        if (fs::exists(problems_dir))
            for (const auto& e : fs::directory_iterator(problems_dir)) {
                if (!e.is_directory()) continue;
                MyProblem mp{e.path().filename().string(), e.path().string(), "", "", false};
                auto slug_lines = read_file_lines(mp.folder + "/.slug");
                if (!slug_lines.empty()) mp.slug = slug_lines[0];
                auto difficulty_lines = read_file_lines(mp.folder + "/.difficulty");
                if (!difficulty_lines.empty()) mp.difficulty = difficulty_lines[0];
                // `.solved` is written on a successful submit; `.status`
                // (ac/notac) comes from `leetcli sync`. Either "ac" source
                // counts as solved; "notac" marks an attempt.
                auto status_lines = read_file_lines(mp.folder + "/.status");
                std::string status = status_lines.empty() ? "" : status_lines[0];
                mp.solved = fs::exists(mp.folder + "/.solved") || status == "ac";
                mp.attempted = (status == "notac") && !mp.solved;
                my_all.push_back(std::move(mp));
            }
        // Most recently worked on first (see last_worked_on), ties broken by
        // label so the order is still deterministic.
        std::sort(my_all.begin(), my_all.end(), [](const MyProblem& a, const MyProblem& b) {
            auto ta = last_worked_on(a.folder), tb = last_worked_on(b.folder);
            return ta != tb ? ta > tb : a.label < b.label;
        });
        my_search_cache = "\x01";
    };

    auto recompute_my = [&]() {
        my_filtered.clear();
        std::string q = to_lower(my_search);
        for (int i = 0; i < (int)my_all.size(); ++i)
            if (q.empty() || to_lower(my_all[i].label).find(q) != std::string::npos)
                my_filtered.push_back(i);
        my_selected = std::clamp(my_selected, 0, std::max(0, (int)my_filtered.size() - 1));
        my_hovered = std::clamp(my_hovered, 0, std::max(0, (int)my_filtered.size() - 1));
        my_list_offset = 0;
    };

    auto is_fetched = [&](const ProblemSummary& p) {
        for (const auto& m : my_all)
            if (m.label.rfind(p.id + ". ", 0) == 0) return true;
        return false;
    };

    // Commits the hover cursor as the deliberate selection — mirrors
    // select_browse_problem, minus the network fetch (My Problems only
    // reads local files, so there's nothing to fetch in the background).
    auto select_my_problem = [&](int idx) {
        if (idx < 0 || idx >= (int)my_filtered.size()) return;
        my_hovered = idx;
        my_selected = idx;
        desc_offset = 0;
    };

    load_my_problems();

    // ── screen ─────────────────────────────────────────────────────────────
    auto screen = ScreenInteractive::Fullscreen();

    // Wired into ImageCache::get_or_fetch as the "a decode finished"
    // callback (runs on the cache's background fetch thread). Bumping
    // images_ready_version tells render()'s version check (see below) to
    // re-parse the stored HTML on the next frame, swapping any
    // "[loading image]" placeholders for the now-cached real image;
    // PostEvent is what actually wakes the main loop to run that frame.
    auto on_image_ready = [&]() {
        ++images_ready_version;
        screen.PostEvent(Event::Custom);
    };

    // ftxui gives no hook to inject raw bytes into its own render/flush
    // cycle (see the plan doc for why), so this writes Kitty/iTerm2 escape
    // sequences to stdout directly, from the render() lambda itself — same
    // thread, called just before render() returns its Element tree, i.e.
    // strictly before ScreenInteractive::Draw() writes that tree's
    // ToString() to std::cout for the same frame. Same-thread + sequential
    // means no interleaving/corruption risk with ftxui's own output.
    //
    // Box positions come from reflect(), which (like everywhere else in this
    // file, see move_sel's comment) is one frame behind: a freshly-appeared
    // placeholder won't have a valid Box until the frame after it first
    // renders. That's fine here — this just skips it for one frame.
    //
    // A placement's Box is only updated when its Element actually gets
    // rendered — if it scrolls out of view, the description panel's
    // desc_offset slicing (see the "Refresh cache"/full_desc code) excludes
    // its Element from the tree entirely, so reflect() never runs on it that
    // frame and the Box would otherwise just keep its last real value
    // (stale, not cleared). render() resets every placement's Box to empty
    // right after calling this, before rebuilding the (possibly sliced)
    // tree, specifically so that "empty this frame" reliably means "not
    // currently visible" (whether scrolled out or not laid out yet) rather
    // than silently leaving a scrolled-past image floating on screen.
    auto draw_pending_images = [&]() {
        if (img_protocol == ImageProtocol::None) return;
        const auto& active = (tab_selected == 0) ? cached_image_placements : browse_image_placements;

        // Remove anything drawn that's no longer part of the active
        // description, or is part of it but isn't currently visible (tab
        // switch, problem switch, or scrolled out of view).
        for (auto it = drawn_images.begin(); it != drawn_images.end();) {
            auto found = std::find_if(active.begin(), active.end(),
                                       [&](const ImagePlacement& p) { return p.id == it->id; });
            bool still_visible = found != active.end() && !found->box.IsEmpty();
            if (still_visible) {
                ++it;
            } else {
                std::cout << build_delete_sequence(img_protocol, it->id);
                it = drawn_images.erase(it);
            }
        }

        for (const auto& p : active) {
            if (p.box.IsEmpty() || !p.decoded) continue;  // not visible this frame, or still loading

            auto drawn_it = std::find_if(drawn_images.begin(), drawn_images.end(),
                                          [&](const DrawnImage& d) { return d.id == p.id; });
            bool already_drawn_here = drawn_it != drawn_images.end() && drawn_it->box == p.box;
            if (already_drawn_here) continue;

            if (drawn_it != drawn_images.end()) std::cout << build_delete_sequence(img_protocol, p.id);

            // Refit against the box's *actual* post-layout size (which can
            // differ from the assumed width html_to_ftxui reserved it with)
            // — always preserves aspect ratio exactly by construction,
            // shrinking whichever dimension is the binding constraint,
            // rather than deriving one dimension and clamping it (which
            // silently distorts the image whenever the clamp kicks in).
            int box_columns = std::max(1, p.box.x_max - p.box.x_min + 1);
            int box_rows = std::max(1, p.box.y_max - p.box.y_min + 1);
            int columns, rows;
            fit_image_to_cells(p.decoded->width, p.decoded->height, box_columns, box_rows, columns, rows);
            std::cout << "\x1b[" << (p.box.y_min + 1) << ";" << (p.box.x_min + 1) << "H";
            std::cout << build_display_sequence(img_protocol, p.decoded->raw_bytes, p.decoded->rgba,
                                                 p.decoded->width, p.decoded->height, columns, rows, p.id);

            if (drawn_it != drawn_images.end()) *drawn_it = DrawnImage{p.id, p.box};
            else drawn_images.push_back(DrawnImage{p.id, p.box});
        }
    };

    // Opens the currently selected My-Problems solution file in $EDITOR.
    // Shared by the manual 'e' and Return keyboard shortcuts.
    auto open_current_in_editor = [&]() {
        if (my_filtered.empty()) return;
        std::string sol = find_solution_file(my_all[my_filtered[my_selected]].folder);
        if (!sol.empty()) {
            screen.WithRestoredIO([&] { launch_in_editor(sol); })();
            cached_key = "\x01";
        }
    };

    auto load_browse_page = [&](int page, const std::string& keyword) {
        if (browse_loading.load()) return;
        browse_loading = true;
        browse_error.clear();

        // Spinner animation: ticks every 80 ms until loading finishes.
        std::thread([&]() {
            while (browse_loading.load()) {
                ++spinner_idx;
                screen.PostEvent(Event::Custom);
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
        }).detach();

        // Fetch thread.
        std::thread([&, page, keyword]() {
            if (!has_leetcode_session()) {
                browse_loading = false;
                screen.Post([&]() {
                    browse_error = "Run `leetcli login` first to browse all problems.";
                });
                screen.PostEvent(Event::Custom);
                return;
            }
            ProblemPage result = fetch_problem_page(page * 100, keyword);
            // The daily challenge is pinned to the top of the unfiltered first
            // page only, so fetch it just for that case.
            ProblemSummary daily;
            if (page == 0 && keyword.empty()) daily = fetch_daily_problem();
            browse_loading = false;  // stops the spinner thread
            screen.Post([&, r = std::move(result), d = std::move(daily), page, keyword]() mutable {
                if (!r.error.empty()) {
                    browse_error = r.error;
                } else {
                    browse_problems  = std::move(r.problems);
                    browse_total     = r.total;
                    browse_page      = page;
                    browse_keyword   = keyword;
                    browse_is_search = !keyword.empty();
                    browse_hovered   = 0;
                    browse_selected  = -1;
                    browse_list_offset = 0;

                    // Pin today's daily challenge to the top of the first
                    // unfiltered page, removing any natural duplicate so it
                    // appears exactly once (marked with the calendar glyph).
                    if (page == 0 && keyword.empty() && !d.slug.empty()) {
                        daily_slug = d.slug;
                        browse_problems.erase(
                            std::remove_if(browse_problems.begin(), browse_problems.end(),
                                [&](const ProblemSummary& p) { return p.slug == d.slug; }),
                            browse_problems.end());
                        browse_problems.insert(browse_problems.begin(), d);
                    }
                    browse_desc_cache.clear();
                    browse_desc_cache_slug = "\x01";
                    browse_image_placements.clear();
                    browse_desc_html.clear();
                    browse_desc_error.clear();
                    browse_desc_offset = 0;
                }
                screen.PostEvent(Event::Custom);
            });
        }).detach();
    };

    // Commits to previewing a Browse-All row: moves both the hover cursor
    // and the deliberate selection there, and (if not already cached from a
    // previous visit) fetches its description in the background — nothing
    // is written to disk here, only fetch_problem() (via 'f') persists it.
    auto select_browse_problem = [&](int idx) {
        if (idx < 0 || idx >= (int)browse_problems.size()) return;
        browse_hovered = idx;
        browse_selected = idx;
        browse_desc_offset = 0;

        const std::string& slug = browse_problems[idx].slug;
        if (browse_desc_cache_slug == slug) return;  // already cached
        if (browse_desc_loading.load()) return;
        browse_desc_loading = true;
        browse_desc_error.clear();

        std::thread([&]() {
            while (browse_desc_loading.load()) {
                ++spinner_idx;
                screen.PostEvent(Event::Custom);
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
        }).detach();

        std::thread([&, slug]() {
            ProblemDescription result = fetch_problem_description(slug);
            browse_desc_loading = false;
            screen.Post([&, r = std::move(result), slug]() mutable {
                if (!r.error.empty()) {
                    browse_desc_error = r.error;
                    browse_desc_cache.clear();
                    browse_desc_cache_slug = "\x01";
                    browse_image_placements.clear();
                    browse_desc_html.clear();
                } else {
                    browse_image_placements.clear();
                    browse_desc_html = r.content_html;
                    browse_desc_cache = html_to_ftxui(browse_desc_html, image_cache, "",
                                                       on_image_ready, browse_image_placements);
                    browse_desc_cache_slug = slug;
                    browse_images_version = images_ready_version.load();
                }
                screen.PostEvent(Event::Custom);
            });
        }).detach();
    };

    // Lazily fetches topics+hints for a problem (used when the local
    // topics.txt/hints.txt cache doesn't exist yet) and caches the result
    // to disk so future selections load instantly.
    auto load_meta = [&](const std::string& folder, const std::string& slug,
                          bool open_topics, int open_hint_idx) {
        if (meta_loading.load() || slug.empty()) return;
        meta_loading = true;
        meta_error.clear();

        std::thread([&]() {
            while (meta_loading.load()) {
                ++spinner_idx;
                screen.PostEvent(Event::Custom);
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
        }).detach();

        std::thread([&, folder, slug, open_topics, open_hint_idx]() {
            TopicsHintsResult result = fetch_topics_and_hints(slug);
            meta_loading = false;
            screen.Post([&, r = std::move(result), folder, open_topics, open_hint_idx]() mutable {
                if (!r.error.empty()) {
                    meta_error = r.error;
                } else {
                    cached_topics = std::move(r.topics);
                    cached_hints  = std::move(r.hints);
                    hint_open.assign(cached_hints.size(), false);
                    topics_loaded = true;
                    hints_loaded  = true;
                    if (open_topics) topics_open = true;
                    if (open_hint_idx >= 0 && open_hint_idx < (int)hint_open.size())
                        hint_open[open_hint_idx] = true;
                    write_lines_file(folder + "/topics.txt", cached_topics);
                    write_lines_file(folder + "/hints.txt", cached_hints);
                }
                screen.PostEvent(Event::Custom);
            });
        }).detach();
    };

    // Creates solution.<ext> for the selected My-Problems entry in the
    // language currently picked in the "Start solution" prompt, then
    // refreshes the cached solution view in place (without disturbing
    // desc_offset/topics/hints state the way a full cache-key refresh would).
    auto start_solution_flow = [&]() {
        if (my_filtered.empty() || start_loading.load()) return;
        const auto& m = my_all[my_filtered[my_selected]];
        if (m.slug.empty()) {
            start_error = "Missing .slug file for this problem — re-fetch it first.";
            return;
        }
        start_loading = true;
        start_error.clear();

        std::thread([&]() {
            while (start_loading.load()) {
                ++spinner_idx;
                screen.PostEvent(Event::Custom);
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
        }).detach();

        std::string lang = kLangOptions[start_lang_idx].id;
        std::string ext  = kLangOptions[start_lang_idx].ext;
        std::thread([&, folder = m.folder, slug = m.slug, lang, ext]() {
            StartSolutionResult result = write_starter_solution(slug, folder, lang, ext);
            start_loading = false;
            screen.Post([&, r = std::move(result), folder]() mutable {
                if (!r.error.empty()) {
                    start_error = r.error;
                } else {
                    // Mirrors open_current_in_editor: hand the terminal to
                    // $EDITOR, then force a full cache refresh on return so
                    // any edits made there (and the new solution file itself)
                    // show up.
                    screen.WithRestoredIO([&] { launch_in_editor(r.solution_path); })();
                    cached_key = "\x01";
                }
                screen.PostEvent(Event::Custom);
            });
        }).detach();
    };

    // Deletes the current problem's solution file(s), returning it to the
    // "no solution yet" state (the Start-solution flow reappears). Confirmed
    // via CONFIRM_RESET before this runs. Leaves README/description/.status
    // etc. intact — only the solution code is removed.
    auto reset_current_solution = [&]() {
        if (my_filtered.empty() || !cached_has_solution) return;
        const std::string folder = my_all[my_filtered[my_selected]].folder;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(folder, ec)) {
            if (entry.path().filename().string().rfind("solution.", 0) == 0)
                fs::remove(entry.path(), ec);
        }
        cached_has_solution = false;
        cached_solution.clear();
        cached_solution_name = "(no solution file)";
        sol_tab = 0;
        run_has_result = false;
        submit_has_result = false;
        cached_key = "\x01";  // force the panel to reload from disk on next render
    };

    auto open_lang_dropdown = [&]() {
        start_lang_highlight = start_lang_idx;
        start_lang_open = true;
    };
    auto confirm_lang_highlight = [&]() {
        start_lang_idx = start_lang_highlight;
        start_lang_open = false;
    };

    // Joins the cached solution lines back into source text, and maps the
    // current solution file's extension back to a LeetCode langSlug — used
    // by Run/Submit, which act on whatever language is already on disk.
    auto current_solution_code = [&]() -> std::string {
        std::string code;
        for (size_t i = 0; i < cached_solution.size(); ++i) {
            if (i > 0) code += "\n";
            code += cached_solution[i];
        }
        return code;
    };
    auto current_lang_id = [&]() -> std::string {
        size_t dot = cached_solution_name.rfind('.');
        std::string ext = (dot == std::string::npos) ? "" : cached_solution_name.substr(dot);
        return lang_id_for_ext(ext);
    };

    auto run_solution_flow = [&]() {
        if (my_filtered.empty() || run_loading.load() || !cached_has_solution) return;
        const auto& m = my_all[my_filtered[my_selected]];
        if (m.slug.empty()) {
            cached_run_result = RunResult{};
            cached_run_result.error = "Missing .slug file for this problem — re-fetch it first.";
            run_has_result = true;
            return;
        }
        run_loading = true;

        std::thread([&]() {
            while (run_loading.load()) {
                ++spinner_idx;
                screen.PostEvent(Event::Custom);
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
        }).detach();

        std::string lang = current_lang_id();
        std::string code = current_solution_code();
        std::thread([&, folder = m.folder, slug = m.slug, lang, code]() {
            RunResult result = run_against_testcases(slug, folder, lang, code);
            run_loading = false;
            screen.Post([&, r = std::move(result)]() mutable {
                cached_run_result = std::move(r);
                run_has_result = true;
                screen.PostEvent(Event::Custom);
            });
        }).detach();
    };

    auto submit_solution_flow = [&]() {
        if (my_filtered.empty() || submit_loading.load() || !cached_has_solution) return;
        const auto& m = my_all[my_filtered[my_selected]];
        if (m.slug.empty()) {
            cached_submit_result = SubmitResult{};
            cached_submit_result.error = "Missing .slug file for this problem — re-fetch it first.";
            submit_has_result = true;
            return;
        }
        submit_loading = true;

        std::thread([&]() {
            while (submit_loading.load()) {
                ++spinner_idx;
                screen.PostEvent(Event::Custom);
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
        }).detach();

        std::string lang = current_lang_id();
        std::string code = current_solution_code();
        std::thread([&, folder = m.folder, slug = m.slug, lang, code]() {
            SubmitResult result = submit_and_poll(slug, folder, lang, code);
            submit_loading = false;
            screen.Post([&, r = std::move(result), folder]() mutable {
                if (r.accepted) {
                    write_lines_file(folder + "/.solved", {"1"});
                    for (auto& mp : my_all)
                        if (mp.folder == folder) mp.solved = true;
                }
                cached_submit_result = std::move(r);
                submit_has_result = true;
                screen.PostEvent(Event::Custom);
            });
        }).detach();
    };

    // Kicks off a full account sync on a background thread: downloads every
    // solved + attempted problem (folder + your submitted code) locally.
    // Progress posts back to the sync_* fields on the UI thread; on completion
    // the My Problems list is reloaded so the new folders appear.
    auto start_sync_flow = [&]() {
        if (sync_running.load()) return;
        sync_error.clear();
        if (!has_leetcode_session()) {
            sync_error = "Run `leetcli login` first to sync your problems.";
            screen.PostEvent(Event::Custom);
            return;
        }
        sync_running = true;
        sync_done = 0; sync_total = 0; sync_current.clear(); sync_last.clear();

        std::thread([&]() {
            sync_problems(0, [&](const SyncProgress& p) {
                screen.Post([&, snapshot = p]() {
                    ++spinner_idx;  // advance the banner spinner on each update
                    sync_done    = snapshot.done;
                    sync_total   = snapshot.total;
                    sync_current = snapshot.current;
                    sync_last    = snapshot.last_result;
                    if (!snapshot.error.empty()) sync_error = snapshot.error;
                    if (snapshot.finished) {
                        sync_running = false;
                        load_my_problems();
                        recompute_my();
                    }
                });
                screen.PostEvent(Event::Custom);
            });
        }).detach();
    };

    // Builds the "Topics (n)" header row and, when open, the chip row below
    // it. Plain Elements (not a Checkbox component) — see the note above
    // hint_open for why.
    auto render_topics_chips = [&]() -> Element {
        if (!meta_error.empty())
            return text("  ! " + meta_error) | color(kHard) | italic;
        if (meta_loading.load())
            return hbox({
                text("  "),
                spinner(15, spinner_idx.load()) | color(kAccent),
                text("  Loading…") | italic | color(kMuted),
            });
        if (cached_topics.empty())
            return text("      (none)") | italic | color(kMuted);
        Elements chips;
        for (const auto& t : cached_topics)
            chips.push_back(text(" " + t + " ") | color(Color::Black) | bgcolor(kAccent));
        return hbox({text("    "), flexbox(chips, FlexboxConfig().SetGap(1, 1)) | flex});
    };

    // Draws a compact bar chart of a runtime/memory distribution (LeetCode's
    // "beats X%" histogram), highlighting the bucket closest to your own
    // status_runtime/status_memory value ("4 ms", "14.9 MB", ...). Canvas
    // draw units are 1 wide x 2 tall per DrawBlockLine call ("block"
    // resolution), and the resulting Element renders at width/2 terminal
    // columns x height/4 terminal rows.
    //
    // Memory buckets come back in KB while status_memory is in MB (see the
    // note on DistributionPoint::bucket) — is_memory converts your_status's
    // value to KB before comparing so the right bar gets highlighted.
    auto render_distribution_chart = [&](const std::vector<DistributionPoint>& points,
                                          const std::string& your_status, bool is_memory) -> Element {
        if (points.empty()) return text("");

        double your_value = 0;
        { std::istringstream iss(your_status); iss >> your_value; }
        if (is_memory) your_value *= 1024;  // MB -> KB

        int highlight = 0;
        double best_diff = 1e18;
        for (int i = 0; i < (int)points.size(); ++i) {
            double v = 0;
            try { v = std::stod(points[i].bucket); } catch (...) {}
            double diff = std::fabs(v - your_value);
            if (diff < best_diff) { best_diff = diff; highlight = i; }
        }

        double max_pct = 0;
        for (const auto& p : points) max_pct = std::max(max_pct, p.percentage);
        if (max_pct <= 0) max_pct = 1;

        // display_cols is the chart's actual terminal-column width. It's at
        // least kMinCols so the label row below (constrained to the same
        // width so it lines up with the bars) always has room to fit
        // "<min> / You: ... / <max>" without truncating — with few
        // distribution points, n alone can be too narrow for that text.
        // Real points are spread proportionally across the wider canvas
        // when display_cols > n.
        const int bar_h = 16;  // 4 terminal rows
        const int n = (int)points.size();
        const int kMinCols = 32;
        const int display_cols = std::max(n, kMinCols);
        Canvas c(display_cols * 2, bar_h);
        for (int i = 0; i < n; ++i) {
            int units = std::max(1, (int)std::lround(points[i].percentage / max_pct * (bar_h - 1)));
            int x = (int)((long long)i * display_cols / n) * 2;
            int y_bottom = bar_h - 1;
            int y_top = y_bottom - units + 1;
            c.DrawBlockLine(x, y_bottom, x, y_top, i == highlight ? kAccent : kBorder);
        }

        // Constrain the label row to the chart's own width so the centered
        // "You: ..." label and the bucket labels line up under the bars
        // they describe, instead of stretching across the whole panel.
        std::string unit = is_memory ? " KB" : " ms";
        Element labels = hbox({
                              text(points.front().bucket + unit) | color(kMuted) | dim,
                              filler(),
                              text("You: " + your_status) | bold | color(kAccent),
                              filler(),
                              text(points.back().bucket + unit) | color(kMuted) | dim,
                          }) | size(WIDTH, EQUAL, display_cols);

        return hbox({
            text("  "),
            vbox({ftxui::canvas(c), labels}),
        });
    };

    // Renders the Solution panel's "Run" tab: idle prompt, spinner while in
    // flight, or the last run's per-testcase pass/fail breakdown.
    auto render_run_tab = [&]() -> Element {
        Elements rows;
        rows.push_back(text(""));
        if (run_loading.load()) {
            rows.push_back(hbox({
                text("  "),
                spinner(15, spinner_idx.load()) | color(kAccent),
                text("  Running testcases…") | italic | color(kMuted),
            }));
        } else if (!run_has_result) {
            rows.push_back(hbox({text("  "), kbd("r", "run against the example testcases")}));
        } else {
            const auto& res = cached_run_result;
            if (!res.error.empty()) {
                rows.push_back(text("  ! " + res.error) | color(kHard) | italic);
            } else if (!res.compile_error.empty()) {
                rows.push_back(text("  Compile Error") | bold | color(kHard));
                rows.push_back(hbox({text("  "), paragraph(oneline(res.compile_error)) | color(kText)}));
            } else if (!res.run_success) {
                rows.push_back(text("  " + res.status_msg) | bold | color(kHard));
                if (!res.runtime_error.empty())
                    rows.push_back(hbox({text("  "), paragraph(oneline(res.runtime_error)) | color(kText)}));
            } else {
                int passed = 0;
                for (const auto& tc : res.testcases) if (tc.passed) ++passed;
                bool all_pass = passed == (int)res.testcases.size();
                rows.push_back(hbox({
                    text("  "),
                    text(all_pass
                             ? "All testcases passed"
                             : std::to_string(passed) + " / " + std::to_string(res.testcases.size()) + " passed")
                        | bold | color(all_pass ? kEasy : kHard),
                }));
                rows.push_back(text(""));
                for (size_t i = 0; i < res.testcases.size(); ++i) {
                    const auto& tc = res.testcases[i];
                    rows.push_back(hbox({
                        text("  "),
                        text(tc.passed ? "✓ " : "✗ ") | bold | color(tc.passed ? kEasy : kHard),
                        text("Test " + std::to_string(i + 1)) | bold | color(kText),
                    }));
                    rows.push_back(hbox({text("    Input:    ") | color(kMuted), paragraph(oneline(tc.input)) | color(kText)}));
                    if (tc.passed) {
                        rows.push_back(hbox({text("    Output:   ") | color(kMuted), paragraph(oneline(tc.actual)) | color(kText)}));
                    } else {
                        rows.push_back(hbox({text("    Expected: ") | color(kMuted), paragraph(oneline(tc.expected)) | color(kText)}));
                        rows.push_back(hbox({text("    Got:      ") | color(kMuted), paragraph(oneline(tc.actual)) | color(kHard)}));
                    }
                    rows.push_back(text(""));
                }
            }
            rows.push_back(hbox({text("  "), kbd("r", "run again")}));
        }
        rows.push_back(text(""));
        return vbox(std::move(rows)) | yframe | size(HEIGHT, LESS_THAN, 16);
    };

    // Renders the Solution panel's "Submit" tab: idle prompt, spinner, or
    // the last submission's verdict + (if Accepted) the runtime/memory
    // percentile and distribution charts.
    auto render_submit_tab = [&]() -> Element {
        Elements rows;
        rows.push_back(text(""));
        if (submit_loading.load()) {
            rows.push_back(hbox({
                text("  "),
                spinner(15, spinner_idx.load()) | color(kAccent),
                text("  Submitting…") | italic | color(kMuted),
            }));
        } else if (!submit_has_result) {
            rows.push_back(hbox({text("  "), kbd("s", "submit for judging")}));
        } else {
            const auto& res = cached_submit_result;
            if (!res.error.empty()) {
                rows.push_back(text("  ! " + res.error) | color(kHard) | italic);
            } else if (!res.compile_error.empty()) {
                rows.push_back(text("  Compile Error") | bold | color(kHard));
                rows.push_back(hbox({text("  "), paragraph(oneline(res.compile_error)) | color(kText)}));
            } else {
                rows.push_back(hbox({
                    text("  "),
                    text(res.accepted ? "✓ Accepted" : "✗ " + res.status_msg)
                        | bold | color(res.accepted ? kEasy : kHard),
                }));
                if (res.accepted) {
                    rows.push_back(hbox({
                        text("  Runtime  ") | color(kMuted),
                        text(res.status_runtime) | bold | color(kText),
                        res.runtime_percentile >= 0
                            ? text("  (beats " + fmt1(res.runtime_percentile) + "%)") | color(kAccent)
                            : text(""),
                    }));
                    rows.push_back(hbox({
                        text("  Memory   ") | color(kMuted),
                        text(res.status_memory) | bold | color(kText),
                        res.memory_percentile >= 0
                            ? text("  (beats " + fmt1(res.memory_percentile) + "%)") | color(kAccent)
                            : text(""),
                    }));
                    if (!res.runtime_distribution.empty()) {
                        rows.push_back(text(""));
                        rows.push_back(text("  Runtime distribution") | bold | color(kText));
                        rows.push_back(render_distribution_chart(res.runtime_distribution, res.status_runtime, false));
                    }
                    if (!res.memory_distribution.empty()) {
                        rows.push_back(text(""));
                        rows.push_back(text("  Memory distribution") | bold | color(kText));
                        rows.push_back(render_distribution_chart(res.memory_distribution, res.status_memory, true));
                    }
                } else {
                    if (res.total_testcases > 0)
                        rows.push_back(hbox({
                            text("  Testcases  ") | color(kMuted),
                            text(std::to_string(res.total_correct) + " / " + std::to_string(res.total_testcases) + " passed")
                                | color(kText),
                        }));
                    if (!res.input_formatted.empty())
                        rows.push_back(hbox({text("  Input:    ") | color(kMuted), paragraph(oneline(res.input_formatted)) | color(kText)}));
                    if (!res.expected_output.empty())
                        rows.push_back(hbox({text("  Expected: ") | color(kMuted), paragraph(oneline(res.expected_output)) | color(kText)}));
                    if (!res.code_output.empty())
                        rows.push_back(hbox({text("  Got:      ") | color(kMuted), paragraph(oneline(res.code_output)) | color(kHard)}));
                }
            }
            rows.push_back(text(""));
            rows.push_back(hbox({text("  "), kbd("s", "submit again")}));
        }
        rows.push_back(text(""));
        return vbox(std::move(rows)) | yframe | size(HEIGHT, LESS_THAN, 20);
    };

    // ── input widgets ──────────────────────────────────────────────────────
    enum Mode { NAV, SEARCH, FETCH, CONFIRM_RESET, HELP };
    Mode mode = NAV;
    std::string search_text;
    std::string fetch_slug;
    int input_focus = 0;

    auto input_style = [](InputState s) -> Element {
        return s.element | color(kText);
    };

    InputOption search_opt;
    search_opt.multiline  = false;
    search_opt.transform  = input_style;
    // Real Input on_enter: applies the My-Problems filter or triggers a
    // Browse-All keyword search, matching whichever tab is active.
    search_opt.on_enter = [&] {
        if (tab_selected == 0) {
            my_search = search_text;
        } else {
            load_browse_page(0, trim(search_text));
        }
        mode = NAV;
    };

    InputOption fetch_opt;
    fetch_opt.multiline  = false;
    fetch_opt.transform  = input_style;
    // Real Input on_enter: fetches the typed slug (or "daily") and jumps
    // back to My Problems, replacing the old manual Return interception.
    fetch_opt.on_enter = [&] {
        std::string slug = trim(fetch_slug);
        if (!slug.empty()) {
            if (slug == "daily") slug = get_daily_question_slug();
            if (!slug.empty()) fetch_problem(slug, "");
            load_my_problems();
            recompute_my();
        }
        fetch_slug.clear();
        mode = NAV;
    };

    auto search_input = Input(&search_text, search_opt);
    auto fetch_input  = Input(&fetch_slug, fetch_opt);
    auto inputs = Container::Tab({search_input, fetch_input}, &input_focus);

    // Routes events to the active text input only while actually editing one
    // (mode == SEARCH/FETCH); otherwise routes to an inert placeholder so
    // Input doesn't swallow navigation keys. Topics/hints and the problem
    // list are all hand-rolled (manual mouse hit-testing in CatchEvent), so
    // they don't need a slot here.
    auto nav_idle = Container::Vertical({});
    int root_pane = 0;
    auto root_tabs = Container::Tab({nav_idle, inputs}, &root_pane);

    // ── render ─────────────────────────────────────────────────────────────
    auto render = [&]() -> Element {
        // Lazy filter for My Problems
        if (tab_selected == 0 && my_search != my_search_cache) {
            recompute_my();
            my_search_cache = my_search;
        }

        // A background image fetch completed since the last time each tab's
        // description was parsed — re-parse its stored HTML so the
        // now-cached image replaces its "[loading image]" placeholder.
        // Nothing else re-triggers html_to_ftxui just because a fetch
        // finished (only an actual selection change does), so without this
        // the placeholder would stick around until the user navigates away
        // and back.
        bool reparsed_for_images = false;
        if (my_images_version != images_ready_version.load() && !cached_html.empty()) {
            cached_image_placements.clear();
            cached_desc = html_to_ftxui(cached_html, image_cache, cached_key + "/images",
                                         on_image_ready, cached_image_placements);
            my_images_version = images_ready_version.load();
            reparsed_for_images = true;
        }
        if (browse_images_version != images_ready_version.load() && !browse_desc_html.empty()) {
            browse_image_placements.clear();
            browse_desc_cache = html_to_ftxui(browse_desc_html, image_cache, "",
                                               on_image_ready, browse_image_placements);
            browse_images_version = images_ready_version.load();
            reparsed_for_images = true;
        }

        // A just-completed fetch means one or more placements now have a
        // decoded image but no valid Box yet — reflect() only fills the Box
        // during *this* frame's layout pass, which draw_pending_images()
        // (running below, before layout) can't see. Force one more frame so
        // the now-positioned image actually gets emitted; without it the
        // image stays invisible until the next unrelated input event redraws
        // the screen ("images don't load until I press something").
        if (reparsed_for_images) screen.PostEvent(Event::Custom);

        // Re-emit any Kitty/iTerm2 images whose on-screen position changed
        // since last drawn (see draw_pending_images's own comment for why
        // this has to happen here, on the main thread, rather than via a
        // dedicated hook ftxui doesn't provide).
        draw_pending_images();

        // Clear every placement's Box now that draw_pending_images has read
        // it — anything actually visible in the tree built below will get a
        // fresh Box from reflect() during this frame's layout pass;
        // anything scrolled out or otherwise excluded will correctly stay
        // empty instead of keeping a stale position (see
        // draw_pending_images's doc comment for why this matters).
        for (auto& p : cached_image_placements) p.box = unset_image_box();
        for (auto& p : browse_image_placements) p.box = unset_image_box();

        // ── Header ──
        auto tab_btn = [&](const std::string& label, bool active) -> Element {
            if (active)
                return text(" " + label + " ")
                    | bold | color(Color::Black) | bgcolor(kAccent);
            return text(" " + label + " ") | color(kMuted);
        };

        Element header = hbox({
            text("  * ") | color(kMedium) | bold,
            text("leet") | bold | color(kAccent),
            text("cli") | bold | color(kText),
            text("  "),
            filler(),
            tab_btn("My Problems", tab_selected == 0) | reflect(tab_my_box),
            text("  "),
            tab_btn("Browse All", tab_selected == 1) | reflect(tab_browse_box),
            text("  "),
        });

        // ── Left panel ──
        Element left;
        if (tab_selected == 0) {
            // Search bar
            Element search_bar;
            if (mode == SEARCH) {
                search_bar = hbox({
                    text(" /") | color(kAccent),
                    search_input->Render() | flex,
                }) | color(kText);
            } else if (my_search.empty()) {
                search_bar = hbox({
                    text(" /") | color(kMuted),
                    text("filter...") | italic | color(kMuted),
                });
            } else {
                search_bar = hbox({
                    text(" /") | color(kAccent),
                    text(my_search) | color(kAccent),
                    text("  x") | color(kMuted) | dim,
                });
            }

            // Problem list — same visual scheme as Browse All's rows (white
            // bg + black text when selected, muted/dim otherwise, plus a
            // difficulty badge). No separate hover/select split here: unlike
            // Browse All, moving my_selected only reads local files, so
            // there's no network cost to treat "cursor" and "selected" as
            // the same thing. No "+" fetched-indicator either — every row
            // here is inherently already fetched, so it'd carry no info.
            Elements items;
            my_row_boxes.assign(my_filtered.size(), Box{});
            for (int vis = 0; vis < (int)my_filtered.size(); ++vis) {
                bool hovered = (vis == my_hovered);
                bool chosen = (vis == my_selected);
                const auto& mp = my_all[my_filtered[vis]];
                Color row_color = chosen ? Color::Black : (hovered ? Color::White : kMuted);
                Element row = hbox({
                    hovered ? text(" >") | bold | color(row_color) : text("   "),
                    mp.solved    ? text("✓") | bold | color(kFetched)
                    : mp.attempted ? text("•") | bold | color(kMuted)
                                   : text(" "),
                    text(" " + mp.label) | (hovered || chosen ? bold : dim) | color(row_color) | flex,
                    text(" "),
                    text(mp.difficulty.empty() ? " " : mp.difficulty.substr(0, 1))
                        | bold | color(mp.difficulty.empty() ? kMuted : diff_color(mp.difficulty)),
                    text(" "),
                });
                if (chosen) row = row | bgcolor(Color::White);
                else if (hovered) row = row | bgcolor(kHoverBg);
                items.push_back(row | reflect(my_row_boxes[vis]));
            }
            if (my_filtered.empty()) {
                items.push_back(
                    text(my_all.empty() ? "  No problems yet." : "  No matches.") | italic | color(kMuted));
            }

            // Manual scroll: slice the rendered items by my_list_offset so mouse
            // wheel scrolls the viewport independently of which row is selected.
            {
                int visible = std::max(1, my_items_box.y_max - my_items_box.y_min + 1);
                int max_offset = std::max(0, (int)items.size() - visible);
                my_list_offset = std::clamp(my_list_offset, 0, max_offset);
            }
            Elements visible_items(items.begin() + my_list_offset, items.end());

            std::string count_str = " " + std::to_string(my_all.size()) + " problem"
                + (my_all.size() != 1 ? "s" : "") + " ";

            Elements panel_rows;
            panel_rows.push_back(search_bar);
            panel_rows.push_back(separatorStyled(LIGHT) | color(kBorder));

            // Sync progress / error banner (only while a sync is running or
            // right after one failed to start).
            if (sync_running.load() || !sync_error.empty()) {
                Element banner;
                if (!sync_error.empty() && !sync_running.load()) {
                    banner = text("  ! " + sync_error) | color(kHard) | italic;
                } else {
                    std::string frac = sync_total > 0
                        ? std::to_string(sync_done) + "/" + std::to_string(sync_total)
                        : "listing…";
                    std::string detail = sync_current.empty() ? sync_last : sync_current;
                    banner = vbox({
                        hbox({
                            spinner(15, spinner_idx.load()) | color(kAccent),
                            text("  Syncing " + frac) | bold | color(kAccent),
                        }),
                        text("  " + detail) | color(kMuted) | dim,
                    });
                }
                panel_rows.push_back(banner);
                panel_rows.push_back(separatorStyled(LIGHT) | color(kBorder));
            }

            panel_rows.push_back(vbox(std::move(visible_items)) | reflect(my_items_box) | flex);

            left = window(
                hbox({text(count_str) | bold | color(kAccent)}),
                vbox(std::move(panel_rows))
            ) | color(kBorder) | reflect(left_list_box);

        } else {
            // Browse search bar
            Element search_bar;
            if (mode == SEARCH) {
                search_bar = hbox({
                    text(" /") | color(kAccent),
                    search_input->Render() | flex,
                    text(" [Enter] ") | color(kMuted) | dim,
                });
            } else if (browse_is_search) {
                search_bar = hbox({
                    text(" /") | color(kAccent),
                    text(browse_keyword) | color(kAccent) | bold,
                    text("  [Esc] clear") | color(kMuted) | dim,
                });
            } else {
                search_bar = hbox({
                    text(" /") | color(kMuted),
                    text("search + Enter") | italic | color(kMuted),
                });
            }

            // Browse list
            Elements items;
            if (browse_loading.load()) {
                browse_row_boxes.clear();
                items.push_back(hbox({
                    spinner(15, spinner_idx.load()) | color(kAccent),
                    text("  Loading…") | italic | color(kMuted),
                }));
            } else if (!browse_error.empty()) {
                browse_row_boxes.clear();
                items.push_back(text("  ! " + browse_error) | color(kHard) | italic);
            } else {
                browse_row_boxes.assign(browse_problems.size(), Box{});
                for (int i = 0; i < (int)browse_problems.size(); ++i) {
                    bool hovered = (i == browse_hovered);
                    bool chosen = (i == browse_selected);
                    const auto& p = browse_problems[i];
                    bool fetched = is_fetched(p);
                    bool is_daily = !daily_slug.empty() && p.slug == daily_slug;

                    // Selection (whichever row is actually previewed on the
                    // right) gets a white background + black text — takes
                    // precedence when a row is both selected and hovered.
                    // Hover-only (arrow-key cursor elsewhere) gets a
                    // dark-grey background + white text instead.
                    Color row_color = chosen ? Color::Black : (hovered ? Color::White : kMuted);

                    Element row = hbox({
                        hovered ? text(" >") | bold | color(row_color) : text("   "),
                        fetched ? text("+") | bold | color(kFetched) : text(" "),
                        is_daily ? text(" " + kDailyIcon) | bold | color(chosen ? Color::Black : kAccent)
                                 : text("  "),
                        text(" " + p.id + ". ") | color(row_color) | (hovered || chosen ? bold : dim),
                        text(p.title) | (hovered || chosen ? bold : dim) | color(row_color) | flex,
                        text(" "),
                        text(p.difficulty.empty() ? " " : p.difficulty.substr(0, 1))
                            | bold | color(diff_color(p.difficulty)),
                        text(" "),
                    });
                    if (chosen) row = row | bgcolor(Color::White);
                    else if (hovered) row = row | bgcolor(kHoverBg);
                    items.push_back(row | reflect(browse_row_boxes[i]));
                }
                if (items.empty())
                    items.push_back(text("  No results.") | italic | color(kMuted));
            }

            // Manual scroll: slice the rendered items by browse_list_offset so
            // mouse wheel scrolls the viewport independently of the selection.
            {
                int visible = std::max(1, browse_items_box.y_max - browse_items_box.y_min + 1);
                int max_offset = std::max(0, (int)items.size() - visible);
                browse_list_offset = std::clamp(browse_list_offset, 0, max_offset);
            }
            Elements visible_items(items.begin() + browse_list_offset, items.end());

            // Pagination info
            std::string page_info;
            if (!browse_loading.load() && browse_total > 0) {
                if (browse_is_search)
                    page_info = " " + std::to_string(browse_total) + " results";
                else {
                    int total_pages = (browse_total + 99) / 100;
                    page_info = " Page " + std::to_string(browse_page + 1)
                        + " / " + std::to_string(total_pages);
                }
            }

            Element list_win_title;
            if (browse_loading.load()) {
                list_win_title = hbox({
                    spinner(15, spinner_idx.load()) | color(kMuted),
                    text(" Browse All") | color(kMuted) | italic,
                });
            } else {
                list_win_title = hbox({
                    text(" Browse All ") | bold | color(kAccent),
                    text(page_info) | color(kMuted) | dim,
                });
            }

            left = window(
                list_win_title,
                vbox({
                    search_bar,
                    separatorStyled(LIGHT) | color(kBorder),
                    vbox(std::move(visible_items)) | reflect(browse_items_box) | flex,
                    browse_total > 0 && !browse_is_search
                        ? hbox({kbd("←"), text(" ") | dim, kbd("→"), text(" page") | color(kMuted)}) | dim
                        : text(""),
                })
            ) | color(kBorder) | reflect(left_list_box);
        }

        // ── Right panel ──
        Element right;
        if (tab_selected == 0) {
            std::string folder, slug, prob_title = "Select a problem", prob_difficulty;
            if (!my_filtered.empty()) {
                const auto& m = my_all[my_filtered[my_selected]];
                prob_title = m.label;
                folder = m.folder;
                slug = m.slug;
                prob_difficulty = m.difficulty;
            }

            // Refresh cache on selection change
            if (folder != cached_key) {
                cached_key = folder;
                std::string html_path = folder + "/description.html";
                cached_image_placements.clear();
                if (fs::exists(html_path)) {
                    cached_html = read_file(html_path);
                    cached_desc = html_to_ftxui(cached_html, image_cache, folder + "/images",
                                                 on_image_ready, cached_image_placements);
                } else {
                    cached_html.clear();
                    cached_desc = lines_to_paragraphs(read_file_lines(folder + "/README.md"));
                }
                my_images_version = images_ready_version.load();
                std::string sol = find_solution_file(folder);
                cached_solution_name = sol.empty()
                    ? "(no solution file)" : fs::path(sol).filename().string();
                cached_solution = sol.empty()
                    ? std::vector<std::string>{}
                    : read_file_lines(sol);
                cached_has_solution = !sol.empty();
                desc_offset = 0;

                start_lang_idx = lang_option_index(get_preferred_language());
                start_lang_open = false;
                start_error.clear();

                sol_tab = 0;
                run_has_result = false;
                submit_has_result = false;

                topics_open = false;
                cached_topics.clear();
                cached_hints.clear();
                hint_open.clear();
                topics_loaded = false;
                hints_loaded  = false;
                meta_error.clear();
                if (fs::exists(folder + "/topics.txt")) {
                    cached_topics = read_file_lines(folder + "/topics.txt");
                    topics_loaded = true;
                }
                if (fs::exists(folder + "/hints.txt")) {
                    cached_hints = read_file_lines(folder + "/hints.txt");
                    hint_open.assign(cached_hints.size(), false);
                    hints_loaded = true;
                }
            }

            // Topics and Hints are plain Elements with manual mouse
            // hit-testing (topics_row_box / hint_row_boxes), not real ftxui
            // components — see the note above hint_open for why.
            topics_row_box = Box{};
            hint_row_boxes.assign(cached_hints.size(), Box{});

            Elements meta;
            meta.push_back(text(""));
            meta.push_back(separatorEmpty());
            if (slug.empty() && !topics_loaded && !hints_loaded) {
                meta.push_back(text("  (re-fetch this problem to load topics/hints)") | dim | color(kMuted) | italic);
            } else {
                std::string topics_label = "Topics (" + std::to_string(cached_topics.size()) + ")";
                Element topics_header = hbox({
                    text("  "),
                    text(topics_open ? "v " : "> ") | bold | color(kAccent),
                    text(topics_label) | bold | color(kText),
                });
                meta.push_back(topics_header | reflect(topics_row_box));
                if (topics_open) meta.push_back(render_topics_chips());
                meta.push_back(text(""));

                if (meta_loading.load()) {
                    meta.push_back(hbox({
                        text("  "),
                        spinner(15, spinner_idx.load()) | color(kAccent),
                        text("  Loading hints…") | italic | color(kMuted),
                    }));
                } else if (!meta_error.empty()) {
                    // Error already surfaced once inside the topics panel above.
                } else if (!hints_loaded) {
                    meta.push_back(hbox({text("  "), kbd("1-9"), text(" hints") | color(kMuted)}));
                } else {
                    meta.push_back(hbox({
                        text("  "),
                        text("Hints (" + std::to_string(cached_hints.size()) + ")") | bold | color(kText),
                    }));
                    if (cached_hints.empty()) {
                        meta.push_back(text("    (none)") | italic | color(kMuted));
                    } else {
                        for (size_t i = 0; i < cached_hints.size(); ++i) {
                            bool open = hint_open[i];
                            Element header = hbox({
                                text("    "),
                                text(open ? "v " : "> ") | bold | color(kAccent),
                                text("Hint " + std::to_string(i + 1)) | (open ? bold : dim) | color(kText),
                            });
                            meta.push_back(header | reflect(hint_row_boxes[i]));
                            if (open)
                                meta.push_back(hbox({
                                    text("      "),
                                    paragraph(cached_hints[i]) | color(kText) | flex,
                                }));
                        }
                    }
                }
            }

            Elements full_desc = cached_desc;
            full_desc.insert(full_desc.end(), meta.begin(), meta.end());
            desc_offset = std::clamp(desc_offset, 0,
                                     std::max(0, (int)full_desc.size() - 1));

            // Description panel — title bar shows "{id}. {title}" + a
            // difficulty badge, exactly like Browse All's panel, instead of
            // repeating the title as its own line inside the content.
            Element title_el = my_filtered.empty()
                ? text(" Select a problem ") | bold | color(kMuted)
                : hbox({
                    text(" " + prob_title + " ") | bold | color(kText),
                    filler(),
                    prob_difficulty.empty() ? text("") : diff_badge(prob_difficulty),
                    text(" "),
                  });

            Elements desc_lines(full_desc.begin() + desc_offset, full_desc.end());
            Element desc_panel = window(
                title_el,
                vbox({
                    text(""),
                    hbox({
                        text("  "),
                        vbox(std::move(desc_lines)) | yframe | flex,
                        text("  "),
                    }) | flex,
                    text(""),
                })
            ) | color(kBorder) | flex | reflect(desc_box);

            Element sol_panel;
            if (!cached_has_solution && !my_filtered.empty()) {
                // "Start solution" prompt: a plain-Element dropdown + button
                // with manual mouse hit-testing, not real ftxui components —
                // same reasoning as topics/hints (see the note above
                // hint_open): real focusable components fight the
                // description panel's yframe auto-scroll.
                start_lang_header_box = Box{};
                start_button_box = Box{};

                Element lang_header = hbox({
                    text(" " + kLangOptions[start_lang_idx].label + " ") | bold | color(kText),
                    text(start_lang_open ? " ▲ " : " ▼ ") | color(kAccent),
                }) | bgcolor(kBgPanel);
                lang_header = lang_header | reflect(start_lang_header_box);

                Elements rows;
                rows.push_back(text(""));
                rows.push_back(hbox({
                    text("   Language   "), lang_header,
                    text(start_lang_open ? "" : "   ") ,
                    start_lang_open ? text("") : kbd("l", "change"),
                }));

                if (start_lang_open) {
                    start_lang_row_boxes.assign(kLangOptionCount, Box{});
                    for (int i = 0; i < kLangOptionCount; ++i) {
                        bool hi = (i == start_lang_highlight);
                        Element row = hbox({
                            text("      "),
                            text(hi ? "> " : "  ") | bold | color(kAccent),
                            text(kLangOptions[i].label) | (hi ? bold : dim)
                                | color(hi ? kAccent : kMuted),
                        });
                        rows.push_back(row | reflect(start_lang_row_boxes[i]));
                    }
                } else {
                    start_lang_row_boxes.clear();
                }

                rows.push_back(text(""));
                if (start_loading.load()) {
                    rows.push_back(hbox({
                        text("   "),
                        spinner(15, spinner_idx.load()) | color(kAccent),
                        text("  Creating solution file…") | italic | color(kMuted),
                    }));
                } else {
                    if (!start_error.empty())
                        rows.push_back(text("   ! " + start_error) | color(kHard) | italic);
                    Element start_btn = text(" Start Solving ")
                        | bold | color(Color::Black) | bgcolor(kAccent)
                        | reflect(start_button_box);
                    rows.push_back(hbox({text("   "), start_btn}));
                }
                rows.push_back(text(""));

                sol_panel = window(
                    text(" Solution ") | bold | color(kAccent),
                    vbox(std::move(rows))
                ) | color(kBorder);
            } else {
                // Code / Run / Submit tab bar, hand-rolled the same way as
                // the My-Problems/Browse-All tabs (reflect() boxes checked
                // in CatchEvent) rather than a real ftxui component — see
                // the note above hint_open for why real focusable
                // components don't mix well with this panel's yframe.
                sol_tab_code_box = Box{};
                sol_tab_run_box = Box{};
                sol_tab_submit_box = Box{};
                auto sol_tab_btn = [&](const std::string& label, bool active, Box& box) -> Element {
                    Element e = text(" " + label + " ")
                        | (active ? bold : dim)
                        | color(active ? Color::Black : kMuted)
                        | (active ? bgcolor(kAccent) : bgcolor(kBgPanel));
                    return e | reflect(box);
                };
                Element tabs = hbox({
                    sol_tab_btn("Code (c)", sol_tab == 0, sol_tab_code_box),
                    text(" "),
                    sol_tab_btn("Run (r)", sol_tab == 1, sol_tab_run_box),
                    text(" "),
                    sol_tab_btn("Submit (s)", sol_tab == 2, sol_tab_submit_box),
                });

                Element sol_content;
                if (sol_tab == 0) {
                    Elements sol_rows;
                    sol_rows.push_back(text(" " + cached_solution_name + " ") | dim | color(Color::RGB(150, 210, 255)));
                    for (int i = 0; i < (int)cached_solution.size(); ++i) {
                        std::string lnum = std::to_string(i + 1);
                        // pad to 3 chars
                        while ((int)lnum.size() < 3) lnum = " " + lnum;
                        sol_rows.push_back(hbox({
                            text(lnum + " ") | color(kMuted) | dim,
                            text(cached_solution[i]) | color(kText),
                        }));
                    }
                    if (cached_solution.empty())
                        sol_rows.push_back(text("  (empty)") | italic | color(kMuted));
                    sol_content = vbox(std::move(sol_rows)) | yframe | size(HEIGHT, LESS_THAN, 14);
                } else if (sol_tab == 1) {
                    sol_content = render_run_tab();
                } else {
                    sol_content = render_submit_tab();
                }

                sol_panel = window(tabs, sol_content) | color(kBorder);
            }

            right = vbox({desc_panel, sol_panel});

        } else {
            // Browse right panel
            if (!browse_error.empty()) {
                right = window(
                    text(" Error ") | bold | color(kHard),
                    vbox({
                        text("") ,
                        text("  ! " + browse_error) | color(kHard),
                        text("") ,
                    })
                ) | color(kHard);
            } else if (browse_loading.load() || browse_problems.empty()) {
                right = window(
                    text(" Browse All ") | bold | color(kMuted),
                    vbox({
                        text(""),
                        hbox({
                            text("  "),
                            spinner(15, spinner_idx.load()) | color(kAccent),
                            text("  Fetching problem list…") | italic | color(kMuted),
                        }),
                        text(""),
                    })
                ) | color(kBorder);
            } else if (browse_selected < 0) {
                right = window(
                    text(" Browse All ") | bold | color(kMuted),
                    vbox({
                        text(""),
                        hbox({
                            text("  "),
                            kbd("Enter"), text(" or click a row to preview its description") | color(kMuted),
                        }),
                        text(""),
                    })
                ) | color(kBorder) | flex;
            } else {
                const auto& p = browse_problems[browse_selected];
                bool fetched = is_fetched(p);

                Element title_el = hbox({
                    text(" " + p.id + ". " + p.title + " ") | bold | color(kText),
                    filler(),
                    diff_badge(p.difficulty),
                    text(" "),
                });

                Element action_hint = fetched
                    ? hbox({
                        text("  +  ") | bold | color(kFetched),
                        text("Already fetched — open ") | color(kMuted),
                        text("My Problems") | bold | color(kAccent),
                        text(" tab to edit.") | color(kMuted),
                      })
                    : hbox({
                        text("  ") ,
                        text("Press ") | color(kMuted),
                        text("f") | bold | color(kAccent),
                        text(" to fetch this problem into My Problems.") | color(kMuted),
                      });

                // Full rendered description, in memory only — nothing is
                // written to disk unless 'f' is pressed. Same manual
                // desc_offset slicing (not yframe) as My Problems' panel;
                // this content has no focusable sub-widgets so it'd be safe
                // either way, but keeping the mechanism consistent means
                // PageUp/Dn and wheel scroll behave identically in both tabs.
                Element body;
                if (browse_desc_loading.load()) {
                    body = hbox({
                        text("  "),
                        spinner(15, spinner_idx.load()) | color(kAccent),
                        text("  Loading description…") | italic | color(kMuted),
                    });
                } else if (!browse_desc_error.empty()) {
                    body = text("  ! " + browse_desc_error) | color(kHard) | italic;
                } else {
                    browse_desc_offset = std::clamp(browse_desc_offset, 0,
                                                     std::max(0, (int)browse_desc_cache.size() - 1));
                    Elements desc_lines(browse_desc_cache.begin() + browse_desc_offset, browse_desc_cache.end());
                    body = hbox({
                        text("  "),
                        vbox(std::move(desc_lines)) | flex,
                        text("  "),
                    }) | flex;
                }

                right = window(title_el,
                    vbox({
                        text(""),
                        body,
                        text(""),
                        action_hint,
                        separatorEmpty(),
                    })
                ) | color(kBorder) | flex | reflect(browse_desc_box);
            }
        }

        // ── Footer / status bar ──
        Element footer;
        if (mode == SEARCH && tab_selected == 0) {
            footer = hbox({
                text("  "),
                kbd("Enter"), text(" / ") | color(kMuted), kbd("Esc"),
                text(" done filtering  ") | color(kMuted),
            });
        } else if (mode == SEARCH && tab_selected == 1) {
            footer = hbox({
                text("  "),
                kbd("Enter"), text(" search   ") | color(kMuted),
                kbd("Esc"), text(" cancel  ") | color(kMuted),
            });
        } else if (mode == FETCH) {
            footer = hbox({
                text("  Fetch slug or 'daily':  ") | color(kMuted),
                fetch_input->Render() | flex | color(kText),
                text("  "),
                kbd("Enter"), text(" go   ") | color(kMuted),
                kbd("Esc"), text(" cancel  ") | color(kMuted),
            });
        } else if (mode == CONFIRM_RESET) {
            std::string name = my_filtered.empty() ? "" : my_all[my_filtered[my_selected]].label;
            footer = hbox({
                text("  Delete solution for ") | color(kMuted),
                text(name) | bold | color(kHard),
                text("? ") | color(kMuted),
                kbd("y"), text(" yes  ") | color(kMuted),
                kbd("n"), text(" no  ") | color(kMuted),
            });
        } else if (tab_selected == 0) {
            // Core keys only — everything else lives in the Actions menu (Space)
            // and the full keymap (?), so this line stays short and stable.
            footer = hbox({
                text("  "),
                kbd("↑↓"), text(" move  ") | color(kMuted),
                kbd("⏎"), text(" open  ") | color(kMuted),
                kbd("Space"), text(" actions  ") | color(kAccent),
                kbd("/"), text(" filter  ") | color(kMuted),
                kbd("Tab"), text(" browse  ") | color(kMuted),
                kbd("?"), text(" help  ") | color(kMuted),
                kbd("q"), text(" quit  ") | color(kMuted),
            });
        } else {
            footer = hbox({
                text("  "),
                kbd("↑↓"), text(" nav  ") | color(kMuted),
                kbd("←→"), text(" page  ") | color(kMuted),
                kbd("Enter"), text(" preview  ") | color(kMuted),
                kbd("f"), text(" fetch  ") | color(kMuted),
                kbd("/"), text(" search  ") | color(kMuted),
                kbd("Esc"), text(" clear  ") | color(kMuted),
                kbd("Tab"), text(" my problems  ") | color(kMuted),
                kbd("q"), text(" quit  ") | color(kMuted),
            });
        }
        Element page = vbox({
            header,
            separatorStyled(LIGHT) | color(kBorder),
            hbox({
                left  | size(WIDTH, EQUAL, 36),
                right | flex,
            }) | flex,
            separatorStyled(LIGHT) | color(kBorder),
            footer,
        }) | borderStyled(ROUNDED, kBorder);

        // ── Actions menu overlay (Space) ──
        if (actions_open) {
            auto acts = build_actions();
            if (!acts.empty())
                actions_highlight = std::clamp(actions_highlight, 0, (int)acts.size() - 1);
            actions_row_boxes.assign(acts.size(), Box{});
            Elements rows;
            for (int i = 0; i < (int)acts.size(); ++i) {
                const bool sel = (i == actions_highlight);
                Color base = acts[i].danger ? kHard : kText;
                Element label = text(acts[i].label);
                label = sel ? (label | bold | color(Color::Black)) : (label | color(base));
                Element row = hbox({
                    text(sel ? " ▸ " : "   "),
                    text(std::string(1, acts[i].key)) | bold
                        | color(sel ? Color::Black : kAccent),
                    text("  "),
                    label | flex,
                    text("  "),
                });
                if (sel) row = row | bgcolor(Color::White);
                rows.push_back(row | reflect(actions_row_boxes[i]));
            }
            Element menu = window(
                text(" Actions ") | bold | color(kAccent),
                vbox(std::move(rows))
            ) | size(WIDTH, EQUAL, 34) | color(kBorder) | clear_under;
            page = dbox({page, menu | center});
        }

        // ── Full keymap overlay (?) ──
        if (mode == HELP) {
            auto row = [&](const std::string& keys, const std::string& desc) {
                return hbox({
                    text("  "), text(keys) | bold | color(kAccent) | size(WIDTH, EQUAL, 12),
                    text(desc) | color(kText),
                });
            };
            auto section = [&](const std::string& title) {
                return text("  " + title) | bold | color(kMuted);
            };
            Element keys = vbox({
                section("Navigate"),
                row("↑↓ / j k", "move selection"),
                row("Tab", "switch My Problems / Browse All"),
                row("PgUp/PgDn", "scroll the description"),
                row("/", "filter (My Problems) or search (Browse)"),
                row("q", "quit"),
                text(""),
                section("My Problems"),
                row("Enter", "open in editor (or start solution)"),
                row("Space", "open the Actions menu"),
                row("e", "edit in $EDITOR"),
                row("r", "run against examples"),
                row("s", "submit to LeetCode"),
                row("t / h", "toggle topics / hints"),
                row("f", "fetch a problem by slug"),
                row("S", "sync solved & attempted"),
                row("x", "reset — delete solution (asks first)"),
                row("Esc", "return a Run/Submit result to code view"),
                text(""),
                section("Browse All"),
                row("←→", "previous / next page"),
                row("Enter", "preview   ·   f  fetch into My Problems"),
                text(""),
                text("  press any key to close") | italic | color(kMuted),
            });
            Element card = window(text(" Keyboard shortcuts ") | bold | color(kAccent),
                                  keys) | color(kBorder) | clear_under;
            page = dbox({page, card | center});
        }

        return page;
    };

    auto root_renderer = Renderer(root_tabs, render);

    // Keeps `selected` scrolled into `viewport`'s visible range, using the
    // viewport's box height from the previous render (reflect() is one frame
    // behind, which is close enough for a fixed-layout terminal UI).
    auto ensure_visible = [](int selected, int& offset, const Box& viewport) {
        int visible = std::max(1, viewport.y_max - viewport.y_min + 1);
        if (selected < offset) offset = selected;
        else if (selected >= offset + visible) offset = selected - visible + 1;
        if (offset < 0) offset = 0;
    };

    // Only moves the hover cursor on both tabs — the actual selection (and
    // whatever it drives: description preview, edit/topics/hints/run/submit
    // actions) is untouched until select_my_problem()/select_browse_problem()
    // is explicitly called (Enter/click).
    auto move_sel = [&](int d) {
        if (tab_selected == 0) {
            int n = (int)my_filtered.size();
            if (n > 0) my_hovered = std::clamp(my_hovered + d, 0, n - 1);
            ensure_visible(my_hovered, my_list_offset, my_items_box);
        } else {
            int n = (int)browse_problems.size();
            if (n > 0) browse_hovered = std::clamp(browse_hovered + d, 0, n - 1);
            ensure_visible(browse_hovered, browse_list_offset, browse_items_box);
        }
    };

    auto toggle_topics = [&]() {
        if (my_filtered.empty()) return;
        const auto& m = my_all[my_filtered[my_selected]];
        if (!topics_loaded && !meta_loading.load()) load_meta(m.folder, m.slug, true, -1);
        else topics_open = !topics_open;
    };

    auto toggle_hint = [&](int idx) {
        if (my_filtered.empty()) return;
        const auto& m = my_all[my_filtered[my_selected]];
        if (!hints_loaded && !meta_loading.load()) load_meta(m.folder, m.slug, false, idx);
        else if (idx < (int)hint_open.size()) hint_open[idx] = !hint_open[idx];
    };

    // Single "Hints" toggle (replaces the old 1-9 per-hint keys): loads hints
    // on first use, then expands all of them if any are collapsed, else
    // collapses all. Individual hints stay independently clickable with the
    // mouse; this is just the keyboard/menu entry point.
    auto toggle_hints = [&]() {
        if (my_filtered.empty()) return;
        const auto& m = my_all[my_filtered[my_selected]];
        if (!hints_loaded && !meta_loading.load()) { load_meta(m.folder, m.slug, false, 0); return; }
        bool any_closed = false;
        for (bool open : hint_open) if (!open) { any_closed = true; break; }
        for (size_t i = 0; i < hint_open.size(); ++i) hint_open[i] = any_closed;
    };

    // Builds the contextual Actions menu for the current My-Problems selection.
    // Only lists actions that are valid right now, so there are no silent
    // no-ops. Each item's callback is also reachable directly via its `key`
    // accelerator when the menu is closed (see the key handler below).
    build_actions = [&]() -> std::vector<MenuAction> {
        std::vector<MenuAction> a;
        const bool has = cached_has_solution;
        if (has) {
            a.push_back({'e', "Edit in $EDITOR", false, [&]{ open_current_in_editor(); }});
            a.push_back({'r', "Run against examples", false, [&]{ sol_tab = 1; run_solution_flow(); }});
            a.push_back({'s', "Submit to LeetCode", true, [&]{ sol_tab = 2; submit_solution_flow(); }});
            if (sol_tab != 0) a.push_back({'c', "Back to code view", false, [&]{ sol_tab = 0; }});
        } else {
            a.push_back({'l', "Start solution / language", false, [&]{ open_lang_dropdown(); }});
        }
        a.push_back({'t', "Toggle topics", false, [&]{ toggle_topics(); }});
        a.push_back({'h', "Toggle hints", false, [&]{ toggle_hints(); }});
        a.push_back({'f', "Fetch a problem…", false, [&]{ mode = FETCH; input_focus = 1; fetch_slug.clear(); }});
        a.push_back({'S', "Sync solved & attempted", false, [&]{ start_sync_flow(); }});
        if (has) a.push_back({'x', "Reset — delete solution…", true, [&]{ mode = CONFIRM_RESET; }});
        return a;
    };

    auto root = CatchEvent(root_renderer, [&](Event e) -> bool {
        // The My-Problems pane (topics/hints components) is active whenever
        // we're not editing a text field; otherwise route to search/fetch.
        // CONFIRM_RESET has no input, so it stays on the NAV pane.
        root_pane = (mode == SEARCH || mode == FETCH) ? 1 : 0;

        if (mode == HELP) {
            // Any key (or click) dismisses the keymap overlay.
            mode = NAV;
            return true;
        }

        if (mode == CONFIRM_RESET) {
            if (e == Event::Character('y') || e == Event::Character('Y')) {
                reset_current_solution();
                mode = NAV;
                return true;
            }
            if (e == Event::Escape || e == Event::Character('n') || e == Event::Character('N')) {
                mode = NAV;
                return true;
            }
            return true;  // swallow all other keys while awaiting confirmation
        }

        // Actions menu: while open, it captures all input. Arrows/jk move the
        // highlight, Enter runs it, an item's accelerator runs it directly,
        // and Space/Esc (or a click outside) closes. Mouse hover highlights,
        // click runs.
        if (actions_open) {
            auto acts = build_actions();
            const int n = (int)acts.size();
            actions_highlight = n ? std::clamp(actions_highlight, 0, n - 1) : 0;
            if (e.is_mouse()) {
                const Mouse& m = e.mouse();
                if (m.motion == Mouse::Moved) {
                    for (size_t i = 0; i < actions_row_boxes.size(); ++i)
                        if (actions_row_boxes[i].Contain(m.x, m.y)) { actions_highlight = (int)i; break; }
                    return true;
                }
                if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
                    for (size_t i = 0; i < actions_row_boxes.size() && (int)i < n; ++i)
                        if (actions_row_boxes[i].Contain(m.x, m.y)) { actions_open = false; acts[i].run(); return true; }
                    actions_open = false;  // click outside closes
                }
                return true;
            }
            if (e == Event::ArrowUp   || e == Event::Character('k')) { if (n) actions_highlight = (actions_highlight - 1 + n) % n; return true; }
            if (e == Event::ArrowDown || e == Event::Character('j')) { if (n) actions_highlight = (actions_highlight + 1) % n; return true; }
            if (e == Event::Return) { actions_open = false; if (actions_highlight < n) acts[actions_highlight].run(); return true; }
            if (e == Event::Escape || e == Event::Character(' ')) { actions_open = false; return true; }
            for (const auto& a : acts) if (e == Event::Character(a.key)) { actions_open = false; a.run(); return true; }
            return true;
        }

        if (mode == SEARCH) {
            if (e == Event::Escape) {
                // Esc clears the search rather than committing what was typed.
                mode = NAV;
                search_text.clear();
                if (tab_selected == 0) {
                    my_search.clear();  // render's lazy filter re-runs on the change
                } else if (browse_is_search) {
                    load_browse_page(0, "");
                }
                return true;
            }
            // Return is handled by search_input's own on_enter (set above);
            // everything else (typing, live-filter) falls through to it too.
            if (tab_selected == 0) my_search = search_text;
            return false;
        }

        if (mode == FETCH) {
            if (e == Event::Escape) { mode = NAV; fetch_slug.clear(); return true; }
            // Return is handled by fetch_input's own on_enter (set above).
            return false;
        }

        // Mouse: wheel scroll + click, only acted on in NAV mode.
        if (e.is_mouse()) {
            const Mouse& m = e.mouse();

            if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
                int dir = (m.button == Mouse::WheelUp) ? -1 : +1;
                if (tab_selected == 0 && desc_box.Contain(m.x, m.y)) {
                    desc_offset = std::max(0, desc_offset + dir * 2);
                } else if (tab_selected == 1 && browse_desc_box.Contain(m.x, m.y)) {
                    browse_desc_offset = std::max(0, browse_desc_offset + dir * 2);
                } else if (left_list_box.Contain(m.x, m.y)) {
                    // Scroll the viewport itself (like a scrollbar), independent
                    // of which row is selected — mirrors the description panel.
                    int& offset = (tab_selected == 0) ? my_list_offset : browse_list_offset;
                    const Box& viewport = (tab_selected == 0) ? my_items_box : browse_items_box;
                    int total = (tab_selected == 0) ? (int)my_filtered.size() : (int)browse_problems.size();
                    int visible = std::max(1, viewport.y_max - viewport.y_min + 1);
                    int max_offset = std::max(0, total - visible);
                    offset = std::clamp(offset + dir * 2, 0, max_offset);
                }
                return true;
            }

            // Pure mouse movement (ftxui's "any event" mouse tracking reports
            // this even with no button held) moves the hover cursor the same
            // way arrow keys do. On Browse All that's a separate cursor from
            // the deliberate selection (only a click or Enter previews/
            // fetches — see the note by browse_hovered's declaration); on My
            // Problems there's just the one state, so hovering there moves
            // my_selected directly, same as arrow keys already do.
            if (m.motion == Mouse::Moved && tab_selected == 1) {
                for (size_t i = 0; i < browse_row_boxes.size(); ++i) {
                    if (browse_row_boxes[i].Contain(m.x, m.y)) {
                        browse_hovered = (int)i;
                        break;
                    }
                }
                return true;
            }
            if (m.motion == Mouse::Moved && tab_selected == 0) {
                for (size_t i = 0; i < my_row_boxes.size(); ++i) {
                    if (my_row_boxes[i].Contain(m.x, m.y)) {
                        my_hovered = (int)i;
                        break;
                    }
                }
                return true;
            }

            if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
                if (tab_my_box.Contain(m.x, m.y)) { tab_selected = 0; return true; }
                if (tab_browse_box.Contain(m.x, m.y)) {
                    tab_selected = 1;
                    if (browse_problems.empty() && !browse_loading.load()) load_browse_page(0, "");
                    return true;
                }

                if (tab_selected == 0) {
                    for (size_t i = 0; i < my_row_boxes.size(); ++i) {
                        if (my_row_boxes[i].Contain(m.x, m.y)) {
                            select_my_problem((int)i);
                            return true;
                        }
                    }
                    if (topics_row_box.Contain(m.x, m.y)) { toggle_topics(); return true; }
                    for (size_t i = 0; i < hint_row_boxes.size(); ++i) {
                        if (hint_row_boxes[i].Contain(m.x, m.y)) {
                            toggle_hint((int)i);
                            return true;
                        }
                    }
                    if (start_lang_header_box.Contain(m.x, m.y)) {
                        if (start_lang_open) start_lang_open = false;
                        else open_lang_dropdown();
                        return true;
                    }
                    for (size_t i = 0; i < start_lang_row_boxes.size(); ++i) {
                        if (start_lang_row_boxes[i].Contain(m.x, m.y)) {
                            start_lang_idx = (int)i;
                            start_lang_open = false;
                            return true;
                        }
                    }
                    if (start_button_box.Contain(m.x, m.y)) { start_solution_flow(); return true; }
                    if (sol_tab_code_box.Contain(m.x, m.y)) { sol_tab = 0; return true; }
                    if (sol_tab_run_box.Contain(m.x, m.y)) { sol_tab = 1; return true; }
                    if (sol_tab_submit_box.Contain(m.x, m.y)) { sol_tab = 2; return true; }
                    return true;
                } else {
                    for (size_t i = 0; i < browse_row_boxes.size(); ++i) {
                        if (browse_row_boxes[i].Contain(m.x, m.y)) {
                            select_browse_problem((int)i);
                            return true;
                        }
                    }
                }
            }
            return true;
        }

        // NAV mode
        if (e == Event::Character('q')) { screen.Exit(); return true; }

        if (e == Event::Tab || e == Event::TabReverse) {
            tab_selected ^= 1;
            if (tab_selected == 1 && browse_problems.empty() && !browse_loading.load())
                load_browse_page(0, "");
            return true;
        }

        if (e == Event::Character('/')) {
            search_text = (tab_selected == 0) ? my_search : "";
            mode = SEARCH;
            input_focus = 0;
            return true;
        }

        if (e == Event::Character('?')) { mode = HELP; return true; }

        // The language dropdown is a small modal: while open, arrows move the
        // highlighted row instead of the My-Problems selection, Enter
        // commits it, and Esc cancels. Everything else for tab_selected==0
        // (edit/topics/hints/fetch) is swallowed so it can't fire underneath
        // the dropdown; Tab/quit above still work since they're checked first.
        if (tab_selected == 0 && start_lang_open) {
            if (e == Event::ArrowUp   || e == Event::Character('k')) {
                start_lang_highlight = (start_lang_highlight + kLangOptionCount - 1) % kLangOptionCount;
                return true;
            }
            if (e == Event::ArrowDown || e == Event::Character('j')) {
                start_lang_highlight = (start_lang_highlight + 1) % kLangOptionCount;
                return true;
            }
            if (e == Event::Return) { confirm_lang_highlight(); return true; }
            if (e == Event::Escape) { start_lang_open = false; return true; }
            return true;
        }

        if (e == Event::ArrowUp   || e == Event::Character('k')) { move_sel(-1); return true; }
        if (e == Event::ArrowDown || e == Event::Character('j')) { move_sel(+1); return true; }
        if (e == Event::PageUp) {
            int& offset = (tab_selected == 0) ? desc_offset : browse_desc_offset;
            offset = std::max(0, offset - 3);
            return true;
        }
        if (e == Event::PageDown) {
            int& offset = (tab_selected == 0) ? desc_offset : browse_desc_offset;
            offset += 3;
            return true;
        }

        if (tab_selected == 0) {
            // Space opens the contextual Actions menu — the primary, always-
            // discoverable way to reach everything below. The single-letter
            // handlers after it are accelerators for those same actions (shown
            // in the menu and the ? help), for people who've learned them.
            if (e == Event::Character(' ')) { actions_open = true; actions_highlight = 0; return true; }

            if (e == Event::Return) {
                // First Enter after moving the hover cursor just commits it
                // (mirrors Browse All's select_browse_problem); a second
                // Enter, once hover==selected, performs the usual action.
                if (my_hovered != my_selected) {
                    select_my_problem(my_hovered);
                } else if (cached_has_solution) {
                    open_current_in_editor();
                } else {
                    start_solution_flow();
                }
                return true;
            }

            if (e == Event::Escape) {
                // Esc backs out of the current state: an active filter first,
                // then a Run/Submit result view.
                if (!my_search.empty()) { my_search.clear(); return true; }  // render's lazy filter re-runs
                if (sol_tab != 0) { sol_tab = 0; return true; }
                return true;
            }

            // Accelerators — availability mirrors build_actions() exactly, so
            // none of these is ever a silent no-op.
            if (e == Event::Character('f')) { mode = FETCH; input_focus = 1; fetch_slug.clear(); return true; }
            if (e == Event::Character('S')) { start_sync_flow(); return true; }
            if (e == Event::Character('t')) { toggle_topics(); return true; }
            if (e == Event::Character('h')) { toggle_hints(); return true; }
            if (!cached_has_solution && e == Event::Character('l')) { open_lang_dropdown(); return true; }
            if (cached_has_solution) {
                if (e == Event::Character('e')) { open_current_in_editor(); return true; }
                // r/s open their tab on the first press; only once you're
                // already on it does a second press actually run/submit.
                if (e == Event::Character('r')) {
                    if (sol_tab != 1) sol_tab = 1; else run_solution_flow();
                    return true;
                }
                if (e == Event::Character('s')) {
                    if (sol_tab != 2) sol_tab = 2; else submit_solution_flow();
                    return true;
                }
                if (e == Event::Character('c')) { sol_tab = 0; return true; }
                if (e == Event::Character('x')) { mode = CONFIRM_RESET; return true; }
            }
            return true;
        } else {
            if (e == Event::ArrowLeft) {
                if (!browse_is_search && browse_page > 0)
                    load_browse_page(browse_page - 1, "");
                return true;
            }
            if (e == Event::ArrowRight) {
                int total_pages = (browse_total + 99) / 100;
                if (!browse_is_search && browse_page + 1 < total_pages)
                    load_browse_page(browse_page + 1, "");
                return true;
            }
            if (e == Event::Escape && browse_is_search) {
                search_text.clear();
                load_browse_page(0, "");
                return true;
            }
            if (e == Event::Return && !browse_problems.empty()) {
                select_browse_problem(browse_hovered);
                return true;
            }
            if (e == Event::Character('f') && browse_selected >= 0) {
                const auto& p = browse_problems[browse_selected];
                std::string want_id = p.id;
                fetch_problem(p.slug, "");
                load_my_problems();
                recompute_my();
                tab_selected = 0;
                for (int vis = 0; vis < (int)my_filtered.size(); ++vis) {
                    if (my_all[my_filtered[vis]].label.rfind(want_id + ". ", 0) == 0) {
                        my_selected = vis; break;
                    }
                }
                return true;
            }
            return true;
        }
    });

    screen.Loop(root);

    // Don't leave a floating Kitty/iTerm2 image on screen after leetcli
    // exits — the terminal doesn't know to clear it on its own.
    for (const auto& d : drawn_images) std::cout << build_delete_sequence(img_protocol, d.id);
    std::cout.flush();
}

}  // namespace leetcli
