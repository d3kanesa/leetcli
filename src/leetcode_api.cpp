#include "leetcode_api.h"
#include "utils.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace leetcli {
    std::string html_to_text(const std::string &html); // assuming this is declared elsewhere
    void write_markdown_file(const std::string &path, const std::string &title, const std::string &markdown);

    void write_solution_file(const std::string &path, const std::string &code);

    void analyze_runtime(const std::string& slug, const std::string &lang_override) {
        std::string path;
        if (!lang_override.empty()) {
            get_solution_filepath(slug, path, lang_override);
        } else {
            get_solution_filepath(slug, path);
        }

            std::ifstream file(path);
            if (!file) {
                std::cerr << "❌ Could not open solution file for " << slug << "\n";
                return;
            }

            std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::string api_key = get_gemini_key(); // Should be renamed for Gemini if needed

            if (api_key.empty()) {
                std::cerr << "❌ No Gemini API key found. Use `leetcli config set-gemini-key <your-key>` first.\n";
                return;
            }

            // Prompt for Gemini
            std::string prompt =
                "Analyze the time and space complexity of the following code and return a JSON object like:\n"
                "{ \"time\": \"O(n)\", \"space\": \"O(1)\" }\n"
                "If the code is invalid or empty, return:\n"
                "{ \"error\": \"Invalid or empty code\" }\n\n"
                "Code:\n" + code;

            // Construct Gemini request payload
            nlohmann::json payload = {
                {"contents", {{
                    {"parts", {{
                        {"text", prompt}
                    }}}
                }}},
                {"generationConfig", {
                    {"responseMimeType", "application/json"},
                    {"responseSchema", {
                        {"type", "OBJECT"},
                        {"properties", {
                            {"time", {{"type", "STRING"}}},
                            {"space", {{"type", "STRING"}}},
                            {"error", {{"type", "STRING"}}}
                        }},
                        {"required", {"time", "space"}}
                    }}
                }}
            };

            std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + api_key;

            cpr::Response r = cpr::Post(
                cpr::Url{url},
                cpr::Header{{"Content-Type", "application/json"}},
                cpr::Body{payload.dump()}
            );

            if (r.status_code != 200) {
                std::cerr << "❌ Gemini API call failed: " << r.status_code << "\n" << r.text << "\n";
                return;
            }

            nlohmann::json response_json = nlohmann::json::parse(r.text);
            try {
                nlohmann::json part = response_json["candidates"][0]["content"]["parts"][0];
                std::string raw_json = part["text"].get<std::string>();

                nlohmann::json inner = nlohmann::json::parse(raw_json);

                std::cout << "\n🧠 AI Runtime Analysis (Experimantal):\n";
                if (inner.contains("error")) {
                    std::cout << "  ⚠️  " << inner["error"] << "\n";
                } else {
                    std::cout << "  Time:  " << inner["time"] << "\n";
                    std::cout << "  Space: " << inner["space"] << "\n";
                }
            } catch (const std::exception &e) {
                std::cerr << "Failed to parse inner JSON: " << e.what() << "\n";
                std::cerr << "Raw text:\n" << r.text << "\n";
            }
    }



    void handle_config_command(const std::vector<std::string> &args) {
        if (args.size() == 3 && args[1] == "set-gemini-key") {
                set_gemini_key(args[2]);
        } else {
            std::cerr << "Usage: leetcli config set-gemini-key <your-api-key>\n";
        }
    }


    std::string get_daily_question_slug() {
        const std::string& session = get_session_cookie();
        const std::string& csrf = get_csrf_token();
        const std::string graphql_url = "https://leetcode.com/graphql";

        std::string query = R"({
        "query": "query questionOfToday { activeDailyCodingChallengeQuestion { question { titleSlug } } }"
    })";

        auto response = cpr::Post(
            cpr::Url{graphql_url},
            cpr::Header{
                {"Content-Type", "application/json"},
                {"x-csrftoken", csrf},
                {"Cookie", "LEETCODE_SESSION=" + session + "; csrftoken=" + csrf},
                {"Referer", "https://leetcode.com/problemset/all/"}
            },
            cpr::Body{query}
        );

        if (response.status_code != 200) {
            std::cerr << "Failed to fetch daily question: " << response.status_code << "\n" << response.text << std::endl;
            return "";
        }

        auto json = nlohmann::json::parse(response.text);
        return json["data"]["activeDailyCodingChallengeQuestion"]["question"]["titleSlug"];
    }

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
        fetch_testcases(slug, dir);
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
    void run_problem(const std::string& slug, const std::string& lang, const std::string& question_id,
        const std::string& code, const std::string& test_input, const std::string& session, const std::string& csrf) {
        // Submit to LeetCode
        nlohmann::json body = {
            {"lang", lang},
            {"question_id", question_id},
            {"typed_code", code},
            {"data_input", test_input}
        };

        auto url = "https://leetcode.com/problems/" + slug + "/interpret_solution/";
        cpr::Response r = cpr::Post(
            cpr::Url{url},
            cpr::Header{
                {"Content-Type", "application/json"},
                {"x-csrftoken", csrf},
                {"Cookie", "LEETCODE_SESSION=" + session + "; csrftoken=" + csrf},
                {"Referer", "https://leetcode.com/problems/" + slug + "/"}
            },
            cpr::Body{body.dump()}
        );

        if (r.status_code != 200) {
            std::cerr << "Run failed: " << r.status_code << "\n" << r.text << std::endl;
            return;
        }

        std::string interpret_id = nlohmann::json::parse(r.text)["interpret_id"];
        std::string check_url = "https://leetcode.com/submissions/detail/" + interpret_id + "/check/";
        std::cout << "Waiting for result...\n";
        // Poll for result
        nlohmann::json result;
        for (int i = 0; i < 10; ++i) {
            cpr::Response check = cpr::Get(
                cpr::Url{check_url},
                cpr::Header{
                    {"x-csrftoken", csrf},
                    {"Cookie", "LEETCODE_SESSION=" + session + "; csrftoken=" + csrf},
                    {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36"},
                    {"Origin", "https://leetcode.com"},
                    {"Referer", "https://leetcode.com/problems/" + slug + "/"}
                }
            );

            if (check.status_code != 200) {
                std::cerr << "Polling failed: " << check.status_code << std::endl;
                return;
            }

            result = nlohmann::json::parse(check.text);
            if (result["state"] == "SUCCESS") break;

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Pretty-print final result
        std::cout << "\n≡ƒƒ⌐ Run Result\n";
        std::cout << "------------------------\n";

        // Always show status
        std::cout << "Status:        " << result.value("status_msg", "Unknown") << "\n";

        if (result.contains("compile_error") && !result["compile_error"].get<std::string>().empty()) {
            std::cout << "⛔ Compile Error:\n";
            std::cout << result["compile_error"].get<std::string>() << "\n";
            return;
        }

        if (!result.value("run_success", false)) {
            std::cout << "⛔ Runtime Error or Submission Failed\n";
            if (result.contains("std_output_list")) {
                std::cout << "Output: " << result["std_output_list"] << "\n";
            }
            return;
        }

        if (result.contains("correct_answer") && !result["correct_answer"].is_null()) {
            std::cout << "Correct:       " << (result["correct_answer"].get<bool>() ? "✅ Yes" : "❌ No") << "\n";
        } else {
            std::cout << "Correct:       Unknown\n";
        }

        if (result.contains("code_answer") && !result["code_answer"].empty()) {
            std::cout << "Your Output:   " << result["code_answer"][0] << "\n";
        }
        if (result.contains("expected_code_answer") && !result["expected_code_answer"].empty()) {
            std::cout << "Expected:      " << result["expected_code_answer"][0] << "\n";
        }

        std::cout << "Runtime:       " << result.value("status_runtime", "N/A") << "\n";
        std::cout << "Memory:        " << result.value("status_memory", "N/A") << "\n";
        std::cout << "Language:      " << result.value("pretty_lang", lang) << "\n";
    }
    void run_tests(const std::string& slug, const std::string &lang_override) {
        // Detect file
        std::string folder_path;
        get_solution_folder(slug, folder_path);
        std::string solution_path;
        if (!lang_override.empty()) {
            get_solution_filepath(slug, solution_path, lang_override);
        } else {
            get_solution_filepath(slug, solution_path);
        }
        std::string ext = get_file_extension(solution_path);
        std::string lang;
        if (ext == "cpp") {
            lang = "cpp";
        } else if (ext == "py") {
            lang = "python3";
        } else if (ext  ==  "java") {
            lang = "java";
        } else {
            std::cerr << "No solution file found.\n";
            return;
        }

        std::ifstream file(solution_path);
        if (!file) {
            std::cerr << "Error: Could not open file " << solution_path << "\n";
            return;
        }
        std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // Load tokens
        std::string session = get_session_cookie();
        std::string csrf = get_csrf_token();
        std::string question_id = get_question_id(slug, session, csrf);

        std::vector<std::string> cases = load_testcases(folder_path + "/" + "testcases.txt");
        std::cout << "Running " << cases.size() << " testcases..." << std::endl;
        for (const std::string& test : cases) {
            std::cout << "Testcase:\n" << test << "\n---\n";
            run_problem(slug, lang, question_id, code, test, session, csrf);

            // Add delay between submissions to avoid rate limiting
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    void fetch_problem_topics(const std::string &slug) {
        std::string actual_slug = slug;

        if (slug == "daily") {
            nlohmann::json query = {
                {"query", R"(
                query questionOfToday {
                    activeDailyCodingChallengeQuestion {
                        question {
                            titleSlug
                        }
                    }
                }
            )"}
            };

            cpr::Response r = cpr::Post(
                cpr::Url{"https://leetcode.com/graphql"},
                cpr::Header{{"Content-Type", "application/json"}},
                cpr::Body{query.dump()}
            );

            if (r.status_code != 200) {
                std::cerr << "Failed to fetch daily problem slug: HTTP " << r.status_code << "\n";
                return;
            }

            auto json = nlohmann::json::parse(r.text);
            if (!json.contains("data")) {
                std::cerr << "Invalid daily challenge response.\n";
                return;
            }

            actual_slug = json["data"]["activeDailyCodingChallengeQuestion"]["question"]["titleSlug"];
            std::cout << "Fetching topics for today's daily problem: " << actual_slug << "\n";
        }
        nlohmann::json query = {
            {
                "query", R"(
                query getQuestionTags($titleSlug: String!) {
                    question(titleSlug: $titleSlug) {
                        topicTags {
                            name
                        }
                    }
                }
            )"
            },
            {"variables", {{"titleSlug", actual_slug}}}
        };

        cpr::Response r = cpr::Post(
            cpr::Url{"https://leetcode.com/graphql"},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{query.dump()}
        );

        std::vector<std::string> topics;
        if (r.status_code != 200) {
            std::cerr << "Failed to fetch topics: HTTP " << r.status_code << "\n";
            return;
        }

        auto json = nlohmann::json::parse(r.text);
        if (!json.contains("data") || json["data"].is_null() || !json["data"].contains("question")) {
            std::cerr << "Invalid response when fetching topics.\n";
            return;
        }
        std::cout << "Topics for \"" << slug << "\":\n";
        int count = 1;
        for (const auto &topic_json : json["data"]["question"]["topicTags"]) {
            std::string topic = topic_json["name"].get<std::string>();
            std::cout << "  " << count++ << ". " << topic << "\n";
        }
    }
    void fetch_problem_hints(const std::string &slug) {
        std::string actual_slug = slug;

        if (slug == "daily") {
            nlohmann::json query = {
                {"query", R"(
                query questionOfToday {
                    activeDailyCodingChallengeQuestion {
                        question {
                            titleSlug
                        }
                    }
                }
            )"}
            };

            cpr::Response r = cpr::Post(
                cpr::Url{"https://leetcode.com/graphql"},
                cpr::Header{{"Content-Type", "application/json"}},
                cpr::Body{query.dump()}
            );

            if (r.status_code != 200) {
                std::cerr << "Failed to fetch daily problem slug: HTTP " << r.status_code << "\n";
                return;
            }

            auto json = nlohmann::json::parse(r.text);
            if (!json.contains("data")) {
                std::cerr << "Invalid daily challenge response.\n";
                return;
            }

            actual_slug = json["data"]["activeDailyCodingChallengeQuestion"]["question"]["titleSlug"];
            std::cout << "Fetching hints for today's daily problem: " << actual_slug << "\n";
        }
        nlohmann::json query = {
            {
                "query", R"(
                query getHints($titleSlug: String!) {
                    question(titleSlug: $titleSlug) {
                        hints
                    }
                }
            )"
            },
            {"variables", {{"titleSlug", actual_slug}}}
        };

        cpr::Response r = cpr::Post(
            cpr::Url{"https://leetcode.com/graphql"},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{query.dump()}
        );

        std::vector<std::string> hints;
        if (r.status_code != 200) {
            std::cerr << "Failed to fetch hints: HTTP " << r.status_code << "\n";
            return;
        }

        auto json = nlohmann::json::parse(r.text);
        if (!json.contains("data") || json["data"].is_null() || !json["data"].contains("question")) {
            std::cerr << "Invalid response when fetching hints.\n";
            return;
        }

        std::cout << "Hints for \"" << slug << "\":\n";
        int count = 1;
        for (const auto &hint_json : json["data"]["question"]["hints"]) {
            std::string hint = hint_json.get<std::string>();
            std::string cleaned_hint = std::regex_replace(hint, std::regex("<.*?>"), ""); // strip HTML tags
            std::cout << "  " << count++ << ". " << cleaned_hint << "\n";
        }
    }
}
