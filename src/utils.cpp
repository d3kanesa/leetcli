#include "utils.h"
#include <regex>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>

namespace leetcli {

    void set_gemini_key(const std::string& key) {
        nlohmann::json config;
        std::string path = std::filesystem::path(get_home()) / ".leetcli/config.json";

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

} // namespace leetcli
