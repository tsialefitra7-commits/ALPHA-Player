cmake_minimum_required(VERSION 3.20)

project(ALPHA_Player VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ALPHA Player currently targets Windows 10/11.")
endif()

add_executable(AlphaPlayer WIN32
    src/main.cpp
)

target_compile_features(AlphaPlayer PRIVATE cxx_std_17)
target_compile_definitions(AlphaPlayer PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(AlphaPlayer PRIVATE
    mf
    mfplat
    mfplay
    mfuuid
    ole32
    shell32
    comdlg32
    gdi32
    user32
)

if(MSVC)
    target_compile_options(AlphaPlayer PRIVATE /W4 /permissive- /EHsc)
endif()
