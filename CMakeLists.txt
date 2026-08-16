cmake_minimum_required(VERSION 3.23)

project(luogu-export VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(ZLIB REQUIRED)

file(GLOB_RECURSE LUOGU_EXPORT_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(luogu-export
    ${LUOGU_EXPORT_SOURCES}
)

target_compile_features(luogu-export PRIVATE cxx_std_17)

target_include_directories(luogu-export PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(luogu-export PRIVATE
    -Wall
    -Wextra
)

target_link_libraries(luogu-export PRIVATE
    CURL::libcurl
    LibXml2::LibXml2
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
)