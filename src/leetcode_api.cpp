#include <iostream>
#include "leetcode_api.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <fstream>

void write_markdown_file(const std::string& slug, const std::string& title, const std::string& markdown) {
    std::string filename = slug + ".md";
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Failed to write to file: " << filename << "\n";
        return;
    }

    out << "# " << title << "\n\n";
    out << markdown << "\n";
    out.close();

    std::cout << "Saved: " << filename << "\n";
}


std::string html_to_text(const std::string& html) {
    std::string text = html;

    // Replace headings
    text = std::regex_replace(text, std::regex("<h1[^>]*>"), "# ");
    text = std::regex_replace(text, std::regex("<h2[^>]*>"), "## ");
    text = std::regex_replace(text, std::regex("<h3[^>]*>"), "### ");
    text = std::regex_replace(text, std::regex("</h[1-6]>"), "\n");

    // Replace paragraphs and breaks
    text = std::regex_replace(text, std::regex("<p[^>]*>"), "\n");
    text = std::regex_replace(text, std::regex("</p>"), "\n");
    text = std::regex_replace(text, std::regex("<br[^>]*>"), "\n");

    // Replace lists
    text = std::regex_replace(text, std::regex("<li[^>]*>"), " - ");
    text = std::regex_replace(text, std::regex("</li>"), "\n");

    // Code blocks
    text = std::regex_replace(text, std::regex("<pre[^>]*><code[^>]*>"), "```\n");
    text = std::regex_replace(text, std::regex("</code></pre>"), "\n```");

    // Inline code
    text = std::regex_replace(text, std::regex("<code[^>]*>"), "`");
    text = std::regex_replace(text, std::regex("</code>"), "`");

    // Bold
    text = std::regex_replace(text, std::regex("<b[^>]*>"), "**");
    text = std::regex_replace(text, std::regex("</b>"), "**");
    text = std::regex_replace(text, std::regex("<strong[^>]*>"), "**");
    text = std::regex_replace(text, std::regex("</strong>"), "**");

    // Remove all other tags
    text = std::regex_replace(text, std::regex("<[^>]*>"), "");

    // Optional: Unescape HTML entities (basic set)
    text = std::regex_replace(text, std::regex("&nbsp;"), " ");
    text = std::regex_replace(text, std::regex("&lt;"), "<");
    text = std::regex_replace(text, std::regex("&gt;"), ">");
    text = std::regex_replace(text, std::regex("&amp;"), "&");
    text = std::regex_replace(text, std::regex("&quot;"), "\"");

    return text;
}

namespace leetcli {
    std::string fetch_problem(const std::string& slug) {
        nlohmann::json query = {
            {"query", "query getQuestionDetail($titleSlug: String!) { question(titleSlug: $titleSlug) { title content } }"},
            {"variables", {{"titleSlug", slug}}}
        };

        cpr::Response r = cpr::Post(
            cpr::Url{"https://leetcode.com/graphql"},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{query.dump()}
        );

        if (r.status_code != 200) return "Failed to fetch problem.";

        auto json = nlohmann::json::parse(r.text);
        auto question = json["data"]["question"];
        std::string title = question["title"].get<std::string>();
        std::string markdown = html_to_text(question["content"].get<std::string>());

        write_markdown_file(slug, title, markdown);

        return title + "\n\n" + markdown;
    }

}
