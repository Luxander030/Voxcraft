cmake_minimum_required(VERSION 3.20)
project(Voxcraft VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
  enable_language(OBJC OBJCXX)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
  endif()
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VX_BUILD_GAME "Build the graphical game (requires a GPU backend)" ON)
option(VX_PROFILE "Enable profiling hooks in release builds" OFF)

# World generation must be bit-identical on x64 and arm64, Windows and macOS:
# no fused multiply-add contraction, no fast-math (ARCHITECTURE.md "Determinism").
if(MSVC)
  add_compile_options(/fp:precise /W3 /utf-8 /permissive- /Zc:__cplusplus)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-ffp-contract=off -fno-fast-math -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()
if(VX_PROFILE)
  add_compile_definitions(VX_PROFILE=1)
endif()

# ---- third party (vendored, permissive licences; see THIRD_PARTY.md) ----
add_library(vx_lz4 STATIC third_party/lz4/lz4.c)
target_include_directories(vx_lz4 PUBLIC third_party)
if(NOT MSVC)
  target_compile_options(vx_lz4 PRIVATE -w)
endif()

# ---- engine core: no graphics, fully testable headless ----
file(GLOB VX_CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp src/world/*.cpp src/storage/*.cpp src/game/*.cpp)
add_library(voxcore STATIC ${VX_CORE_SRC})
target_include_directories(voxcore PUBLIC src third_party)
find_package(Threads REQUIRED)
target_link_libraries(voxcore PUBLIC vx_lz4 Threads::Threads)
if(WIN32)
  target_link_libraries(voxcore PUBLIC shell32 ole32 psapi)
endif()

# ---- CPU-side renderer logic (meshing etc.) - testable without a GPU ----
file(GLOB VX_RENDER_CORE_SRC CONFIGURE_DEPENDS src/render/*.cpp)
if(VX_RENDER_CORE_SRC)
  add_library(voxrender STATIC ${VX_RENDER_CORE_SRC})
  target_link_libraries(voxrender PUBLIC voxcore)
else()
  add_library(voxrender INTERFACE)
  target_link_libraries(voxrender INTERFACE voxcore)
endif()

# ---- runtime assets (validated + packed by tools/build_assets.py; incremental) ----
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB VX_TEXTURES CONFIGURE_DEPENDS assets/textures/blocks/*.png)
file(GLOB VX_ITEM_TEXTURES CONFIGURE_DEPENDS assets/textures/items/*.png)
add_custom_command(
  OUTPUT ${CMAKE_SOURCE_DIR}/assets/generated/voxcraft.pak
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_assets.py
  DEPENDS ${VX_TEXTURES} ${VX_ITEM_TEXTURES} ${CMAKE_SOURCE_DIR}/assets/data/blocks.json ${CMAKE_SOURCE_DIR}/assets/data/items.json ${CMAKE_SOURCE_DIR}/tools/build_assets.py
  COMMENT "Packing assets")
add_custom_target(assets ALL DEPENDS ${CMAKE_SOURCE_DIR}/assets/generated/voxcraft.pak)

# ---- tests ----
enable_testing()
file(GLOB VX_TEST_SRC CONFIGURE_DEPENDS tests/*.cpp)
list(FILTER VX_TEST_SRC EXCLUDE REGEX "bench\\.cpp$")
add_executable(voxtests ${VX_TEST_SRC})
target_link_libraries(voxtests PRIVATE voxrender)
target_compile_definitions(voxtests PRIVATE VX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_dependencies(voxtests assets)
add_test(NAME voxtests COMMAND voxtests)

add_executable(voxbench tests/bench.cpp)
target_link_libraries(voxbench PRIVATE voxrender)
target_compile_definitions(voxbench PRIVATE VX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ---- world maintenance CLI ----
add_executable(vxworld src/tools/vxworld.cpp)
target_link_libraries(vxworld PRIVATE voxcore)
target_compile_definitions(vxworld PRIVATE VX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ---- the game ----
if(VX_BUILD_GAME AND EXISTS ${CMAKE_SOURCE_DIR}/src/app/main.cpp)
  file(GLOB VX_APP_SRC CONFIGURE_DEPENDS src/app/*.cpp)
  if(APPLE)
    set(VX_PLATFORM_SRC src/platform/sokol_impl.m)
  else()
    set(VX_PLATFORM_SRC src/platform/sokol_impl.c)
  endif()
  add_executable(voxcraft WIN32 MACOSX_BUNDLE ${VX_APP_SRC} ${VX_PLATFORM_SRC})
  target_link_libraries(voxcraft PRIVATE voxrender)
  target_compile_definitions(voxcraft PRIVATE VX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_dependencies(voxcraft assets)
  if(APPLE)
    set_source_files_properties(src/platform/sokol_impl.m PROPERTIES COMPILE_FLAGS "-fobjc-arc -w")
  elseif(NOT MSVC)
    set_source_files_properties(src/platform/sokol_impl.c PROPERTIES COMPILE_FLAGS "-w")
  endif()
  if(APPLE)
    target_compile_definitions(voxcraft PRIVATE SOKOL_METAL)
    target_link_libraries(voxcraft PRIVATE "-framework Cocoa" "-framework QuartzCore" "-framework Metal" "-framework MetalKit" "-framework AudioToolbox")
    set_target_properties(voxcraft PROPERTIES
      OUTPUT_NAME "Voxcraft"
      MACOSX_BUNDLE_BUNDLE_NAME "Voxcraft"
      MACOSX_BUNDLE_GUI_IDENTIFIER "dev.voxcraft.game"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in)
    set(VX_DATA_DEST "$<TARGET_BUNDLE_CONTENT_DIR:voxcraft>/Resources")
  elseif(WIN32)
    target_compile_definitions(voxcraft PRIVATE SOKOL_D3D11)
    target_link_libraries(voxcraft PRIVATE d3d11 dxgi ole32)
    set(VX_DATA_DEST "$<TARGET_FILE_DIR:voxcraft>")
  else()
    target_compile_definitions(voxcraft PRIVATE SOKOL_GLCORE)
    target_link_libraries(voxcraft PRIVATE X11 Xi Xcursor GL dl m asound)
    set(VX_DATA_DEST "$<TARGET_FILE_DIR:voxcraft>")
  endif()
  # Packed runtime assets (built by tools/build_assets.py) are copied next to the executable / into the .app
  add_custom_command(TARGET voxcraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VX_DATA_DEST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/assets/generated/voxcraft.pak ${VX_DATA_DEST}/voxcraft.pak
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/assets/data/blocks.json ${CMAKE_SOURCE_DIR}/assets/data/items.json ${CMAKE_SOURCE_DIR}/assets/data/recipes.json ${VX_DATA_DEST})
endif()
