#include "utils.h"
#include <regex>
#include <fstream>
#include <filesystem>
#include <iostream>

namespace leetcli {

        void init_problems_folder() {
            std::filesystem::path dir = "problems";
            if (!std::filesystem::exists(dir)) {
                if (std::filesystem::create_directory(dir)) {
                    std::cout << "Created problems/ directory.\n";
                } else {
                    std::cerr << "Failed to create problems/ directory.\n";
                }
            } else {
                std::cout << "problems/ directory already exists.\n";
            }
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

} // namespace leetcli
