cmake_minimum_required(VERSION 3.16)
project(flight_fault_injection LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
add_compile_options(-Wall -Wextra -Wpedantic)

if(APPLE)
  message(STATUS "platform: darwin")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(STATUS "platform: linux")
else()
  message(FATAL_ERROR "unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

find_package(Threads REQUIRED)

add_library(rt_platform STATIC src/rt_platform.cpp src/udp_rx.cpp)
target_include_directories(rt_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(rt_platform SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mavlink")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_definitions(rt_platform PRIVATE _GNU_SOURCE)
endif()

add_executable(loop_bench src/loop_bench.cpp)
target_link_libraries(loop_bench PRIVATE rt_platform Threads::Threads)

add_executable(udp_sender tools/udp_sender.cpp)
target_link_libraries(udp_sender PRIVATE rt_platform)
