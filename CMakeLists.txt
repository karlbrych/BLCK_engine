cmake_minimum_required(VERSION 3.21)
project(blck_engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Single-config generators (Makefiles, Ninja) start with no build type at all.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Keep the executable in one predictable place across generators, so the
# debugger configs and the asset copy below always agree on the path.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(output_config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${output_config} "${CMAKE_BINARY_DIR}/bin")
endforeach()

# Used as a fallback everywhere a package ships only a .pc file.
find_package(PkgConfig QUIET)

# ---------------------------------------------------------------- OpenGL
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------- Lua / Sol2 scripting
# Pin Lua 5.4 deliberately: sol2 supports 5.1 through 5.4 and #errors on anything
# newer. Distros that ship Lua 5.5 as the default put a 5.5 lua.h in /usr/include,
# so the 5.4 include directory this imported target carries must reach every
# translation unit that includes <sol/sol.hpp>, ahead of the default search path.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua5.4)
find_package(sol2 CONFIG QUIET)
if(NOT sol2_FOUND)
    find_path(SOL2_INCLUDE_DIR NAMES sol/sol.hpp PATHS /usr/include /usr/local/include)
    if(SOL2_INCLUDE_DIR)
        message(STATUS "Using system Sol2 headers at ${SOL2_INCLUDE_DIR}")
    else()
        include(FetchContent)
        set(SOL2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(sol2
            URL https://github.com/ThePhD/sol2/archive/refs/tags/v3.2.3.tar.gz
        )
        FetchContent_MakeAvailable(sol2)
    endif()
endif()

# ---------------------------------------------------------------- Window backend
find_package(glfw3 CONFIG QUIET)
if(TARGET glfw)
    set(BACKEND_LIB glfw)
elseif(PkgConfig_FOUND)
    pkg_check_modules(GLFW3_PC QUIET IMPORTED_TARGET glfw3)
    if(TARGET PkgConfig::GLFW3_PC)
        set(BACKEND_LIB PkgConfig::GLFW3_PC)
    endif()
endif()

if(NOT BACKEND_LIB)
    message(FATAL_ERROR
        "GLFW was not found. Install it with one of:\n"
        "  Debian/Ubuntu : sudo apt install libglfw3-dev\n"
        "  Fedora        : sudo dnf install glfw-devel\n"
        "  Arch          : sudo pacman -S glfw\n"
        "  macOS         : brew install glfw\n"
        "  vcpkg         : vcpkg install glfw3 (configure with the vcpkg toolchain file)")
endif()

# ---------------------------------------------------------------- OpenGL loader
set(GLAD_SOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad")
if(NOT EXISTS "${GLAD_SOURCES_DIR}/cmake/CMakeLists.txt")
    message(FATAL_ERROR
        "third_party/glad is missing. Fetch it with:\n"
        "  git clone --depth 1 --branch glad2 https://github.com/Dav1dde/glad.git third_party/glad")
endif()

find_package(Python COMPONENTS Interpreter REQUIRED)

# Unlike glad 0.1, the glad2 generator depends on Jinja2. Check for it now so
# the failure is one actionable line instead of a traceback during the build.
execute_process(
    COMMAND "${Python_EXECUTABLE}" -c "import jinja2"
    RESULT_VARIABLE GLAD2_JINJA_RESULT
    OUTPUT_QUIET
    ERROR_QUIET
)
if(NOT GLAD2_JINJA_RESULT EQUAL 0)
    message(FATAL_ERROR
        "The glad2 generator needs the Jinja2 Python module:\n"
        "  \"${Python_EXECUTABLE}\" -m pip install --user jinja2\n"
        "Or regenerate this project with the 'glad' loader, which has no such dependency.")
endif()

add_subdirectory("${GLAD_SOURCES_DIR}/cmake" glad_build)
glad_add_library(glad_loader REPRODUCIBLE LOADER API gl:core=4.6)
set(LOADER_LIB glad_loader)

# ---------------------------------------------------------------- Application
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

file(GLOB APP_INCLUDE_DIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
set(PROJECT_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
foreach(include_dir IN LISTS APP_INCLUDE_DIRS)
    if(IS_DIRECTORY "${include_dir}")
        list(APPEND PROJECT_INCLUDE_DIRS "${include_dir}")
    endif()
endforeach()

add_executable(${PROJECT_NAME} ${APP_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::LUA)

if(TARGET sol2::sol2)
    target_link_libraries(${PROJECT_NAME} PRIVATE sol2::sol2)
elseif(TARGET sol2)
    target_link_libraries(${PROJECT_NAME} PRIVATE sol2)
elseif(SOL2_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE "${SOL2_INCLUDE_DIR}")
else()
    target_include_directories(${PROJECT_NAME} PRIVATE "${sol2_SOURCE_DIR}/include")
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        OpenGL::GL
        ${BACKEND_LIB}
        ${LOADER_LIB}
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------- Debug tooling
# Gates the in-engine debug rendering (wireframe today). Not called plain DEBUG:
# that name is claimed by half the C libraries in existence, and colliding with
# one of them is a bad afternoon. Turn it off with -DBLCK_DEBUG=OFF and the
# setters compile away, leaving flags that can never become true.
option(BLCK_DEBUG "Compile the in-engine debug rendering tools (wireframe)" ON)
if(BLCK_DEBUG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE BLCK_DEBUG)
    message(STATUS "Debug rendering tools: enabled (BLCK_DEBUG)")
else()
    message(STATUS "Debug rendering tools: compiled out")
endif()

# ---------------------------------------------------------------- Optional libraries
# Links PKG to the executable if it can be found, and defines DEFINE when it is.
# Extra arguments are the imported target names to try, in order.
function(app_use_optional PKG DEFINE)
    find_package(${PKG} CONFIG QUIET)
    foreach(candidate IN LISTS ARGN)
        if(TARGET ${candidate})
            target_link_libraries(${PROJECT_NAME} PRIVATE ${candidate})
            target_compile_definitions(${PROJECT_NAME} PRIVATE ${DEFINE})
            message(STATUS "Optional dependency ${PKG}: using ${candidate}")
            return()
        endif()
    endforeach()

    if(PkgConfig_FOUND)
        string(TOLOWER "${PKG}" pc_module)
        pkg_check_modules(OPT_${PKG} QUIET IMPORTED_TARGET ${pc_module})
        if(TARGET PkgConfig::OPT_${PKG})
            target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::OPT_${PKG})
            target_compile_definitions(${PROJECT_NAME} PRIVATE ${DEFINE})
            message(STATUS "Optional dependency ${PKG}: using pkg-config module ${pc_module}")
            return()
        endif()
    endif()

    message(STATUS "Optional dependency ${PKG}: not found, skipped")
endfunction()

# glm is a hard requirement now, not an optional extra: EngineCamera builds its
# view/projection matrices with it and the renderer hands those straight to the
# shaders. HAVE_GLM stays defined so Mesh.h and Shader.h keep their glm paths.
find_package(glm CONFIG QUIET)
if(TARGET glm::glm)
    target_link_libraries(${PROJECT_NAME} PRIVATE glm::glm)
elseif(TARGET glm)
    target_link_libraries(${PROJECT_NAME} PRIVATE glm)
else()
    find_path(GLM_INCLUDE_DIR NAMES glm/glm.hpp)
    if(NOT GLM_INCLUDE_DIR)
        message(FATAL_ERROR
            "glm was not found. Install it with one of:\n"
            "  Debian/Ubuntu : sudo apt install libglm-dev\n"
            "  Fedora        : sudo dnf install glm-devel\n"
            "  Arch          : sudo pacman -S glm\n"
            "  macOS         : brew install glm\n"
            "  vcpkg         : vcpkg install glm")
    endif()
    target_include_directories(${PROJECT_NAME} PRIVATE "${GLM_INCLUDE_DIR}")
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_GLM)

app_use_optional(imgui HAVE_IMGUI imgui::imgui)
app_use_optional(assimp HAVE_ASSIMP assimp::assimp)
app_use_optional(fmt HAVE_FMT fmt::fmt)
app_use_optional(spdlog HAVE_SPDLOG spdlog::spdlog)

# stb_image is a single header, so it has no link target -- just an include dir.
# It decodes the textures embedded in glTF/GLB models, so a copy is vendored in
# third_party/ as the fallback: without it Model.cpp still loads geometry, but
# every material comes out as a flat colour.
find_path(STB_IMAGE_INCLUDE_DIR NAMES stb_image.h PATH_SUFFIXES stb)
if(NOT STB_IMAGE_INCLUDE_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/stb_image.h")
    set(STB_IMAGE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
endif()
if(STB_IMAGE_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE "${STB_IMAGE_INCLUDE_DIR}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_STB_IMAGE)
    message(STATUS "Optional dependency stb_image: using ${STB_IMAGE_INCLUDE_DIR}")

    # stb_image is not written to this project's warning level, and it is the
    # only translation unit that compiles it, so silence just that one file
    # instead of lowering the bar for everything.
    if(MSVC)
        set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/backend/stb_image_impl.cpp"
            PROPERTIES COMPILE_OPTIONS "/w")
    else()
        set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/backend/stb_image_impl.cpp"
            PROPERTIES COMPILE_OPTIONS "-w")
    endif()
else()
    message(STATUS "Optional dependency stb_image: not found, model textures disabled")
endif()

# ---------------------------------------------------------------- Assets
# Shaders, textures and models are loaded relative to the executable at runtime.
# Linked rather than copied where the platform allows it: models run to tens of
# megabytes, and copying them on every build is minutes of nothing useful.
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
        COMMENT "Copying assets next to the executable"
    )
else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
        COMMENT "Linking assets next to the executable"
    )
endif()

# ---------------------------------------------------------------- Gameplay scripts
# The scripts live in src/gameplay/ but are looked up as gameplay/ relative to
# the executable, so the deployed layout stays flat next to assets/. They are
# linked rather than copied where the platform allows it, so editing a script
# hot-reloads in the running app instead of leaving a stale copy behind.
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/gameplay"
        COMMENT "Copying gameplay scripts next to the executable"
    )
else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${PROJECT_NAME}>/gameplay"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/gameplay"
        COMMENT "Linking gameplay scripts next to the executable"
    )
endif()
