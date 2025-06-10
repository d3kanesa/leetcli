#include "leetcode_api.h"
#include "utils.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace leetcli {

std::string html_to_text(const std::string& html); // assuming this is declared elsewhere
void write_markdown_file(const std::string& path, const std::string& title, const std::string& markdown);
void write_solution_file(const std::string& path, const std::string& code);

std::string fetch_problem(const std::string& slug) {
    // GraphQL query: fetch title, content, questionId, starter code
    nlohmann::json query = {
        {"query", R"(
            query getQuestionDetail($titleSlug: String!) {
                question(titleSlug: $titleSlug) {
                    title
                    content
                    questionId
                    codeSnippets {
                        lang
                        langSlug
                        code
                    }
                }
            }
        )"},
        {"variables", {{"titleSlug", slug}}}
    };

    // Send POST request
    cpr::Response r = cpr::Post(
        cpr::Url{"https://leetcode.com/graphql"},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Body{query.dump()}
    );

    if (r.status_code != 200) {
        std::cerr << "Failed to fetch problem: HTTP " << r.status_code << "\n";
        return "Failed to fetch problem.";
    }

    // Parse response JSON
    auto json = nlohmann::json::parse(r.text);
    auto question = json["data"]["question"];

    std::string title = question["title"];
    std::string id = question["questionId"];
    std::string markdown = html_to_text(question["content"]);

    // Find C++ starter code
    std::string starter_code = "// Starter code not found.\n";
    for (const auto& snippet : question["codeSnippets"]) {
        if (snippet["langSlug"] == "cpp") {
            starter_code = snippet["code"];
            break;
        }
    }

    // Make safe folder path: problems/{id}. {title}/
    std::string safe_title = std::regex_replace(title, std::regex("[\\\\/:*?\"<>|]"), "");
    std::string dir = "problems/" + id + ". " + safe_title;
    std::filesystem::create_directories(dir);

    // Write files
    write_markdown_file(dir + "/README.md", title, markdown);
    write_solution_file(dir + "/solution.cpp", starter_code);

    return title + "\n\n" + markdown;
}

}
