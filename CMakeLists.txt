cmake_minimum_required(VERSION 3.22)
project(TomCatLinux LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR EMSCRIPTEN OR ANDROID)
    message(FATAL_ERROR "This entry point targets desktop Linux. Use Premake on Windows or Web/CMakeLists.txt for Emscripten.")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    message(FATAL_ERROR "The initial Linux port targets x86_64.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(TC_SRC "${CMAKE_CURRENT_SOURCE_DIR}/TomCat/src")
set(TC_VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/TomCat/vendor")
foreach(dependency GLFW glm spdlog Box2D ImGuizmo)
    if(NOT EXISTS "${TC_VENDOR}/${dependency}/.git")
        message(FATAL_ERROR "Initialize TomCat/vendor/${dependency} with git submodule update --init --recursive TomCat/vendor/${dependency}")
    endif()
endforeach()

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(OpenAL REQUIRED)
find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SHADERC REQUIRED IMPORTED_TARGET shaderc_combined)
find_package(glslang CONFIG REQUIRED)
find_package(SPIRV-Tools CONFIG REQUIRED)
find_package(SPIRV-Tools-opt CONFIG REQUIRED)
# Ubuntu 24.04's SPIRV-Cross predates plain-uniform reflection used by cooked shaders.
include(FetchContent)
foreach(feature CLI TESTS HLSL MSL CPP REFLECT C_API UTIL)
    set(SPIRV_CROSS_ENABLE_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spirv_cross
    URL https://github.com/KhronosGroup/SPIRV-Cross/archive/d8e3e2b141b8c8a167b2e3984736a6baacff316c.tar.gz
    URL_HASH SHA256=18e4abb4ae97451c3d90bb23eb1c22859c8ef08bd5d8d3ec09e035846bbf6556
    SOURCE_DIR "${CMAKE_BINARY_DIR}/dependencies/spirv_cross")
FetchContent_MakeAvailable(spirv_cross)

# Use the pinned GLFW fork (including the Editor's window styling extensions).
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "X11/XWayland is the initial Linux backend")
add_subdirectory("${TC_VENDOR}/GLFW" dependencies/glfw EXCLUDE_FROM_ALL)

file(GLOB_RECURSE TC_BOX2D CONFIGURE_DEPENDS "${TC_VENDOR}/Box2D/src/*.cpp")
add_library(tc_box2d STATIC ${TC_BOX2D})
target_include_directories(tc_box2d PUBLIC "${TC_VENDOR}/Box2D/include" PRIVATE "${TC_VENDOR}/Box2D/src")
file(GLOB TC_YAML CONFIGURE_DEPENDS "${TC_VENDOR}/yaml-cpp/src/*.cpp")
add_library(tc_yaml STATIC ${TC_YAML})
target_include_directories(tc_yaml PUBLIC "${TC_VENDOR}/yaml-cpp/include")
target_compile_definitions(tc_yaml PUBLIC YAML_CPP_STATIC_DEFINE)
add_library(tc_glad STATIC "${TC_VENDOR}/Glad/src/glad.c")
target_include_directories(tc_glad PUBLIC "${TC_VENDOR}/Glad/include")
target_link_libraries(tc_glad PUBLIC ${CMAKE_DL_LIBS})
add_library(tc_imgui STATIC
    "${TC_VENDOR}/ImGui/imgui.cpp" "${TC_VENDOR}/ImGui/imgui_draw.cpp"
    "${TC_VENDOR}/ImGui/imgui_tables.cpp" "${TC_VENDOR}/ImGui/imgui_widgets.cpp"
    "${TC_VENDOR}/ImGui/imgui_demo.cpp" "${TC_VENDOR}/ImGuizmo/ImGuizmo.cpp")
target_include_directories(tc_imgui PUBLIC "${TC_VENDOR}/ImGui" "${TC_VENDOR}/ImGuizmo")

