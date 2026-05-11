cmake_minimum_required(VERSION 3.20)
project(uvc-nozzle VERSION 0.1.0 LANGUAGES CXX OBJCXX OBJC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
find_library(IOSURFACE_FRAMEWORK IOSurface REQUIRED)

set(NOZZLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NOZZLE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NOZZLE_BUILD_OPENGL OFF CACHE BOOL "" FORCE)
add_subdirectory(nozzle EXCLUDE_FROM_ALL)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "")
set(GLFW_BUILD_TESTS OFF CACHE BOOL "")
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "")
add_subdirectory(libs/glfw EXCLUDE_FROM_ALL)

set(IMGUI_SOURCES
    libs/imgui/imgui.cpp
    libs/imgui/imgui_demo.cpp
    libs/imgui/imgui_draw.cpp
    libs/imgui/imgui_tables.cpp
    libs/imgui/imgui_widgets.cpp
    libs/imgui/backends/imgui_impl_glfw.cpp
    libs/imgui/backends/imgui_impl_metal.mm
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui/backends
)
target_link_libraries(imgui PRIVATE
    glfw
    ${METAL_FRAMEWORK}
    ${APPKIT_FRAMEWORK}
)

add_executable(uvc-nozzle
    src/main.cpp
    src/capture/platform/macos/capture_device.mm
    src/publisher/publisher.mm
    src/gui/gui.mm
)

target_include_directories(uvc-nozzle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/nozzle/include
)

target_link_libraries(uvc-nozzle PRIVATE
    nozzle
    imgui
    glfw
    ${METAL_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${AVFOUNDATION_FRAMEWORK}
    ${COREVIDEO_FRAMEWORK}
    ${COREMEDIA_FRAMEWORK}
    ${APPKIT_FRAMEWORK}
    ${IOSURFACE_FRAMEWORK}
)

set_target_properties(uvc-nozzle PROPERTIES
    MACOSX_DEPLOYMENT_TARGET 12.0
)

option(UVC_BUILD_TESTS "Build unit tests" ON)

if(UVC_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.6.0
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(uvc_tests
        tests/test_main.cpp
        src/capture/platform/macos/capture_device.mm
        src/publisher/publisher.mm
    )

    target_include_directories(uvc_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/nozzle/include
    )

    target_link_libraries(uvc_tests PRIVATE
        nozzle
        Catch2::Catch2WithMain
        ${METAL_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${AVFOUNDATION_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${IOSURFACE_FRAMEWORK}
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(uvc_tests)
endif()
