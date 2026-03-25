cmake_minimum_required(VERSION 3.20)
project(UnsufferedTheFallen VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h")

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL2::SDL2 SDL2::SDL2main OpenGL::GL GLEW::GLEW glm::glm
    assimp::assimp EnTT::EnTT OpenAL::OpenAL
    nlohmann_json::nlohmann_json freetype
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /O2)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -O2)
endif()
