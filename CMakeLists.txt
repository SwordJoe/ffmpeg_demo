cmake_minimum_required(VERSION 3.12)
project(test)
set(CMAKE_CXX_STANDARD 17)
# set(CXX_STANDARD_REQUIRED True)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

set(FFMPEG_INCLUDE_DIR "D:/softWare/ffmpeg/ffmpeg-8.0-full_build-shared/include")
set(FFMPEG_LINK_DIR "D:/softWare/ffmpeg/ffmpeg-8.0-full_build-shared/lib")



add_executable(test_decode_h264 ${CMAKE_CURRENT_SOURCE_DIR}/test_decode_video_h264/main.cpp)
target_include_directories(test_decode_h264 PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(test_decode_h264 
    PRIVATE
    "${FFMPEG_LINK_DIR}/avcodec.lib"
    "${FFMPEG_LINK_DIR}/avformat.lib"
    "${FFMPEG_LINK_DIR}/swscale.lib"
    "${FFMPEG_LINK_DIR}/swresample.lib"
    "${FFMPEG_LINK_DIR}/avutil.lib"
)


add_executable(test_demux_mp4 ${CMAKE_CURRENT_SOURCE_DIR}/test_demux_video_mp4/main.cpp)
target_include_directories(test_demux_mp4 PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(test_demux_mp4 
    PRIVATE
    "${FFMPEG_LINK_DIR}/avcodec.lib"
    "${FFMPEG_LINK_DIR}/avformat.lib"
    "${FFMPEG_LINK_DIR}/swscale.lib"
    "${FFMPEG_LINK_DIR}/swresample.lib"
    "${FFMPEG_LINK_DIR}/avutil.lib"
)


add_executable(test_avio ${CMAKE_CURRENT_SOURCE_DIR}/test_avio/main.cpp)
target_include_directories(test_avio PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(test_avio 
    PRIVATE
    "${FFMPEG_LINK_DIR}/avcodec.lib"
    "${FFMPEG_LINK_DIR}/avformat.lib"
    "${FFMPEG_LINK_DIR}/swscale.lib"
    "${FFMPEG_LINK_DIR}/swresample.lib"
    "${FFMPEG_LINK_DIR}/avutil.lib"
)