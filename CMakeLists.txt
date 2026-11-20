cmake_minimum_required(VERSION 3.20)
project(logihub VERSION 0.1.3 LANGUAGES C CXX)
add_compile_definitions(LOGIHUB_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network Test)

add_library(logicore STATIC src/hidpp.cpp src/onboard.cpp src/devices.cpp src/audio.cpp src/dsp.cpp
    src/openrgb.cpp)
target_include_directories(logicore PUBLIC src)
target_link_libraries(logicore PUBLIC Qt6::Core Qt6::Network)

add_executable(logihub src/main.cpp src/mainwindow.cpp src/devicepage.cpp src/theme.cpp src/audiowidgets.cpp
    src/mousediagram.cpp src/rgbpanel.cpp src/updater.cpp)
target_link_libraries(logihub PRIVATE logicore Qt6::Widgets)

# ---- Wtyczki LADSPA dla Voice FX (build/ladspa/) ----
set(LADSPA_OUT ${CMAKE_BINARY_DIR}/ladspa)
find_path(LADSPA_INCLUDE ladspa.h)
if(LADSPA_INCLUDE)
    add_library(logihub_dsp MODULE ladspa/logihub_dsp.c)
    target_include_directories(logihub_dsp PRIVATE ${LADSPA_INCLUDE})
    target_link_libraries(logihub_dsp PRIVATE m)
    set_target_properties(logihub_dsp PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${LADSPA_OUT}
                          C_VISIBILITY_PRESET default)

    # RNNoise - opcjonalnie; biblioteka trafia obok wtyczki (rpath $ORIGIN)
    find_path(RNNOISE_INCLUDE rnnoise.h)
    find_library(RNNOISE_LIB rnnoise)
    if(RNNOISE_INCLUDE AND RNNOISE_LIB)
        add_library(logihub_rnnoise MODULE ladspa/logihub_rnnoise.c)
        target_include_directories(logihub_rnnoise PRIVATE ${LADSPA_INCLUDE} ${RNNOISE_INCLUDE})
        target_link_libraries(logihub_rnnoise PRIVATE ${RNNOISE_LIB})
        set_target_properties(logihub_rnnoise PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${LADSPA_OUT}
                              BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
        get_filename_component(RNNOISE_REAL ${RNNOISE_LIB} REALPATH)
        configure_file(${RNNOISE_REAL} ${LADSPA_OUT}/librnnoise.so.0 COPYONLY)
    else()
        message(STATUS "RNNoise nie znaleziony - redukcja szumów tylko przez WebRTC")
    endif()
else()
    message(WARNING "Brak ladspa.h - wtyczki Voice FX nie zostaną zbudowane (bramka/kompresor/de-esser/limiter)")
endif()

enable_testing()
add_executable(test_hidpp tests/test_hidpp.cpp)
target_link_libraries(test_hidpp PRIVATE logicore Qt6::Test ${CMAKE_DL_LIBS})
target_compile_definitions(test_hidpp PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests"
                           LADSPA_DIR="${LADSPA_OUT}")
add_test(NAME test_hidpp COMMAND test_hidpp)

# Ręczny test na prawdziwym sprzęcie (nie w ctest)
add_executable(hwcheck tests/hwcheck.cpp)
target_link_libraries(hwcheck PRIVATE logicore)
