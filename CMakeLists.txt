cmake_minimum_required(VERSION 3.21)
project(RcloneLite VERSION 1.1.4 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)

FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG v1.19.0
    GIT_SHALLOW ON
    SOURCE_SUBDIR api/cpp
)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)

set(SLINT_FEATURE_BACKEND_WINIT ON CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_SKIA OFF CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_FEMTOVG ON CACHE BOOL "" FORCE)
set(SLINT_FEATURE_RENDERER_SOFTWARE ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Slint nlohmann_json)

add_executable(RcloneLite WIN32
    src/main.cpp
    src/controller.cpp
    src/proc.cpp
    src/rclone.cpp
    src/store.cpp
    src/platform.cpp
    src/updater.cpp
    resources/app.rc
)

target_include_directories(RcloneLite PRIVATE src)
target_compile_definitions(RcloneLite PRIVATE APP_VERSION="${PROJECT_VERSION}" _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(RcloneLite PRIVATE /utf-8 /W3 /permissive- /EHsc)
    target_link_options(RcloneLite PRIVATE /ENTRY:mainCRTStartup)
endif()

target_link_libraries(RcloneLite PRIVATE Slint::Slint nlohmann_json::nlohmann_json)
set_property(TARGET RcloneLite PROPERTY SLINT_EMBED_RESOURCES embed-files)
slint_target_sources(RcloneLite ui/app.slint)

if(WIN32)
    add_custom_command(TARGET RcloneLite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:RcloneLite> $<TARGET_FILE_DIR:RcloneLite>
        COMMAND_EXPAND_LISTS)
endif()
