#include "leetcode_api.h"
#include "utils.h"
#include <iostream>

int main(int argc, char** argv) {
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
            std::cerr << "Usage: leetcli fetch <slug>\n";
            return 1;
        }
        std::string slug = argv[2];
        std::string problem = leetcli::fetch_problem(slug);
        std::cout << problem << "\n";
        return 0;
    }

    std::cerr << "Unknown command: " << command << "\n";
    return 1;
}
