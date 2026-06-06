cmake_minimum_required(VERSION 3.22)
project(OmniTrader)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED)
find_package(fmt REQUIRED)
find_package(Quill REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(glaze REQUIRED)
find_package(CURL REQUIRED)
find_package(websocketpp REQUIRED)
find_package(concurrentqueue REQUIRED)
find_package(OpenSSL REQUIRED)

# All shared/library sources (everything under src/ except the two entrypoints).
file(GLOB_RECURSE OMNI_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM OMNI_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/listener_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/trader_main.cpp
)

add_library(omni_core STATIC ${OMNI_CORE_SOURCES})
target_include_directories(omni_core PUBLIC src)
target_link_libraries(omni_core
    PUBLIC
        boost::boost
        fmt::fmt
        quill::quill
        nlohmann_json::nlohmann_json
        glaze::glaze
        CURL::libcurl
        websocketpp::websocketpp
        concurrentqueue::concurrentqueue
        OpenSSL::SSL
        OpenSSL::Crypto
)

add_executable(listener src/listener_main.cpp)
target_link_libraries(listener PRIVATE omni_core)

add_executable(trader src/trader_main.cpp)
target_link_libraries(trader PRIVATE omni_core)