file(GLOB_RECURSE TC_SOURCES CONFIGURE_DEPENDS "${TC_SRC}/*.cpp")
list(FILTER TC_SOURCES EXCLUDE REGEX "/platform/Web/")
list(REMOVE_ITEM TC_SOURCES "${TC_SRC}/platform/Window/WindowsPlatformUtils.cpp")
file(GLOB TC_ASTC CONFIGURE_DEPENDS "${TC_VENDOR}/astcenc/Source/astcenc_*.cpp")
file(GLOB_RECURSE TC_ETC CONFIGURE_DEPENDS "${TC_VENDOR}/etc2comp/EtcLib/*.cpp")
set_source_files_properties(${TC_ASTC} PROPERTIES COMPILE_DEFINITIONS
    "ASTCENC_NEON=0;ASTCENC_SVE=0;ASTCENC_SSE=20;ASTCENC_AVX=0;ASTCENC_POPCNT=0;ASTCENC_F16C=0")
add_library(TomCat STATIC ${TC_SOURCES} ${TC_ASTC} ${TC_ETC}
    "${TC_VENDOR}/harfbuzz/src/harfbuzz.cc" "${TC_VENDOR}/stb_image/stb_image.cpp")
target_include_directories(TomCat PUBLIC
    "${TC_SRC}" "${TC_VENDOR}" "${TC_VENDOR}/spdlog/include" "${TC_VENDOR}/glm"
    "${TC_VENDOR}/entt/include" "${TC_VENDOR}/stb_image" "${CMAKE_BINARY_DIR}/dependencies"
    PRIVATE "${TC_VENDOR}/etc2comp/EtcLib/Etc" "${TC_VENDOR}/etc2comp/EtcLib/EtcCodec")
target_compile_definitions(TomCat PUBLIC TC_PLATFORM_LINUX=1 GLFW_INCLUDE_NONE
    "$<$<CONFIG:Debug>:TC_DEBUG>" "$<$<NOT:$<CONFIG:Debug>>:TC_RELEASE>"
    PRIVATE _GNU_SOURCE)
target_link_libraries(TomCat PUBLIC tc_box2d tc_yaml tc_glad tc_imgui glfw
    Threads::Threads OpenGL::GL OpenAL::OpenAL Vulkan::Vulkan ${CMAKE_DL_LIBS}
    PkgConfig::SHADERC glslang::SPIRV glslang::glslang SPIRV-Tools-opt SPIRV-Tools-static
    spirv-cross-glsl spirv-cross-core)

