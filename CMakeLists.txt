cmake_minimum_required(VERSION 3.17)

project(ffmpeg_player_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

set(FFMPEG_DIR /usr/local/Cellar/ffmpeg/6.0 CACHE PATH "ffmpeg dir")
set(SDL_DIR /usr/local/Cellar/sdl2/2.26.5 CACHE PATH "sdl dir")


include_directories(
    third_party/
    ${FFMPEG_DIR}/include/
    ${SDL_DIR}/include/)

link_directories(
    ${FFMPEG_DIR}/lib/
    ${SDL_DIR}/lib/
    )

file(GLOB_RECURSE SRC_DIR "src/*.cc")

# aux_source_directory(./src DIR_SRCS)

add_executable(ffmpeg_player_demo ${SRC_DIR})

target_link_libraries(ffmpeg_player_demo
        # FFmpeg
        avcodec
        avfilter
        avformat
        avutil
        swresample
        swscale
        # SDL 
        SDL2
        SDL2main
        SDL2_ttf
        )