cmake_minimum_required(VERSION 3.7)

project(kaboem)

add_compile_options(-Wall)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_BUILD_TYPE RelWithDebInfo)
#set(CMAKE_BUILD_TYPE Debug)

add_executable(
  kaboem
  agc.cpp
  filter.cpp
  font.cpp
  frequencies.cpp
  gui.cpp
  io.cpp
  midi.cpp
  pipewire.cpp
  pipewire-audio.cpp
  player.cpp
  sample.cpp
  sound.cpp
  time.cpp
)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)
target_link_libraries(kaboem Threads::Threads)

include(FindPkgConfig)

pkg_check_modules(SDL REQUIRED sdl3)
target_link_libraries(kaboem ${SDL_LIBRARIES})
target_include_directories(kaboem PUBLIC ${SDL_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${SDL_CFLAGS_OTHER})

pkg_check_modules(SDL_TTF REQUIRED sdl3-ttf)
target_link_libraries(kaboem ${SDL_TTF_LIBRARIES})
target_include_directories(kaboem PUBLIC ${SDL_TTF_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${SDL_TTF_CFLAGS_OTHER})

pkg_check_modules(SNDFILE REQUIRED sndfile)
target_link_libraries(kaboem ${SNDFILE_LIBRARIES})
target_include_directories(kaboem PUBLIC ${SNDFILE_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${SNDFILE_CFLAGS_OTHER})

pkg_check_modules(MIDI REQUIRED alsa)
target_link_libraries(kaboem ${MIDI_LIBRARIES})
target_include_directories(kaboem PUBLIC ${MIDI_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${MIDI_CFLAGS_OTHER})

find_package(PipeWire)
pkg_check_modules(PIPEWIRE libpipewire-0.3)
target_link_libraries(kaboem ${PIPEWIRE_LIBRARIES})
target_include_directories(kaboem PUBLIC ${PIPEWIRE_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${PIPEWIRE_CFLAGS_OTHER})

pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)
target_link_libraries(kaboem ${NLOHMANN_JSON_LIBRARIES})
target_include_directories(kaboem PUBLIC ${NLOHMANN_JSON_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${NLOHMANN_JSON_CFLAGS_OTHER})

pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
target_link_libraries(kaboem ${FONTCONFIG_LIBRARIES})
target_include_directories(kaboem PUBLIC ${FONTCONFIG_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${FONTCONFIG_CFLAGS_OTHER})

pkg_search_module(FFTW REQUIRED fftw3 IMPORTED_TARGET)
include_directories(PkgConfig::FFTW)
link_libraries(PkgConfig::FFTW)
target_link_libraries(kaboem fftw3)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION True)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY demos DESTINATION share/kaboem)
install(FILES README.md LICENSE DESTINATION share/kaboem)
install(FILES kaboem.desktop DESTINATION share/applications/)
install(FILES images/icon.png DESTINATION share/icons/hicolor/256x256/ RENAME kaboem.png)
