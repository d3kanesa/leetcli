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

    // Check for missing or null question field
    if (!json.contains("data") || json["data"].is_null() || !json["data"].contains("question") || json["data"]["question"].is_null()) {
        return "Problem not found. Check the title slug: \"" + slug + "\"";
    }

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
    std::string dir = get_problems_dir() + id + ". " + safe_title;
    std::filesystem::create_directories(dir);

    // Write files
    write_markdown_file(dir + "/README.md", title, markdown);
    write_solution_file(dir + "/solution.cpp", starter_code);
    std::string solution_path = dir + "/solution.cpp";

    return title + "\n\n" + markdown;
}
    std::string read_question_id_from_readme(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";

    std::string line;
    std::getline(in, line); // expect "# Title"
    std::getline(in, line); // blank line
    std::getline(in, line); // content starts here
    return line.empty() ? "" : "unknown";
}

    void solve_problem(const std::string& slug) {
    // Step 1: Fetch title + question ID from LeetCode
    nlohmann::json query = {
        {"query", R"(
            query getQuestionDetail($titleSlug: String!) {
                question(titleSlug: $titleSlug) {
                    title
                    questionId
                }
            }
        )"},
        {"variables", {{"titleSlug", slug}}}
    };

    cpr::Response r = cpr::Post(
        cpr::Url{"https://leetcode.com/graphql"},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Body{query.dump()}
    );

    if (r.status_code != 200) {
        std::cerr << "Failed to query problem info.\n";
        return;
    }

    auto json = nlohmann::json::parse(r.text);
    auto question = json["data"]["question"];
    std::string id = question["questionId"];
    std::string title = question["title"];
    std::string safe_title = std::regex_replace(title, std::regex("[\\\\/:*?\"<>|]"), "");

    // Step 2: Build full path to solution.cpp
    std::string folder = get_problems_dir() + "/" + id + ". " + safe_title;
    std::string solution_path = folder + "/solution.cpp";

    // Step 3: Check file exists and launch
    if (!std::filesystem::exists(solution_path)) {
        std::cerr << "File not found: " << solution_path << "\n";
        std::cerr << "Run: leetcli fetch " << slug << "\n";
        return;
    }

    launch_in_editor(solution_path);
}
    void list_fetched_problems() {
    std::string problems_dir = get_problems_dir();

    if (!std::filesystem::exists(problems_dir)) {
        std::cerr << "Problems directory not found: " << problems_dir << "\n";
        return;
    }

    std::cout << "Fetched problems:\n";

    for (const auto& entry : std::filesystem::directory_iterator(problems_dir)) {
        if (!entry.is_directory()) continue;

        std::string folder_name = entry.path().filename().string();
        std::string solution_path = entry.path().string() + "/solution.cpp";
        std::string status = std::filesystem::exists(solution_path) ? "[💾]" : "[ ]";

        std::cout << "  " << status << " " << folder_name << "\n";
    }
}

}
