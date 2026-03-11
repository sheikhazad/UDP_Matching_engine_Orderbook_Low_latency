cmake_minimum_required(VERSION 3.14)

project(matching_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(matching_engine
    src/MatchingEngineMain.cpp
    src/MatchingEngine.cpp
    src/SymbolOrderbook.cpp
    src/util/StringUtil.cpp
    src/domain/commandParser.cpp
    src/memory/OrderSlotPool.cpp
)

target_include_directories(matching_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(matching_engine PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)