function(tc_application target source output packages)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${source}/*.cpp")
    add_executable(${target} ${sources})
    target_link_libraries(${target} PRIVATE TomCat)
    target_include_directories(${target} PRIVATE "${source}")
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${output}")
    if(packages)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${packages}" "$<TARGET_FILE_DIR:${target}>/Packages")
    endif()
endfunction()

tc_application(TomCatInut "${CMAKE_CURRENT_SOURCE_DIR}/Editor/TomCatInut/src" Editor "${CMAKE_CURRENT_SOURCE_DIR}/Editor/TomCatInut/Packages")
tc_application(Manager "${CMAKE_CURRENT_SOURCE_DIR}/Builder/Manager/src" Hub "${CMAKE_CURRENT_SOURCE_DIR}/Builder/Manager/Packages")
tc_application(TomCatPlayer "${CMAKE_CURRENT_SOURCE_DIR}/Player/TomCatPlayer/src" Player "")

set(TC_EDITOR_SRC "${CMAKE_CURRENT_SOURCE_DIR}/Editor/TomCatInut/src")
add_library(tc_build_tools STATIC
    "${TC_EDITOR_SRC}/Scripting/ScriptProjectCompiler.cpp"
    "${TC_EDITOR_SRC}/Scripting/ScriptMetadataCache.cpp"
    "${TC_EDITOR_SRC}/Player/PlayerBuilder.cpp")
target_include_directories(tc_build_tools PUBLIC "${TC_EDITOR_SRC}")
target_link_libraries(tc_build_tools PUBLIC TomCat)
tc_application(TomCatCLI "${CMAKE_CURRENT_SOURCE_DIR}/Tools/TomCatCLI/src" CLI "${CMAKE_CURRENT_SOURCE_DIR}/Editor/TomCatInut/Packages")
target_link_libraries(TomCatCLI PRIVATE tc_build_tools)

add_custom_target(tc_managed ALL
    COMMAND "${DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Managed/TomCat.Managed.slnx"
        --configuration Release --nologo -nodeReuse:false -p:UseSharedCompilation=false
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
set(TC_MANAGED_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/Managed/TomCat.ScriptHost/bin/Release/net10.0")
set(TC_MANAGED_FILES
    "${TC_MANAGED_OUTPUT}/TomCat.Managed.dll"
    "${TC_MANAGED_OUTPUT}/TomCat.ScriptHost.dll"
    "${TC_MANAGED_OUTPUT}/TomCat.ScriptHost.runtimeconfig.json"
    "${TC_MANAGED_OUTPUT}/TomCat.ScriptHost.deps.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/Managed/TomCat.ScriptGenerator/bin/Release/net10.0/TomCat.ScriptGenerator.dll")
foreach(app TomCatInut TomCatCLI)
    add_dependencies(${app} tc_managed)
    add_custom_target(${app}_managed ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${app}>/Managed"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TC_MANAGED_FILES} "$<TARGET_FILE_DIR:${app}>/Managed"
        DEPENDS ${app} tc_managed VERBATIM)
endforeach()

include(CTest)
if(BUILD_TESTING)
    foreach(test InputRegression SaveDataRegression LinuxPlatformRegression AudioRegression LinuxAudioRegression)
        file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/${test}/src/*.cpp")
        add_executable(${test} ${sources})
        target_link_libraries(${test} PRIVATE TomCat)
        add_test(NAME ${test} COMMAND ${test})
        set_tests_properties(${test} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 60 ENVIRONMENT "TC_RENDERER=opengl")
    endforeach()
    add_test(NAME PlayerVersion COMMAND TomCatPlayer --version)
    add_test(NAME LinuxAudioDevice COMMAND LinuxAudioRegression --device)
    set_tests_properties(LinuxAudioDevice PROPERTIES ENVIRONMENT "ALSOFT_DRIVERS=null" TIMEOUT 30)
    add_test(NAME LinuxAudioUnavailable COMMAND LinuxAudioRegression --unavailable)
    set_tests_properties(LinuxAudioUnavailable PROPERTIES ENVIRONMENT "ALSOFT_DRIVERS=tomcat-unavailable-backend" TIMEOUT 30)
    add_test(NAME EditorIdentity COMMAND TomCatInut --product-info-json)
    add_test(NAME HubIdentity COMMAND Manager --product-info-json)
    set_tests_properties(PlayerVersion EditorIdentity HubIdentity PROPERTIES TIMEOUT 30 ENVIRONMENT "TC_RENDERER=opengl")
    add_executable(LinuxRuntimeRegression Tests/LinuxRuntimeRegression/src/LinuxRuntimeRegression.cpp)
    target_link_libraries(LinuxRuntimeRegression PRIVATE tc_build_tools)
    add_dependencies(LinuxRuntimeRegression TomCatCLI_managed TomCatInut_managed)
    find_program(XVFB_RUN xvfb-run REQUIRED)
    add_test(NAME LinuxRuntimeRegression COMMAND "${XVFB_RUN}" -a "$<TARGET_FILE:LinuxRuntimeRegression>"
        "$<TARGET_FILE:TomCatCLI>" "${CMAKE_BINARY_DIR}/bin/Editor/Packages/PlayerTemplates/linux-x64"
        "${CMAKE_BINARY_DIR}/acceptance-game")
    set_tests_properties(LinuxRuntimeRegression PROPERTIES TIMEOUT 600
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "TC_RENDERER=opengl;LIBGL_ALWAYS_SOFTWARE=1;ALSOFT_DRIVERS=null")
endif()
