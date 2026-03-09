cmake_minimum_required(VERSION 3.20)

project(energytop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZMQ REQUIRED IMPORTED_TARGET libzmq)
find_path(CPPZMQ_INCLUDE_DIR zmq.hpp REQUIRED)
find_path(INIH_INCLUDE_DIR ini.h REQUIRED)
find_library(INIH_LIBRARY NAMES inih REQUIRED)

add_library(
  energytop_core
  src/config/config.cpp
  src/collectors/battery_collector.cpp
  src/collectors/collector_registry.cpp
  src/stats/power_stats.cpp
  src/daemon/energy_daemon.cpp
)

target_include_directories(
  energytop_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CPPZMQ_INCLUDE_DIR}
    ${INIH_INCLUDE_DIR}
)

target_link_libraries(
  energytop_core
  PUBLIC
    PkgConfig::LIBZMQ
    ${INIH_LIBRARY}
)

add_executable(energytopd src/daemon/energytopd_main.cpp)
target_link_libraries(energytopd PRIVATE energytop_core)

add_executable(energytop src/cli/energytop_main.cpp)
target_link_libraries(energytop PRIVATE energytop_core)

if(BUILD_TESTING)
  find_package(GTest REQUIRED)

  add_executable(test_power_stats tests/unit/test_power_stats.cpp)
  target_link_libraries(test_power_stats PRIVATE energytop_core GTest::gtest_main)
  add_test(NAME unit_power_stats COMMAND test_power_stats)

  add_executable(test_config tests/unit/test_config.cpp)
  target_link_libraries(test_config PRIVATE energytop_core GTest::gtest_main)
  add_test(NAME unit_config COMMAND test_config)

  add_executable(
    test_collector_registry
    tests/unit/test_collector_registry.cpp
  )
  target_link_libraries(test_collector_registry PRIVATE energytop_core GTest::gtest_main)
  add_test(NAME unit_collector_registry COMMAND test_collector_registry)

  add_executable(
    test_battery_collector_mock_sysfs
    tests/system/test_battery_collector_mock_sysfs.cpp
  )
  target_link_libraries(test_battery_collector_mock_sysfs PRIVATE energytop_core GTest::gtest_main)
  add_test(NAME system_battery_collector_mock_sysfs COMMAND test_battery_collector_mock_sysfs)

  add_executable(
    test_daemon_mock_sysfs
    tests/system/test_daemon_mock_sysfs.cpp
  )
  target_link_libraries(test_daemon_mock_sysfs PRIVATE energytop_core GTest::gtest_main)
  add_test(NAME system_daemon_mock_sysfs COMMAND test_daemon_mock_sysfs)
endif()
