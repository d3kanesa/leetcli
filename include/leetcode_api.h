#pragma once
#include <string>
#include <vector>

namespace leetcli {
    std::string get_daily_question_slug();
    std::string fetch_problem(const std::string& slug, const std::string& lang_override);
    std::string read_question_id_from_readme(const std::string& path);
    void solve_problem(const std::string& slug, const std::string &lang_override);
    void list_fetched_problems();
    void run_tests(const std::string& slug, const std::string &lang_override);
    void run_problem(const std::string& slug, const std::string& lang, const std::string& question_id,
    const std::string& code, const std::string& test_input, const std::string& session, const std::string& csrf);
    void submit_solution(const std::string& slug, const std::string &lang_override);
    void handle_config_command(const std::vector<std::string>& args);
    void analyze_runtime(const std::string& slug, const std::string &lang_override);
    void give_hint(const std::string& slug, const std::string &lang_override);
    void fetch_problem_topics(const std::string &slug);
    void fetch_problem_hints(const std::string &slug);
}
