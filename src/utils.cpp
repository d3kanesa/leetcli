#include "utils.h"
#include <regex>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

namespace leetcli {
    std::string get_problems_dir() {
        std::filesystem::path config_path = std::filesystem::path(std::getenv("HOME")) / ".leetcli/config.json";
        if (!std::filesystem::exists(config_path)) {
            std::cerr << "Error: config not found. Run `leetcli init` first.\n";
            std::exit(1);
        }

        std::ifstream in(config_path);
        nlohmann::json config;
        in >> config;
        return config["problems_dir"];
    }

    void init_problems_folder() {
        std::filesystem::path home = std::getenv("HOME");
        std::filesystem::path config_dir = home / ".leetcli";
        std::filesystem::path config_path = config_dir / "config.json";

        // Check if already initialized
        if (std::filesystem::exists(config_path)) {
            std::cerr << "leetcli is already initialized.\n";
            std::cerr << "To reset: delete " << config_path << "\n";
            return;
        }

        // Create config and problems dir
        std::string default_path = std::filesystem::current_path().string() + "/problems";
        std::filesystem::create_directories(config_dir);
        std::filesystem::create_directories(default_path);

        nlohmann::json config = {
            {"problems_dir", default_path}
        };

        std::ofstream out(config_path);
        if (!out) {
            std::cerr << "Failed to write config: " << config_path << "\n";
            return;
        }

        out << config.dump(4);
        std::cout << "leetcli initialized.\nProblems will be saved to:\n  " << default_path << "\n";
    }


    std::string html_to_text(const std::string& html) {
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

    void write_markdown_file(const std::string& path, const std::string& title, const std::string& markdown) {
        std::ofstream out(path);
        if (!out) {
            std::cerr << "Failed to write markdown: " << path << "\n";
            return;
        }
        out << "# " << title << "\n\n" << markdown << "\n";
        out.close();
    }

    void write_solution_file(const std::string& path, const std::string& code) {
        std::ofstream out(path);
        if (!out) {
            std::cerr << "Failed to write solution: " << path << "\n";
            return;
        }
        out << code << "\n";
        out.close();


    }
    void launch_in_editor(const std::string& path) {
            std::string cmd = "vim";
            cmd += " \"" + path + "\"";
            std::system(cmd.c_str());
        }

} // namespace leetcli
