cmake_minimum_required(VERSION 3.12)
project(test)
set(CMAKE_CXX_STANDARD 17)
# set(CXX_STANDARD_REQUIRED True)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

set(FFMPEG_INCLUDE_DIR "D:/softWare/ffmpeg/ffmpeg-8.0-full_build-shared/include")
set(FFMPEG_LINK_DIR "D:/softWare/ffmpeg/ffmpeg-8.0-full_build-shared/lib")
set(FFMPEG_LIBS
    "${FFMPEG_LINK_DIR}/avcodec.lib"
    "${FFMPEG_LINK_DIR}/avformat.lib"
    "${FFMPEG_LINK_DIR}/swscale.lib"
    "${FFMPEG_LINK_DIR}/swresample.lib"
    "${FFMPEG_LINK_DIR}/avutil.lib"
)

function(add_demo name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE ${FFMPEG_INCLUDE_DIR})
    target_link_libraries(${name} PRIVATE ${FFMPEG_LIBS})
endfunction()

add_demo(test_decode_h264 ${CMAKE_CURRENT_SOURCE_DIR}/test_decode_video_h264/main.cpp)
add_demo(test_demux_mp4 ${CMAKE_CURRENT_SOURCE_DIR}/test_demux_video_mp4/main.cpp)
add_demo(test_avio ${CMAKE_CURRENT_SOURCE_DIR}/test_avio/main.cpp)
add_demo(test_encode_pcm2aac ${CMAKE_CURRENT_SOURCE_DIR}/test_encode_audio_pcm2aac/main.cpp)
add_demo(test_encode_yuv2h264 ${CMAKE_CURRENT_SOURCE_DIR}/test_encode_video_yuv2h264/main.cpp)