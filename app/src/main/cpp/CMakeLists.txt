cmake_minimum_required(VERSION 3.22.1)

project(mobilestt_native)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)

set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_CPU_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)

# GPU/Vulkan은 나중 단계에서 켭니다.
# 지금은 네이티브 연결 확인을 위해 CPU 빌드만 먼저 성공시키는 단계입니다.
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/whisper.cpp/CMakeLists.txt")
    message(FATAL_ERROR "whisper.cpp not found. GitHub Actions must clone it to app/src/main/cpp/whisper.cpp")
endif()

add_subdirectory(
        whisper.cpp
        whisper-build
)

add_library(
        mobilestt
        SHARED
        native-lib.cpp
)

target_include_directories(
        mobilestt
        PRIVATE
        whisper.cpp
        whisper.cpp/include
        whisper.cpp/ggml/include
)

find_library(log-lib log)

target_link_libraries(
        mobilestt
        whisper
        ${log-lib}
)

