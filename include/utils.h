#pragma once

#include <string>

namespace leetcli {
    void init_problems_folder();
    std::string html_to_text(const std::string& html);
    void write_markdown_file(const std::string& path, const std::string& title, const std::string& markdown);
    void write_solution_file(const std::string& path, const std::string& code);
    void launch_in_editor(const std::string& path);
    std::string get_problems_dir();
    std::string get_preferred_language();

}
