cmake_minimum_required(VERSION 3.20)

if(APPLE)
    project(uvc-nozzle VERSION 0.1.0 LANGUAGES CXX OBJCXX OBJC)
else()
    project(uvc-nozzle VERSION 0.1.0 LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Nozzle ---
set(NOZZLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NOZZLE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NOZZLE_BUILD_OPENGL OFF CACHE BOOL "" FORCE)
add_subdirectory(nozzle EXCLUDE_FROM_ALL)

# --- GLFW ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "")
set(GLFW_BUILD_TESTS OFF CACHE BOOL "")
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "")
add_subdirectory(libs/glfw EXCLUDE_FROM_ALL)

# --- Platform dependencies ---
if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(IOSURFACE_FRAMEWORK IOSurface REQUIRED)
elseif(UNIX)
    find_package(OpenGL REQUIRED)
endif()

# --- ImGui ---
set(IMGUI_SOURCES
    libs/imgui/imgui.cpp
    libs/imgui/imgui_demo.cpp
    libs/imgui/imgui_draw.cpp
    libs/imgui/imgui_tables.cpp
    libs/imgui/imgui_widgets.cpp
    libs/imgui/backends/imgui_impl_glfw.cpp
)

if(APPLE)
    list(APPEND IMGUI_SOURCES libs/imgui/backends/imgui_impl_metal.mm)
elseif(WIN32)
    list(APPEND IMGUI_SOURCES libs/imgui/backends/imgui_impl_dx11.cpp)
else()
    list(APPEND IMGUI_SOURCES libs/imgui/backends/imgui_impl_opengl3.cpp)
endif()

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui/backends
)

if(APPLE)
    target_link_libraries(imgui PRIVATE glfw ${METAL_FRAMEWORK} ${APPKIT_FRAMEWORK})
elseif(WIN32)
    target_link_libraries(imgui PRIVATE glfw)
else()
    target_link_libraries(imgui PRIVATE glfw OpenGL::GL)
endif()

# --- Platform-specific sources ---
set(UVC_CAPTURE_SRC
    $<$<PLATFORM_ID:Darwin>:src/capture/platform/macos/capture_device.mm>
    $<$<PLATFORM_ID:Windows>:src/capture/platform/windows/capture_device.cpp>
    $<$<PLATFORM_ID:Linux>:src/capture/platform/linux/capture_device.cpp>
)

set(UVC_PUBLISHER_SRC
    $<$<PLATFORM_ID:Darwin>:src/publisher/publisher.mm>
    $<$<NOT:$<PLATFORM_ID:Darwin>>:src/publisher/publisher.cpp>
)

set(UVC_GUI_BACKEND_SRC
    $<$<PLATFORM_ID:Darwin>:src/gui/platform/macos/render_backend_metal.mm>
    $<$<PLATFORM_ID:Windows>:src/gui/platform/windows/render_backend_d3d11.cpp>
    $<$<PLATFORM_ID:Linux>:src/gui/platform/linux/render_backend_opengl.cpp>
)

# --- uvc-nozzle executable ---
if(APPLE)
    add_executable(uvc-nozzle MACOSX_BUNDLE
        src/main.cpp
        ${UVC_CAPTURE_SRC}
        ${UVC_PUBLISHER_SRC}
        src/gui/gui.cpp
        ${UVC_GUI_BACKEND_SRC}
    )
else()
    add_executable(uvc-nozzle
        src/main.cpp
        ${UVC_CAPTURE_SRC}
        ${UVC_PUBLISHER_SRC}
        src/gui/gui.cpp
        ${UVC_GUI_BACKEND_SRC}
    )
endif()

target_include_directories(uvc-nozzle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/nozzle/include
)

if(APPLE)
    target_link_libraries(uvc-nozzle PRIVATE
        nozzle imgui glfw
        ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK}
        ${AVFOUNDATION_FRAMEWORK} ${COREVIDEO_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK} ${APPKIT_FRAMEWORK} ${IOSURFACE_FRAMEWORK}
    )
elseif(WIN32)
    target_link_libraries(uvc-nozzle PRIVATE
        nozzle imgui glfw
        d3d11 dxgi d3dcompiler
        mfplat mf mfreadwrite mfuuid strmiids ole32 uuid
    )
else()
    target_link_libraries(uvc-nozzle PRIVATE
        nozzle imgui glfw OpenGL::GL
    )
endif()

if(APPLE)
    set_target_properties(uvc-nozzle PROPERTIES
        MACOSX_DEPLOYMENT_TARGET 12.0
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.nozzle.uvc-nozzle"
        MACOSX_BUNDLE_BUNDLE_NAME "uvc-nozzle"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )
endif()

# --- Tests ---
option(UVC_BUILD_TESTS "Build unit tests" ON)

if(UVC_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.6.0
    )
    FetchContent_MakeAvailable(Catch2)

    set(UVC_TEST_CAPTURE_SRC
        $<$<PLATFORM_ID:Darwin>:src/capture/platform/macos/capture_device.mm>
        $<$<PLATFORM_ID:Windows>:src/capture/platform/windows/capture_device.cpp>
        $<$<PLATFORM_ID:Linux>:src/capture/platform/linux/capture_device.cpp>
    )

    set(UVC_TEST_PUBLISHER_SRC
        $<$<PLATFORM_ID:Darwin>:src/publisher/publisher.mm>
        $<$<NOT:$<PLATFORM_ID:Darwin>>:src/publisher/publisher.cpp>
    )

    add_executable(uvc_tests
        tests/test_main.cpp
        ${UVC_TEST_CAPTURE_SRC}
        ${UVC_TEST_PUBLISHER_SRC}
    )

    target_include_directories(uvc_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/nozzle/include
    )

    if(APPLE)
        target_link_libraries(uvc_tests PRIVATE
            nozzle Catch2::Catch2WithMain
            ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK}
            ${AVFOUNDATION_FRAMEWORK} ${COREVIDEO_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK} ${APPKIT_FRAMEWORK} ${IOSURFACE_FRAMEWORK}
        )
    elseif(WIN32)
        target_link_libraries(uvc_tests PRIVATE
            nozzle Catch2::Catch2WithMain
            mfplat mf mfreadwrite mfuuid strmiids ole32 uuid
        )
    else()
        target_link_libraries(uvc_tests PRIVATE
            nozzle Catch2::Catch2WithMain OpenGL::GL
        )
    endif()

    include(CTest)
    include(Catch)
    catch_discover_tests(uvc_tests)
endif()
