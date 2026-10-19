cmake_minimum_required(VERSION 3.20)
project(TowTycoon VERSION 0.2.0 LANGUAGES CXX)

if(WIN32)
  enable_language(RC)
  set(APP_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
else()
  set(APP_RESOURCES "")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CTest)

add_executable(TowTycoon
  src/main.cpp
  src/Engine.cpp
  src/SaveStore.cpp
  ${APP_RESOURCES}
)
target_compile_definitions(TowTycoon PRIVATE TOWTYCOON_VERSION="${PROJECT_VERSION}")

if(WIN32)
  set_source_files_properties("${APP_RESOURCES}" PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/TowTycoon.ico"
  )
  target_include_directories(TowTycoon PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

find_package(SDL2 CONFIG QUIET)
find_package(SDL2_image CONFIG QUIET)
find_package(SDL2_ttf CONFIG QUIET)

if(TARGET SDL2::SDL2 AND TARGET SDL2_image::SDL2_image)
  target_link_libraries(TowTycoon PRIVATE SDL2::SDL2 SDL2_image::SDL2_image)
  if(TARGET SDL2_ttf::SDL2_ttf)
    target_link_libraries(TowTycoon PRIVATE SDL2_ttf::SDL2_ttf)
    target_compile_definitions(TowTycoon PRIVATE TOWTYCOON_USE_TTF=1)
  endif()
else()
  # Use pkg-config on MSYS2 (most reliable)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
  pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
  pkg_check_modules(SDL2_TTF QUIET SDL2_ttf)

  target_include_directories(TowTycoon PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
  )
  if(SDL2_TTF_FOUND)
    target_include_directories(TowTycoon PRIVATE
      ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_compile_definitions(TowTycoon PRIVATE TOWTYCOON_USE_TTF=1)
  endif()

  target_compile_options(TowTycoon PRIVATE
    ${SDL2_CFLAGS_OTHER}
    ${SDL2_IMAGE_CFLAGS_OTHER}
  )
  if(SDL2_TTF_FOUND)
    target_compile_options(TowTycoon PRIVATE
      ${SDL2_TTF_CFLAGS_OTHER}
    )
  endif()

  target_link_directories(TowTycoon PRIVATE
    ${SDL2_LIBRARY_DIRS}
    ${SDL2_IMAGE_LIBRARY_DIRS}
  )
  if(SDL2_TTF_FOUND)
    target_link_directories(TowTycoon PRIVATE
      ${SDL2_TTF_LIBRARY_DIRS}
    )
  endif()

  target_link_libraries(TowTycoon PRIVATE
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
  )
  if(SDL2_TTF_FOUND)
    target_link_libraries(TowTycoon PRIVATE
      ${SDL2_TTF_LIBRARIES}
    )
  endif()
endif()

if(BUILD_TESTING)
  add_executable(TowTycoonRngTests tests/deterministic_rng_test.cpp)
  target_include_directories(TowTycoonRngTests PRIVATE src)
  add_test(NAME deterministic_rng COMMAND TowTycoonRngTests)

  add_executable(TowTycoonUiViewportTests tests/ui_viewport_test.cpp)
  target_include_directories(TowTycoonUiViewportTests PRIVATE src)
  add_test(NAME ui_viewport COMMAND TowTycoonUiViewportTests)

  add_executable(TowTycoonSimulationTests tests/simulation_core_test.cpp)
  target_include_directories(TowTycoonSimulationTests PRIVATE src)
  add_test(NAME simulation_core COMMAND TowTycoonSimulationTests)

  add_executable(TowTycoonSaveGameCodecTests tests/save_game_codec_test.cpp)
  target_include_directories(TowTycoonSaveGameCodecTests PRIVATE src)
  add_test(NAME save_game_codec COMMAND TowTycoonSaveGameCodecTests)

  add_executable(TowTycoonSaveStoreTests tests/save_store_test.cpp src/SaveStore.cpp)
  target_include_directories(TowTycoonSaveStoreTests PRIVATE src)
  add_test(NAME save_store COMMAND TowTycoonSaveStoreTests)
endif()

# The PE dependency copier is only needed for MinGW builds. Derive its runtime
# directory from the active toolchain instead of assuming an MSYS2 install path.
if(MINGW)
  get_filename_component(_runtime_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
endif()

set(_copy_runtime_deps_script "${CMAKE_CURRENT_BINARY_DIR}/cmake/CopyRuntimeDeps.cmake")
file(WRITE "${_copy_runtime_deps_script}" [=[
if(NOT DEFINED exe OR NOT DEFINED dest OR NOT DEFINED objdump OR NOT DEFINED mingw_bin)
  message(WARNING "CopyRuntimeDeps.cmake requires -Dexe -Ddest -Dobjdump -Dmingw_bin.")
  return()
endif()
if(NOT EXISTS "${exe}")
  message(WARNING "Executable not found: ${exe}")
  return()
endif()

# Windows system DLLs that are always present and should be ignored.
set(_system_dlls
  kernel32.dll
  msvcrt.dll
  ntdll.dll
  user32.dll
  gdi32.dll
  shell32.dll
  advapi32.dll
  ole32.dll
  oleaut32.dll
  ws2_32.dll
  comdlg32.dll
  comctl32.dll
  imm32.dll
  setupapi.dll
  version.dll
  winmm.dll
  bcrypt.dll
  dwrite.dll
  rpcrt4.dll
  usp10.dll
  api-ms-win-core-synch-l1-2-0.dll
)

set(_queue "${exe}")
set(_visited_files "")
set(_seen_dlls "")
set(_copied "")
set(_missing "")

macro(_handle_dll dll)
  string(TOLOWER "${dll}" _dll_lower)
  list(FIND _system_dlls "${_dll_lower}" _sys_idx)
  if(_sys_idx EQUAL -1)
    list(FIND _seen_dlls "${_dll_lower}" _seen_idx)
    if(_seen_idx EQUAL -1)
      list(APPEND _seen_dlls "${_dll_lower}")
      set(_src "${mingw_bin}/${dll}")
      if(EXISTS "${_src}")
        file(COPY "${_src}" DESTINATION "${dest}")
        list(APPEND _copied "${dll}")
        list(APPEND _queue "${_src}")
      else()
        list(APPEND _missing "${dll}")
      endif()
    endif()
  endif()
endmacro()

# Seed core runtime DLLs that must ship.
set(_seed_dlls
  SDL2.dll
  SDL2_image.dll
  libgcc_s_seh-1.dll
  libstdc++-6.dll
  libwinpthread-1.dll
)
foreach(dll IN LISTS _seed_dlls)
  _handle_dll("${dll}")
endforeach()

while(_queue)
  list(GET _queue 0 _current)
  list(REMOVE_AT _queue 0)
  list(FIND _visited_files "${_current}" _visit_idx)
  if(_visit_idx GREATER -1)
    continue()
  endif()
  list(APPEND _visited_files "${_current}")

  execute_process(
    COMMAND "${objdump}" -p "${_current}"
    RESULT_VARIABLE _objdump_result
    OUTPUT_VARIABLE _objdump_out
    ERROR_VARIABLE _objdump_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT _objdump_result EQUAL 0)
    message(WARNING "objdump failed (${_objdump_result}) for ${_current}: ${_objdump_err}")
    continue()
  endif()

  string(REGEX MATCHALL "DLL Name: ([^\r\n]+)" _dll_lines "${_objdump_out}")
  foreach(line IN LISTS _dll_lines)
    string(REGEX REPLACE "DLL Name: " "" dll "${line}")
    _handle_dll("${dll}")
  endforeach()
endwhile()

list(REMOVE_DUPLICATES _copied)
list(REMOVE_DUPLICATES _missing)

# Validate required runtime DLLs exist in output folder.
set(_required_dlls
  SDL2.dll
  SDL2_image.dll
  libpng16-16.dll
  libjpeg-8.dll
  libjxl.dll
  libavif-16.dll
)
set(_missing_required "")
foreach(req IN LISTS _required_dlls)
  if(NOT EXISTS "${dest}/${req}")
    list(APPEND _missing_required "${req}")
  endif()
endforeach()

if(_missing_required)
  message(WARNING
    "Missing required DLLs in output folder: ${_missing_required}. "
    "Searched output: ${dest}; source bin: ${mingw_bin}. "
    "Copied: ${_copied}."
  )
endif()

message(STATUS "DLLs copied to ${dest}: ${_copied}")
if(_missing)
  message(WARNING "Missing non-system DLLs in ${mingw_bin}: ${_missing}")
endif()
]=])

if(MINGW)
  add_custom_command(TARGET TowTycoon POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -Dexe="$<TARGET_FILE:TowTycoon>"
      -Ddest="$<TARGET_FILE_DIR:TowTycoon>"
      -Dobjdump="${CMAKE_OBJDUMP}"
      -Dmingw_bin="${_runtime_bin_dir}"
      -P "${_copy_runtime_deps_script}"
  )
endif()

# Copy assets beside exe
add_custom_command(TARGET TowTycoon POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
  $<TARGET_FILE_DIR:TowTycoon>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  ${CMAKE_SOURCE_DIR}/Assets
  $<TARGET_FILE_DIR:TowTycoon>/assets
)

# Install a distributable folder: executable + assets.
install(TARGETS TowTycoon
  RUNTIME DESTINATION .
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/Assets/
  DESTINATION assets
)

set(_install_runtime_deps_script "${CMAKE_CURRENT_BINARY_DIR}/cmake/InstallRuntimeDeps.cmake")
file(WRITE "${_install_runtime_deps_script}" [=[
if(NOT DEFINED exe OR NOT DEFINED dest OR NOT DEFINED objdump OR NOT DEFINED mingw_bin)
  message(FATAL_ERROR "InstallRuntimeDeps.cmake requires -Dexe -Ddest -Dobjdump -Dmingw_bin.")
endif()
if(NOT EXISTS "${exe}")
  message(FATAL_ERROR "Executable not found: ${exe}")
endif()

execute_process(
  COMMAND "${objdump}" -p "${exe}"
  RESULT_VARIABLE _objdump_result
  OUTPUT_VARIABLE _objdump_out
  ERROR_VARIABLE _objdump_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _objdump_result EQUAL 0)
  message(FATAL_ERROR "objdump failed (${_objdump_result}): ${_objdump_err}")
endif()

string(REGEX MATCHALL "DLL Name: ([^\r\n]+)" _dll_lines "${_objdump_out}")
set(_dlls "")
foreach(line IN LISTS _dll_lines)
  string(REGEX REPLACE "DLL Name: " "" dll "${line}")
  list(APPEND _dlls "${dll}")
endforeach()

# Windows system DLLs that are always present and should be ignored.
set(_system_dlls
  kernel32.dll
  msvcrt.dll
  ntdll.dll
  user32.dll
  gdi32.dll
  shell32.dll
  advapi32.dll
  ole32.dll
  oleaut32.dll
  ws2_32.dll
  comdlg32.dll
  comctl32.dll
  imm32.dll
  setupapi.dll
  version.dll
  winmm.dll
  bcrypt.dll
  dwrite.dll
  rpcrt4.dll
  usp10.dll
  api-ms-win-core-synch-l1-2-0.dll
)

list(APPEND _dlls
  SDL2.dll
  SDL2_image.dll
  libgcc_s_seh-1.dll
  libstdc++-6.dll
  libwinpthread-1.dll
)
list(REMOVE_DUPLICATES _dlls)
set(_dlls_filtered "")
foreach(dll IN LISTS _dlls)
  string(TOLOWER "${dll}" _dll_lower)
  list(FIND _system_dlls "${_dll_lower}" _sys_index)
  if(_sys_index EQUAL -1)
    list(APPEND _dlls_filtered "${dll}")
  endif()
endforeach()
set(_dlls "${_dlls_filtered}")

message(STATUS "Install DLLs referenced by ${exe}: ${_dlls}")

set(_copied "")
set(_missing "")
foreach(dll IN LISTS _dlls)
  set(_src "${mingw_bin}/${dll}")
  if(EXISTS "${_src}")
    file(INSTALL DESTINATION "${dest}" TYPE SHARED_LIBRARY FILES "${_src}")
    list(APPEND _copied "${dll}")
  else()
    list(APPEND _missing "${dll}")
  endif()
endforeach()

message(STATUS "Install DLLs copied to ${dest}: ${_copied}")
if(_missing)
  message(WARNING "Missing non-system DLLs in ${mingw_bin}: ${_missing}")
endif()
]=])

if(MINGW)
  install(CODE
    "execute_process(COMMAND \"${CMAKE_COMMAND}\" -Dexe=\"$<TARGET_FILE:TowTycoon>\" -Ddest=\"${CMAKE_INSTALL_PREFIX}\" -Dobjdump=\"${CMAKE_OBJDUMP}\" -Dmingw_bin=\"${_runtime_bin_dir}\" -P \"${_install_runtime_deps_script}\")"
  )
endif()
