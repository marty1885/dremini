cmake_minimum_required(VERSION 3.15)
project(dremini)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
find_package(Drogon REQUIRED)

add_library(dremini STATIC)
target_sources(dremini PRIVATE dremini/GeminiClient.cpp
    dremini/GeminiServer.cpp
    dremini/GeminiServerPlugin.cpp
    dremini/GeminiRenderer.cpp
    dremini/GeminiParser.cpp)
target_include_directories(dremini PUBLIC .)
target_link_libraries(dremini PUBLIC Drogon::Drogon)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/dremini
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/dremini
    DESTINATION include
    FILES_MATCHING PATTERN "*.h")
install(TARGETS dremini
	LIBRARY DESTINATION lib)

option(DREMINI_BUILD_EXAMPLES "Build Dremini examples" ON)
if(DREMINI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

option(DREMINI_BUILD_TEST "Build Dremini tests" ON)
if(DREMINI_BUILD_TEST)
    add_subdirectory(tests)
endif()

