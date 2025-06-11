#pragma once
#include <string>

namespace leetcli {
    std::string fetch_problem(const std::string& slug, const std::string& lang_override);
    std::string read_question_id_from_readme(const std::string& path);
    void solve_problem(const std::string& slug);
    void list_fetched_problems();

}
