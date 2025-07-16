cmake_minimum_required(VERSION 3.9)

project(kaboem)

add_compile_options(-Wall)

set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 0011)
set(KABOEM_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DKABOEM_VERSION=\"\\\"${KABOEM_VERSION}\\\"\"")


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_BUILD_TYPE RelWithDebInfo)
#set(CMAKE_BUILD_TYPE Debug)

add_executable(
  kaboem
  agc.cpp
  bp-filter.cpp
  font.cpp
  frequencies.cpp
  gui.cpp
  io.cpp
  midi.cpp
  mixer.cpp
  player.cpp
  sample.cpp
  sdl3-audio.cpp
  sound.cpp
  time.cpp
  utils.cpp
)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)
target_link_libraries(kaboem PRIVATE Threads::Threads)

include(FindPkgConfig)

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
target_link_libraries(kaboem PRIVATE SDL3::SDL3)

find_package(SDL3_ttf REQUIRED CONFIG)
target_link_libraries(kaboem PRIVATE SDL3_ttf::SDL3_ttf)

pkg_check_modules(SMF smf)
target_link_libraries(kaboem PRIVATE ${SMF_LIBRARIES})
target_include_directories(kaboem PUBLIC ${SMF_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${SMF_CFLAGS_OTHER})


# - Try to find libsndfile
# Once done, this will define
#
#  LIBSNDFILE_FOUND - system has libsndfile
#  LIBSNDFILE_INCLUDE_DIRS - the libsndfile include directories
#  LIBSNDFILE_LIBRARIES - link these to use libsndfile

# Use pkg-config to get hints about paths
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBSNDFILE_PKGCONF sndfile)
endif(PKG_CONFIG_FOUND)

# Include dir
find_path(LIBSNDFILE_INCLUDE_DIR
        NAMES sndfile.h
        PATHS ${LIBSNDFILE_PKGCONF_INCLUDE_DIRS}
        )

# Library
find_library(LIBSNDFILE_LIBRARY
        NAMES sndfile libsndfile-1
        PATHS ${LIBSNDFILE_PKGCONF_LIBRARY_DIRS}
        )

find_package(PackageHandleStandardArgs)
find_package_handle_standard_args(LibSndFile  DEFAULT_MSG  LIBSNDFILE_LIBRARY LIBSNDFILE_INCLUDE_DIR)

if(LIBSNDFILE_FOUND)
    set(LIBSNDFILE_LIBRARIES ${LIBSNDFILE_LIBRARY})
    set(LIBSNDFILE_INCLUDE_DIRS ${LIBSNDFILE_INCLUDE_DIR})
endif(LIBSNDFILE_FOUND)

mark_as_advanced(LIBSNDFILE_LIBRARY LIBSNDFILE_LIBRARIES LIBSNDFILE_INCLUDE_DIR LIBSNDFILE_INCLUDE_DIRS)

include(FindPkgConfig)
pkg_search_module(SndFile REQUIRED sndfile)

include_directories(${LIBSNDFILE_INCLUDE_DIRS})

message(STATUS "sndfile include dirs path: ${LIBSNDFILE_INCLUDE_DIRS}")
message(STATUS "sndfile libs path: ${LIBSNDFILE_LIBRARIES}")

target_link_libraries(kaboem PRIVATE ${LIBSNDFILE_LIBRARIES})
########################

pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)
target_link_libraries(kaboem ${NLOHMANN_JSON_LIBRARIES})
target_include_directories(kaboem PUBLIC ${NLOHMANN_JSON_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${NLOHMANN_JSON_CFLAGS_OTHER})

find_package(Fontconfig)
target_link_libraries(kaboem PRIVATE Fontconfig::Fontconfig)

#find_package(rtmidi)
#target_link_libraries(kaboem PRIVATE RtMidi)
#pkg_check_modules(RTMIDI REQUIRED rtmidi)
pkg_check_modules(RTMIDI REQUIRED RtMidi)
target_link_libraries(kaboem PRIVATE RtMidi::rtmidi)
target_link_libraries(kaboem PRIVATE ${RTMIDI_LIBRARIES})
target_include_directories(kaboem PUBLIC ${RTMIDI_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${RTMIDI_CFLAGS_OTHER})

pkg_search_module(FFTW REQUIRED fftw3 IMPORTED_TARGET)
include_directories(PkgConfig::FFTW)
link_libraries(PkgConfig::FFTW)
target_link_libraries(kaboem PRIVATE fftw3)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION True)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY demos DESTINATION share/kaboem)
install(FILES README.md LICENSE DESTINATION share/kaboem)
install(FILES kaboem.desktop DESTINATION share/applications/)
install(FILES images/icon.png DESTINATION share/icons/hicolor/256x256/ RENAME kaboem.png)

configure_file(config.h.in config.h)
target_include_directories(kaboem PUBLIC "${PROJECT_BINARY_DIR}")

if (RTMIDI_FOUND)
	message("librtmidi found, MIDI in/out support enabled")
endif()

if (SMF_FOUND)
	message("libsmf found, can write MIDI files")
endif()
