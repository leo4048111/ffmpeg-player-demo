cmake_minimum_required(VERSION 3.17)

project(ffmpeg_player_demo)

set(CMAKE_CXX_STANDARD 11)

# FFmpeg的安装目录，可以通过命令"brew info ffmpeg"获取
set(FFMPEG_DIR /usr/local/Cellar/ffmpeg/6.0)
set(SDL_DIR /usr/local/Cellar/sdl2/2.26.5)

# 头文件搜索路径
include_directories(
    ${FFMPEG_DIR}/include/
    ${SDL_DIR}/include/)
# 动态链接库或静态链接库的搜索路径
link_directories(
    ${FFMPEG_DIR}/lib/
    ${SDL_DIR}/lib/
    )

file(GLOB_RECURSE SRC_DIR "src/*.cc")

# aux_source_directory(./src DIR_SRCS)

add_executable(ffmpeg_player_demo ${SRC_DIR})

#链接库
target_link_libraries(ffmpeg_player_demo
        # #FFmpeg 库
        avcodec
        avfilter
        avformat
        avutil
        swresample
        swscale
        #SDL 库
        SDL2
        SDL2main
        )