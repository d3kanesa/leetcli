#include "leetcode_api.h"
#include "utils.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>

namespace leetcli {
    std::string html_to_text(const std::string &html); // assuming this is declared elsewhere
    void write_markdown_file(const std::string &path, const std::string &title, const std::string &markdown);

    void write_solution_file(const std::string &path, const std::string &code);

    std::string fetch_problem(const std::string &slug, const std::string &lang_override) {
        // GraphQL query: fetch title, content, questionId, starter code
        nlohmann::json query = {
            {
                "query", R"(
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
        )"
            },
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
        if (!json.contains("data") || json["data"].is_null() || !json["data"].contains("question") || json["data"][
                "question"].is_null()) {
            return "Problem not found. Check the title slug: \"" + slug + "\"";
        }

        auto question = json["data"]["question"];

        std::string title = question["title"];
        std::string id = question["questionId"];
        std::string markdown = html_to_text(question["content"]);

        // Find C++ starter code
        std::string preferred_lang = lang_override.empty() ? get_preferred_language() : lang_override;

        std::string starter_code = "// No code found for " + preferred_lang + "\n";
        for (const auto &snippet: question["codeSnippets"]) {
            if (snippet["langSlug"] == preferred_lang) {
                starter_code = snippet["code"];
                break;
            }
        }

        // File extension based on lang
        std::string ext = (preferred_lang == "python") ? ".py" : (preferred_lang == "java") ? ".java" : ".cpp";


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

    std::string read_question_id_from_readme(const std::string &path) {
        std::ifstream in(path);
        if (!in) return "";

        std::string line;
        std::getline(in, line); // expect "# Title"
        std::getline(in, line); // blank line
        std::getline(in, line); // content starts here
        return line.empty() ? "" : "unknown";
    }

    void solve_problem(const std::string &slug, const std::string &lang_override) {
        std::string solution_file;
        int status;
        if (!lang_override.empty()) {
            status = get_solution_filepath(slug, solution_file, lang_override);
        } else {
            status = get_solution_filepath(slug, solution_file);
        }
        if (!status) {
            launch_in_editor(solution_file);
        }
    }

    void list_fetched_problems() {
        std::string problems_dir = get_problems_dir();

        if (!std::filesystem::exists(problems_dir)) {
            std::cerr << "Problems directory not found: " << problems_dir << "\n";
            return;
        }

        std::cout << "Fetched problems:\n";

        for (const auto &entry: std::filesystem::directory_iterator(problems_dir)) {
            if (!entry.is_directory()) continue;

            std::string folder_name = entry.path().filename().string();
            std::string solution_path = entry.path().string() + "/solution.cpp";
            std::string status = std::filesystem::exists(solution_path) ? "[💾]" : "[ ]";

            std::cout << "  " << status << " " << folder_name << "\n";
        }
    }

    void submit_solution(const std::string &slug, const std::string &lang_override) {
        std::string session = get_session_cookie();
        std::string csrf = get_csrf_token();
        // Step 1: Read source code from file
        std::string solution_path;
        if (!lang_override.empty()) {
            get_solution_filepath(slug, solution_path, lang_override);
        } else {
            get_solution_filepath(slug, solution_path);
        }
        std::ifstream file(solution_path);
        if (!file) {
            // std::cerr << "Error: Could not open file " << solution_path << "\n";
            return;
        }

        std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // Step 2: Query LeetCode to get questionId
        nlohmann::json query = {
            {
                "query", R"(
                query getQuestionDetail($titleSlug: String!) {
                    question(titleSlug: $titleSlug) {
                        questionId
                    }
                }
            )"
            },
            {"variables", {{"titleSlug", slug}}}
        };

        auto resp = cpr::Post(
            cpr::Url{"https://leetcode.com/graphql"},
            cpr::Header{{"Content-Type", "application/json"}, {"Cookie", "LEETCODE_SESSION=" + session}},
            cpr::Body{query.dump()}
        );

        if (resp.status_code != 200) {
            std::cerr << "Failed to fetch question ID\n";
            return;
        }

        auto json = nlohmann::json::parse(resp.text);
        std::string question_id = json["data"]["question"]["questionId"];

        // Step 3: Submit the solution
        std::string lang = get_preferred_language();
        nlohmann::json payload = {
            {"lang", lang},
            {"question_id", question_id},
            {"typed_code", code}
        };

        auto submit_resp = cpr::Post(
            cpr::Url{"https://leetcode.com/problems/" + slug + "/submit/"},
            cpr::Header{
                {"Content-Type", "application/json"},
                {"Referer", "https://leetcode.com/problems/" + slug + "/"},
                {"x-csrftoken", csrf},
                {"Cookie", "LEETCODE_SESSION=" + session + "; csrftoken=" + csrf}
            },
            cpr::Body{payload.dump()}
        );

        if (submit_resp.status_code != 200) {
            std::cerr << "Submission failed\n";
            std::cerr << "Status: " << submit_resp.status_code << "\n";
            std::cerr << "Response body:\n" << submit_resp.text << "\n";
            return;
        }

        // Step 4: Extract submission_id
        std::string submission_id;
        try {
            auto j = nlohmann::json::parse(submit_resp.text);
            if (j.contains("submission_id")) {
                submission_id = std::to_string(j["submission_id"].get<int>());
            } else {
                std::cerr << "No submission_id in response.\n";
                std::cerr << "Raw JSON: " << j.dump(2) << "\n";
                return;
            }
        } catch (...) {
            std::cerr << "Invalid JSON. Raw response:\n" << submit_resp.text << "\n";
            return;
        }

        // Step 5: Poll submission result
        std::cout << "Waiting for result...\n";
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            auto result_resp = cpr::Get(
                cpr::Url{"https://leetcode.com/submissions/detail/" + submission_id + "/check/"},
                cpr::Header{{"Cookie", "LEETCODE_SESSION=" + session}}
            );

            if (result_resp.status_code != 200) {
                std::cerr << "Failed to poll submission\n";
                return;
            }

            auto result_json = nlohmann::json::parse(result_resp.text);
            // std::cout << result_json;
            std::string state = result_json["state"];
            if (state == "SUCCESS") {
                std::string status_msg = result_json["status_msg"];
                std::cout << "Result: " << status_msg << "\n";

                if (status_msg == "Accepted") {
                    std::cout << "✅ Accepted! Runtime: " << result_json["status_runtime"]
                            << ", Memory: " << result_json["status_memory"] << "\n";
                } else {
                    std::cout << "❌ " << status_msg << "\n";
                    if (result_json.contains("compile_error")) {
                        std::cout << "Compile Error:\n" << result_json["compile_error"] << "\n";
                    }
                    if (result_json.contains("input_formatted"))
                        std::cout << "Input:            " << result_json["input_formatted"] << "\n";

                    if (result_json.contains("expected_output"))
                        std::cout << "Expected Output:  " << result_json["expected_output"] << "\n";

                    if (result_json.contains("code_output"))
                        std::cout << "Your Output:      " << result_json["code_output"] << "\n";

                    if (result_json.contains("total_correct") && result_json.contains("total_testcases"))
                        std::cout << "Testcases Passed: " << result_json["total_correct"] << " / " << result_json[
                            "total_testcases"] << "\n";

                    std::cout << "\n🔍 View full details:\n";
                    std::cout << "   https://leetcode.com/submissions/detail/" << submission_id << "/\n";
                }

                break;
            }
        }
    }
}
