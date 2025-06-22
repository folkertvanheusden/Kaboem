cmake_minimum_required(VERSION 3.7)

project(kaboem)

add_compile_options(-Wall)

set(CMAKE_BUILD_TYPE RelWithDebInfo)

add_executable(
  kaboem
  gui.cpp
  io.cpp
  pipewire.cpp
  pipewire-audio.cpp
  sample.cpp
  sound.cpp
)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)
target_link_libraries(kaboem Threads::Threads)

include(FindPkgConfig)

pkg_check_modules(SDL REQUIRED sdl2)
target_link_libraries(kaboem ${SDL_LIBRARIES})
target_include_directories(kaboem PUBLIC ${SDL_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${SDL_CFLAGS_OTHER})

pkg_check_modules(SDL_GFX REQUIRED SDL2_gfx)
target_link_libraries(kaboem ${SDL_GFX_LIBRARIES})
target_include_directories(kaboem PUBLIC ${SDL_GFX_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${SDL_GFX_CFLAGS_OTHER})

pkg_check_modules(SDL_TTF REQUIRED SDL2_ttf)
target_link_libraries(kaboem ${SDL_TTF_LIBRARIES})
target_include_directories(kaboem PUBLIC ${SDL_TTF_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${SDL_TTF_CFLAGS_OTHER})

pkg_check_modules(SNDFILE REQUIRED sndfile)
target_link_libraries(kaboem ${SNDFILE_LIBRARIES})
target_include_directories(kaboem PUBLIC ${SNDFILE_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${SNDFILE_CFLAGS_OTHER})

find_package(PipeWire)
pkg_check_modules(PIPEWIRE libpipewire-0.3)
target_link_libraries(kaboem ${PIPEWIRE_LIBRARIES})
target_include_directories(kaboem PUBLIC ${PIPEWIRE_INCLUDE_DIRS})
target_compile_options(kaboem PUBLIC ${PIPEWIRE_CFLAGS_OTHER})

pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)
target_link_libraries(kaboem ${NLOHMANN_JSON})
target_include_directories(kaboem PUBLIC ${NLOHMANN_JSON})
target_compile_options(kaboem PUBLIC ${NLOHMANN_JSON})

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION True)
