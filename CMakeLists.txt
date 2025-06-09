cmake_minimum_required(VERSION 3.14)
project(leetcli)

set(CMAKE_CXX_STANDARD 17)

find_package(cpr REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(leetcli
        src/main.cpp
        src/leetcode_api.cpp
)

target_include_directories(leetcli PRIVATE include)

target_link_libraries(leetcli PRIVATE cpr::cpr nlohmann_json::nlohmann_json)

