# Aseprite     | Copyright (C) 2001-2016  David Capello
# LibreSprite  | Copyright (C)      2021  LibreSprite contributors

######################################################################
# Common definitions for all Aseprite libraries/projects

add_definitions(-DHAVE_CONFIG_H)

if(ENABLE_MEMLEAK)
  add_definitions(-DMEMLEAK)
endif()

######################################################################
# Compiler-specific flags

if(MSVC)
  # Do not link with libcmt.lib (to avoid duplicated symbols with msvcrtd.lib)
  if(CMAKE_BUILD_TYPE STREQUAL Debug)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
  endif()

  add_definitions(-D_SCL_SECURE_NO_WARNINGS)
endif(MSVC)

# Directories where .h files can be found
include_directories(. .. ../third_party ../third_party/observable)

if((UNIX AND NOT APPLE) OR (WIN32 AND NOT MSVC))
  set(KEEP_INJECTIONS_BEGIN "-Wl,--whole-archive")
  set(KEEP_INJECTIONS_END "-Wl,--no-whole-archive")
endif()

if(APPLE)
  set(KEEP_INJECTIONS_BEGIN "-all_load")
  set(KEEP_INJECTIONS_END "")
endif()

if(MSVC)
  set(KEEP_INJECTIONS_BEGIN "-WHOLEARCHIVE:$<TARGET_FILE:app-lib> -WHOLEARCHIVE:$<TARGET_FILE:script-lib>")
  set(KEEP_INJECTIONS_END "")
endif()

######################################################################
# Warning flags

if(MSVC)
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-switch")
endif()

######################################################################
# pthread

if (CMAKE_USE_PTHREADS_INIT)
  list(APPEND PLATFORM_LIBS ${CMAKE_THREAD_LIBS_INIT})
endif()

######################################################################
# Aseprite Libraries (in preferred order to be built)

# Disable clip examples
set(CLIP_EXAMPLES OFF CACHE BOOL "Compile clip examples")
set(CLIP_TESTS OFF CACHE BOOL "Compile clip tests")
add_subdirectory(clip)

# Disable undo tests
set(UNDO_TESTS OFF CACHE BOOL "Compile undo tests")
add_subdirectory(undo)

# Our base library
add_subdirectory(base)

# Directory where base/config.h file is located
include_directories(${BASE_INCLUDE_DIR})

if(GEN_EXECUTABLE)
  # Cross-compiling: reuse the host-built tool, adding no build target for it.
  add_custom_target(gen)
else()
  add_subdirectory(gen)
  set(GEN_EXECUTABLE ${CMAKE_BINARY_DIR}/bin/gen)
endif()
add_subdirectory(app)
if (NOT GEN_ONLY)
add_subdirectory(cfg)
add_subdirectory(css)
add_subdirectory(doc)
add_subdirectory(filters)
add_subdirectory(fixmath)
add_subdirectory(flic)
add_subdirectory(gfx)
add_subdirectory(net)
add_subdirectory(render)
add_subdirectory(script)
add_subdirectory(she)
add_subdirectory(ui)
endif()

######################################################################
# Copy data/ directory target

file(GLOB_RECURSE src_data_files
  RELATIVE ${CMAKE_SOURCE_DIR}/data/ "${CMAKE_SOURCE_DIR}/data/*.*")
foreach(fn ${src_data_files})
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/bin/data/${fn}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/data/${fn} ${CMAKE_BINARY_DIR}/bin/data/${fn}
    MAIN_DEPENDENCY ${CMAKE_SOURCE_DIR}/data/${fn})
  list(APPEND out_data_files ${CMAKE_BINARY_DIR}/bin/data/${fn})
endforeach()

add_custom_target(copy_data DEPENDS ${out_data_files})

######################################################################
# Aseprite application
if (NOT GEN_ONLY)
if(WIN32)
  set(win32_resources
    main/resources_win32.rc
    main/settings.manifest)
endif()

add_executable(libresprite WIN32
  main/main.cpp
  ${win32_resources}
  ${x11_resources})

target_link_libraries(libresprite ${KEEP_INJECTIONS_BEGIN}
  app-lib
  base-lib
  cfg-lib
  clip
  css-lib
  doc-lib
  filters-lib
  fixmath-lib
  flic-lib
  gfx-lib
  net-lib
  render-lib
  script-lib
  she
  ui-lib
  undo
  ${KEEP_INJECTIONS_END}
  ${LibArchive_LIBRARY}
  ${TINYXML2_LIBRARY}
  ${JPEG_LIBRARIES}
  ${GIF_LIBRARIES}
  ${PNG_LIBRARIES}
  ${WEBP_LIBRARIES}
  ${ZLIB_LIBRARIES}
  ${V8_LIBRARIES}
  ${FREETYPE_LIBRARIES}
  ${PLATFORM_LIBS})

if(IOS)
  # Keep the bundle version in step with the one the app reports about itself.
  # CFBundleVersion only accepts digits and dots, so "1.2-dev" becomes "1.2".
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/config.h" _libresprite_version_line
       REGEX "^#define[ \t]+VERSION[ \t]+\"")
  string(REGEX REPLACE "^#define[ \t]+VERSION[ \t]+\"([^\"]*)\".*$" "\\1"
         LIBRESPRITE_VERSION "${_libresprite_version_line}")
  string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" LIBRESPRITE_VERSION_NUMERIC
         "${LIBRESPRITE_VERSION}")
  if(NOT LIBRESPRITE_VERSION_NUMERIC)
    set(LIBRESPRITE_VERSION_NUMERIC "0.0")
  endif()

  set(LIBRESPRITE_BUNDLE_EXECUTABLE "libresprite")
  set(LIBRESPRITE_BUNDLE_IDENTIFIER "org.libresprite.LibreSprite")
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/main/Info.plist.in
    ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
    @ONLY)

  # On iOS the executable is an .app bundle and data/ has to live inside it:
  # there is no shared prefix to install into on a sandboxed device.
  set_target_properties(libresprite PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${LIBRESPRITE_BUNDLE_IDENTIFIER}")

  set_source_files_properties(${CMAKE_SOURCE_DIR}/data
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  target_sources(libresprite PRIVATE ${CMAKE_SOURCE_DIR}/data)

  install(TARGETS libresprite
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin)
else()
  install(TARGETS libresprite
    RUNTIME DESTINATION bin)

  install(DIRECTORY ../data
    DESTINATION share/libresprite)
endif()
else()
  add_custom_target(libresprite DEPENDS app-lib)
endif()

add_dependencies(libresprite copy_data)

######################################################################
# Tests

if(ENABLE_TESTS)
  include(FindTests)

  find_tests(base base-lib)
  find_tests(gfx gfx-lib)
  find_tests(doc doc-lib)
  find_tests(render render-lib)
  find_tests(css css-lib)
  find_tests(ui ui-lib)
  find_tests(app/file app-lib)
  find_tests(app app-lib)
  find_tests(. app-lib)
endif()
