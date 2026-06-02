cmake_minimum_required(VERSION 4.2)
project(Anywheredoor)

set(CMAKE_CXX_STANDARD 20)
#find_package(OpenSSL REQUIRED)
#find_package(SQLite3 REQUIRED)

include(FetchContent)

FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.15.0 # Use the latest stable release tag
)

FetchContent_MakeAvailable(tomlplusplus)
FetchContent_MakeAvailable(spdlog)

#add_subdirectory(src)
add_executable(Anywheredoor
        src/main.cpp
        src/core/AgentConfig.cpp
        src/core/AgentConfig.h
        src/core/EventLoop.cpp
        src/core/EventLoop.h
)
# After add_executable(...)
configure_file(
        ${CMAKE_SOURCE_DIR}/agent.toml
        ${CMAKE_BINARY_DIR}/agent.toml
        COPYONLY
)

target_link_libraries(Anywheredoor PRIVATE tomlplusplus::tomlplusplus)
target_link_libraries(Anywheredoor PRIVATE spdlog::spdlog)
