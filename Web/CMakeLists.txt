cmake_minimum_required(VERSION 3.20)
project(TomCatWebPlayer LANGUAGES C CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure with emcmake; this target requires Emscripten")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(TC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(TC_SRC "${TC_ROOT}/TomCat/src")
set(TC_VENDOR "${TC_ROOT}/TomCat/vendor")

add_compile_options(-pthread -fexceptions)
add_compile_definitions(TC_PLATFORM_WEB=1 GLFW_INCLUDE_NONE YAML_CPP_STATIC_DEFINE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${TC_VENDOR}/Box2D" box2d EXCLUDE_FROM_ALL)
target_compile_definitions(box2d PUBLIC B2_USER_SETTINGS)
target_include_directories(box2d PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
file(GLOB TC_YAML "${TC_VENDOR}/yaml-cpp/src/*.cpp")
add_library(tc_yaml STATIC ${TC_YAML})
target_include_directories(tc_yaml PUBLIC "${TC_VENDOR}/yaml-cpp/include")

set(TC_SOURCES)
foreach(component Core Asset Audio Math Project Renderer Runtime Scene Scripting Utils)
  file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${TC_SRC}/TomCat/${component}/*.cpp")
  list(APPEND TC_SOURCES ${sources})
endforeach()
file(GLOB gl_sources "${TC_SRC}/platform/OpenGL/*.cpp")
list(REMOVE_ITEM gl_sources
  "${TC_SRC}/platform/OpenGL/OpenGLShader.cpp"
  "${TC_SRC}/platform/OpenGL/OpenGLFramebuffer.cpp"
  "${TC_SRC}/platform/OpenGL/OpenGLContext.cpp")
list(REMOVE_ITEM TC_SOURCES "${TC_SRC}/TomCat/Renderer/GraphicsContext.cpp"
  "${TC_SRC}/TomCat/Project/ProjectManager.cpp")
add_library(tc_player_core STATIC ${TC_SOURCES} ${gl_sources}
  "${TC_VENDOR}/stb_image/stb_image.cpp"
  "${TC_SRC}/platform/Window/WindowsInput.cpp"
  "${TC_SRC}/platform/Web/WebWindow.cpp"
  "${TC_SRC}/platform/Web/WebShader.cpp"
  "${TC_SRC}/platform/Web/WebFramebuffer.cpp"
  "${TC_ROOT}/Player/TomCatPlayer/src/PlayerRuntimeLayer.cpp"
  "${TC_ROOT}/Player/TomCatPlayer/src/PlayerCommandLine.cpp")
target_include_directories(tc_player_core PUBLIC
  "${TC_SRC}" "${TC_ROOT}/Player/TomCatPlayer/src"
  "${TC_VENDOR}/spdlog/include" "${TC_VENDOR}/glm" "${TC_VENDOR}/entt/include"
  "${TC_VENDOR}/stb_image" "${TC_VENDOR}/ImGui" "${TC_VENDOR}/ImGuizmo")
target_link_libraries(tc_player_core PUBLIC box2d tc_yaml)

add_executable(tomcat_player "${TC_SRC}/platform/Web/WebPlayer.cpp")
target_link_libraries(tomcat_player PRIVATE tc_player_core)
set(TC_WEB_LINK_OPTIONS
  --no-entry -pthread -fexceptions
  "-sUSE_GLFW=3" "-sMIN_WEBGL_VERSION=2" "-sMAX_WEBGL_VERSION=2"
  "-sMODULARIZE=1"
  "-sALLOW_MEMORY_GROWTH=1" "-sINITIAL_MEMORY=134217728"
  "-sSTACK_SIZE=4194304" "-sDEFAULT_PTHREAD_STACK_SIZE=2097152"
  "-sASSERTIONS=1" "-sSTACK_OVERFLOW_CHECK=2"
  "-sPTHREAD_POOL_SIZE=12" "-sPTHREAD_POOL_SIZE_STRICT=2"
  "-sFORCE_FILESYSTEM=1"
  "-sEXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString','FS','HEAPU8','PThread']"
  "--preload-file=${TC_ROOT}/Samples/PhysicsPlayground@/Samples/PhysicsPlayground")
target_link_options(tomcat_player PRIVATE ${TC_WEB_LINK_OPTIONS}
  "-sEXPORT_NAME=TomCatPlayerModule"
  "-sEXPORTED_FUNCTIONS=['_malloc','_free','_tc_web_player_boot','_tc_web_player_frame','_tc_web_player_resize','_tc_web_player_shutdown','_tc_web_player_error','_tc_web_player_cook_sample','_tc_web_player_stats']")

add_executable(tomcat_editor
  "${TC_SRC}/platform/Web/WebEditor.cpp"
  "${TC_SRC}/platform/Web/WebEditorUI.cpp"
  "${TC_SRC}/TomCat/ImGui/ImGuiLayer.cpp"
  "${TC_SRC}/TomCat/ImGui/ImGuiCallback.cpp"
  "${TC_ROOT}/Editor/TomCatInut/src/panels/SceneHierarchyPanel.cpp"
  "${TC_ROOT}/Editor/TomCatInut/src/panels/ContentBrowserPanel.cpp"
  "${TC_ROOT}/Editor/TomCatInut/src/panels/ConsolePanel.cpp"
  "${TC_ROOT}/Editor/TomCatInut/src/Scripting/ScriptMetadataCache.cpp"
  "${TC_VENDOR}/ImGui/imgui.cpp" "${TC_VENDOR}/ImGui/imgui_draw.cpp"
  "${TC_VENDOR}/ImGui/imgui_tables.cpp" "${TC_VENDOR}/ImGui/imgui_widgets.cpp"
  "${TC_VENDOR}/ImGui/backends/imgui_impl_glfw.cpp"
  "${TC_VENDOR}/ImGui/backends/imgui_impl_opengl3.cpp"
  "${TC_VENDOR}/ImGuizmo/ImGuizmo.cpp"
  "${TC_SRC}/TomCat/Editor/WebEditorSession.cpp"
  "${TC_SRC}/TomCat/Editor/SceneHistory.cpp")
target_link_libraries(tomcat_editor PRIVATE tc_player_core)
target_include_directories(tomcat_editor PRIVATE "${TC_VENDOR}" "${TC_ROOT}/Editor/TomCatInut/src")
target_compile_definitions(tomcat_editor PRIVATE IMGUI_IMPL_OPENGL_ES3)
target_link_options(tomcat_editor PRIVATE ${TC_WEB_LINK_OPTIONS}
  "-sEXPORT_NAME=TomCatEditorModule"
  "--preload-file=${TC_ROOT}/Editor/TomCatInut/Packages@/Packages"
  "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/fonts@/WebFonts"
  "-sEXPORTED_FUNCTIONS=['_malloc','_free','_tc_web_editor_boot','_tc_web_editor_frame','_tc_web_editor_resize','_tc_web_editor_shutdown','_tc_web_editor_error','_tc_web_editor_rpc','_tc_web_editor_state','_tc_web_editor_take_actions']")
