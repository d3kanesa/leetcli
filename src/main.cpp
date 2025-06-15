#include "leetcode_api.h"
#include "utils.h"
#include <iostream>

int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (argc < 2) {
        std::cout << "Usage:\n"
                << "  leetcli init\n"
                << "  leetcli fetch <slug>\n";
        return 1;
    }

    std::string command = argv[1];

    if (command == "init") {
        leetcli::init_problems_folder();
        return 0;
    }

    if (command == "fetch") {

        if (argc < 3) {
            std::cerr << "Usage: leetcli fetch <slug> [--lang=cpp|python|java]\n";
            return 1;
        }

        std::string slug = argv[2];

        if (slug == "daily") {
            slug = leetcli::get_daily_question_slug();
        }

        std::string lang_override;

        // Check for --lang=xxx
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.rfind("--lang=", 0) == 0) {
                lang_override = arg.substr(7); // everything after --lang=
            }
        }

        std::string problem = leetcli::fetch_problem(slug, lang_override); // pass override to function
        std::cout << problem << "\n";
        return 0;
    }

    if (command == "solve") {
        if (argc < 3) {
            std::cerr << "Usage: leetcli solve <slug>\n";
            return 1;
        }
        std::string lang_override;

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.rfind("--lang=", 0) == 0) {
                lang_override = arg.substr(7);
            }
        }
        std::string slug = argv[2];
        leetcli::solve_problem(slug, lang_override);
        return 0;
    }
    if (command == "list") {
        leetcli::list_fetched_problems();
        return 0;
    }

    if (command == "login") {
        leetcli::set_session_cookie();
        return 0;
    }
    if (command == "submit") {
        if (argc < 3) {
            std::cerr << "Usage: leetcli submit <slug> [--lang=cpp|python|java]\n";
            return 1;
        }
        std::string slug = argv[2];
        std::string lang_override;

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.rfind("--lang=", 0) == 0) {
                lang_override = arg.substr(7);
            }
        }

        leetcli::submit_solution(slug, lang_override);
        return 0;
    }
    if (command == "run") {
        if (argc < 3) {
            std::cerr << "Usage: leetcli run <slug>\n";
            return 1;
        }
        std::string slug = argv[2];
        std::string lang_override;

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.rfind("--lang=", 0) == 0) {
                lang_override = arg.substr(7);
            }
        }
        leetcli::run_tests(slug, lang_override);
        return 0;
    } if (command == "config") {
        leetcli::handle_config_command(args);
        return 0;
    }
    if (command == "runtime") {
        if (argc < 3) {
            std::cerr << "Usage: leetcli runtime <slug>\n";
            return 1;
        }
        std::string slug = argv[2];
        std::string lang_override;

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.rfind("--lang=", 0) == 0) {
                lang_override = arg.substr(7);
            }
        }
        leetcli::analyze_runtime(slug, lang_override);
        return 0;
    }

    std::cerr << "Unknown command: " << command << "\n";
    return 1;
}
