cmake_minimum_required(VERSION 3.16)
project(StreamingEnabled CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
if(NOT EXISTS "${THIRD_PARTY}/imgui/imgui.cpp")
  message(FATAL_ERROR "Missing third_party dependencies. Run scripts/fetch-deps.ps1 first.")
endif()

# --- GLFW (window + input only, no graphics API) ------------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY}/glfw" "${CMAKE_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL)

# --- Dear ImGui ---------------------------------------------------------------
set(IMGUI_DIR "${THIRD_PARTY}/imgui")
add_library(imgui STATIC
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(imgui PUBLIC
  "${IMGUI_DIR}"
  "${IMGUI_DIR}/backends"
  "${THIRD_PARTY}/nlohmann"
)
target_compile_definitions(imgui PUBLIC UNICODE _UNICODE NOMINMAX)

# --- The host ------------------------------------------------------------------
add_executable(StreamingEnabled WIN32
  src/main.cpp
  src/app.cpp
  src/display.cpp
  src/overlay.cpp
  src/sha1.cpp
  src/state.cpp
  src/ws_server.cpp
  src/log.cpp
)

target_include_directories(StreamingEnabled PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${THIRD_PARTY}/dcomp"
)

target_link_libraries(StreamingEnabled PRIVATE
  imgui
  glfw
  d3d11
  d3dcompiler
  dxgi
  dxguid
  dcomp
  dwmapi
  ws2_32
  user32
  gdi32
  shell32
  winmm
)

target_compile_definitions(StreamingEnabled PRIVATE UNICODE _UNICODE NOMINMAX)

if(MINGW)
  target_link_options(StreamingEnabled PRIVATE -static-libgcc -static-libstdc++)
endif()
