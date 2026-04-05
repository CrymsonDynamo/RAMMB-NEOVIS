cmake_minimum_required(VERSION 3.24)
project(rammb-neovis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies via FetchContent ────────────────────────────────────────────
include(FetchContent)

# Dear ImGui
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

# stb (header-only)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(stb)

# ── System dependencies (install via apt) ────────────────────────────────────
find_package(OpenGL REQUIRED)
find_package(GLEW   REQUIRED)
find_package(glfw3  REQUIRED)
find_package(CURL   REQUIRED)
find_package(SQLite3 REQUIRED)

# FFmpeg (libav*)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC  REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL   REQUIRED libavutil)
pkg_check_modules(SWSCALE  REQUIRED libswscale)

# GLM (header-only, find manually if find_package fails)
find_package(glm QUIET)
if(NOT glm_FOUND)
    find_path(GLM_INCLUDE_DIR glm/glm.hpp)
endif()

# nlohmann/json (header-only)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
endif()

# ── ImGui as a library target ────────────────────────────────────────────────
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)

target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)

target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

# ── RAMMB-NEOVIS executable ───────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/animation.cpp
    src/renderer/shader.cpp
    src/renderer/renderer.cpp
    src/network/http_client.cpp
    src/tile_manager.cpp
    src/ui/sidebar.cpp
    src/ui/scene_bar.cpp
    src/ui/timeline.cpp
    src/ui/export_panel.cpp
    src/ui/tools_panel.cpp
    src/export/exporter.cpp
    src/stb_impl.cpp)

add_executable(rammb-neovis ${SOURCES})

target_include_directories(rammb-neovis PRIVATE
    src
    ${stb_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)

if(TARGET glm::glm)
    target_link_libraries(rammb-neovis PRIVATE glm::glm)
elseif(DEFINED GLM_INCLUDE_DIR)
    target_include_directories(rammb-neovis PRIVATE ${GLM_INCLUDE_DIR})
endif()

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(rammb-neovis PRIVATE nlohmann_json::nlohmann_json)
elseif(DEFINED NLOHMANN_JSON_INCLUDE_DIR)
    target_include_directories(rammb-neovis PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
endif()

target_include_directories(rammb-neovis PRIVATE
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS})

target_link_libraries(rammb-neovis PRIVATE
    OpenGL::GL
    GLEW::GLEW
    glfw
    CURL::libcurl
    SQLite::SQLite3
    imgui_lib
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES})

# Copy shaders next to the binary after build
add_custom_command(TARGET rammb-neovis POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:rammb-neovis>/shaders
    COMMENT "Copying shaders")

# ── Compiler flags ───────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(rammb-neovis PRIVATE /W4 /WX-)
else()
    target_compile_options(rammb-neovis PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Install rules (used by CPack and `cmake --install`) ──────────────────────
include(GNUInstallDirs)

install(TARGETS rammb-neovis
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})            # /usr/bin/rammb-neovis

install(DIRECTORY ${CMAKE_SOURCE_DIR}/shaders/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/rammb-neovis/shaders) # /usr/share/rammb-neovis/shaders/

# .desktop entry so it shows up in app launchers
configure_file(
    ${CMAKE_SOURCE_DIR}/packaging/rammb-neovis.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/rammb-neovis.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rammb-neovis.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# ── CPack — generates .deb (and .tar.gz as a bonus) ──────────────────────────
set(CPACK_PACKAGE_NAME            "rammb-neovis")
set(CPACK_PACKAGE_VENDOR          "CrymsonDynamo")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Native satellite imagery viewer — RAMMB Slider replacement")
set(CPACK_PACKAGE_VERSION         "0.1.0")
set(CPACK_PACKAGE_VERSION_MAJOR   "0")
set(CPACK_PACKAGE_VERSION_MINOR   "1")
set(CPACK_PACKAGE_VERSION_PATCH   "0")
set(CPACK_PACKAGE_CONTACT         "https://github.com/CrymsonDynamo/RAMMB-NEOVIS")
set(CPACK_PACKAGE_HOMEPAGE_URL    "https://github.com/CrymsonDynamo/RAMMB-NEOVIS")

# .deb specific
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "CrymsonDynamo")
set(CPACK_DEBIAN_PACKAGE_SECTION    "science")
set(CPACK_DEBIAN_PACKAGE_PRIORITY   "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libglfw3 (>= 3.3), libglew2.2, libcurl4, \
libavcodec60 | libavcodec-extra60 | libavcodec59 | libavcodec-extra59, \
libavformat60 | libavformat59, \
libavutil58 | libavutil57, \
libswscale7 | libswscale6, \
libgl1, zenity")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "RAMMB-NEOVIS — native C++/OpenGL satellite imagery viewer\n"
    " Browse and animate GOES, Himawari and JPSS imagery from the RAMMB Slider\n"
    " archive. Supports MP4/GIF/PNG export, regional tile selection, scene tabs,\n"
    " and date range queries.")

set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-linux-amd64")

include(CPack)
