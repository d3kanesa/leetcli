#pragma once

#include <string>
#include <__filesystem/path.h>
#include <optional>

namespace leetcli {
    static std::filesystem::path get_home();
    std::string get_file_extension(const std::string& filename);
    void init_problems_folder();
    std::string html_to_text(const std::string& html);
    void write_markdown_file(const std::string& path, const std::string& title, const std::string& markdown);
    void write_solution_file(const std::string& path, const std::string& code);
    void launch_in_editor(const std::string& path);
    std::string get_problems_dir();
    std::string get_preferred_language();
    void set_session_cookie();
    std::string get_session_cookie();
    std::string get_csrf_token();
    int get_solution_folder(const std::string &slug, std::string &folder_path);
    std::string get_question_id(const std::string& slug, const std::string& session, const std::string& csrf);
    void fetch_testcases(const std::string& slug, const std::string& folder_path);
    std::vector<std::string> load_testcases(const std::string& filepath);
    int get_solution_filepath(const std::string& slug, std::string& solution_file,  const std::optional<std::string> &language = std::nullopt);
}
