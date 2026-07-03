cmake_minimum_required(VERSION 3.14)
project(leetcli)

set(CMAKE_CXX_STANDARD 17)

find_package(cpr REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(ftxui REQUIRED)

add_executable(leetcli
        src/main.cpp
        src/leetcode_api.cpp
        src/utils.cpp
        src/tui.cpp
        src/image_protocol.cpp
        src/image_cache.cpp
        include/utils.h
        include/tui.h
        include/image_protocol.h
        include/image_cache.h
)

# ▸▸▸ NEW: guarantee we get /SUBSYSTEM:CONSOLE on MSVC, no -mwindows on MinGW
set_target_properties(leetcli PROPERTIES
        WIN32_EXECUTABLE FALSE        # never build as a GUI app
)

target_include_directories(leetcli PRIVATE include third_party)
target_link_libraries(leetcli PRIVATE
        cpr::cpr
        nlohmann_json::nlohmann_json
        ftxui::screen
        ftxui::dom
        ftxui::component
)

install(TARGETS leetcli RUNTIME DESTINATION bin)
