#include "utils.h"
#include <regex>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>

namespace leetcli {
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
        std::cout << "Enter your preferred language (e.g., cpp, python, java): ";
        std::getline(std::cin, lang);

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

    void launch_in_editor(const std::string &path) {
        std::string cmd = "vim";
        cmd += " \"" + path + "\"";
        std::system(cmd.c_str());
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
            {"python", ".py"},
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
        std::cout << solution_file << "\n";
        return 0;
    }
} // namespace leetcli
