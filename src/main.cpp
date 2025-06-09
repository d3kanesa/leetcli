#include <iostream>
#include "leetcode_api.h"

int main(int argc, char** argv) {
    if (argc < 3 || std::string(argv[1]) != "fetch") {
        std::cout << "Usage: leetcli fetch <slug>\n";
        return 1;
    }

    std::string slug = argv[2];
    std::string problem = leetcli::fetch_problem(slug);
    std::cout << problem << "\n";

    return 0;
}
