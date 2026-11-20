cmake_minimum_required(VERSION 3.20)
project(logihub VERSION 0.1.0 LANGUAGES CXX)
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

enable_testing()
add_executable(test_hidpp tests/test_hidpp.cpp)
target_link_libraries(test_hidpp PRIVATE logicore Qt6::Test)
target_compile_definitions(test_hidpp PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_test(NAME test_hidpp COMMAND test_hidpp)

# Ręczny test na prawdziwym sprzęcie (nie w ctest)
add_executable(hwcheck tests/hwcheck.cpp)
target_link_libraries(hwcheck PRIVATE logicore)
