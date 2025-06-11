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

std::string fetch_problem(const std::string& slug, const std::string& lang_override) {
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
    std::string preferred_lang = lang_override.empty() ? get_preferred_language() : lang_override;

    std::string starter_code = "// No code found for " + preferred_lang + "\n";
    for (const auto& snippet : question["codeSnippets"]) {
        if (snippet["langSlug"] == preferred_lang) {
            starter_code = snippet["code"];
            break;
        }
    }

    // File extension based on lang
    std::string ext = (preferred_lang == "python") ? ".py" :
                      (preferred_lang == "java") ? ".java" : ".cpp";


    // Make safe folder path: problems/{id}. {title}/
    std::string safe_title = std::regex_replace(title, std::regex("[\\\\/:*?\"<>|]"), "");
    std::string dir = get_problems_dir() + "/" + id + ". " + safe_title;
    std::filesystem::create_directories(dir);

    std::string solution_path = dir + "/solution" + ext;

    // Write files
    write_markdown_file(dir + "/README.md", title, markdown);
    write_solution_file(solution_path, starter_code);

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
    // Step 1: Query LeetCode to get the ID and Title
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

    // Step 2: Build the folder path
    std::string folder = get_problems_dir() + "/" + id + ". " + safe_title;

    if (!std::filesystem::exists(folder)) {
        std::cerr << "Folder not found. Run: leetcli fetch " << slug << "\n";
        return;
    }

    // Step 3: Try to find any known solution file
    std::vector<std::string> extensions = {".cpp", ".py", ".java", ".js", ".cs"};
    std::string solution_file;

    for (const auto& ext : extensions) {
        std::filesystem::path candidate = folder + "/solution" + ext;
        if (std::filesystem::exists(candidate)) {
            solution_file = candidate.string();
            break;
        }
    }

    // Step 4: Launch appropriate file or folder
    if (!solution_file.empty()) {
        launch_in_editor(solution_file);
    } else {
        std::cout << "No solution file found. Opening folder instead.\n";
        launch_in_editor(folder);
    }
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
