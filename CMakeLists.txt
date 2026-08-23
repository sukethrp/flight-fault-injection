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

add_executable(loop_bench
  src/loop_bench.cpp
  src/control.cpp
  src/trajectory.cpp
  src/setpoint_tx.cpp
  src/ekf.cpp
  src/detectors.cpp
  src/failsafe.cpp)
target_link_libraries(loop_bench PRIVATE rt_platform Threads::Threads)

add_executable(udp_sender tools/udp_sender.cpp)
target_link_libraries(udp_sender PRIVATE rt_platform)

add_executable(plant tools/plant.cpp)
target_link_libraries(plant PRIVATE rt_platform)
target_include_directories(plant PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")

add_executable(proxy injector/proxy.cpp injector/fault.cpp)
target_link_libraries(proxy PRIVATE rt_platform)
target_include_directories(proxy PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/injector"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(test_msg_slots tests/test_msg_slots.cpp)
target_link_libraries(test_msg_slots PRIVATE rt_platform)

add_executable(test_udp_sender tests/test_udp_sender.cpp)
target_link_libraries(test_udp_sender PRIVATE rt_platform)
target_compile_definitions(test_udp_sender PRIVATE
  "UDP_SENDER_PATH=\"$<TARGET_FILE:udp_sender>\"")
add_dependencies(test_udp_sender udp_sender)

add_executable(test_plant tests/test_plant.cpp)
target_include_directories(test_plant PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")

add_executable(test_control
  tests/test_control.cpp
  src/control.cpp
  src/trajectory.cpp)
target_include_directories(test_control PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(test_ekf
  tests/test_ekf.cpp
  src/ekf.cpp)
target_include_directories(test_ekf PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(test_detectors
  tests/test_detectors.cpp
  src/detectors.cpp
  src/failsafe.cpp)
target_link_libraries(test_detectors PRIVATE rt_platform)
target_include_directories(test_detectors SYSTEM PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mavlink")

enable_testing()
add_test(NAME test_msg_slots COMMAND test_msg_slots)
add_test(NAME test_udp_sender COMMAND test_udp_sender)
add_test(NAME test_plant COMMAND test_plant)
add_test(NAME test_control COMMAND test_control)
add_test(NAME test_ekf COMMAND test_ekf)
set_tests_properties(test_ekf PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME test_detectors COMMAND test_detectors)
add_test(NAME test_percentiles COMMAND python3
  "${CMAKE_SOURCE_DIR}/analysis/test_percentiles.py")
add_test(NAME test_estimator_error COMMAND python3
  "${CMAKE_SOURCE_DIR}/analysis/test_estimator_error.py")
add_test(NAME test_fault_table COMMAND python3
  "${CMAKE_SOURCE_DIR}/analysis/test_fault_table.py")
