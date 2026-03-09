cmake_minimum_required(VERSION 3.20)

project(energytop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

execute_process(
  COMMAND git rev-parse --short=8 HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE ENERGYTOP_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT ENERGYTOP_GIT_COMMIT)
  set(ENERGYTOP_GIT_COMMIT unknown)
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ENERGYTOP_ARCH_RAW)
if(ENERGYTOP_ARCH_RAW MATCHES "^(x86_64|amd64)$")
  set(ENERGYTOP_ARCH x64)
elseif(ENERGYTOP_ARCH_RAW MATCHES "^(aarch64|arm64)$")
  set(ENERGYTOP_ARCH arm64)
else()
  set(ENERGYTOP_ARCH "${ENERGYTOP_ARCH_RAW}")
endif()

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

set(ENERGYTOP_INSTALLER_NAME
    "energytop-installer-${ENERGYTOP_ARCH}-${ENERGYTOP_GIT_COMMIT}.sh"
)
set(ENERGYTOP_INSTALLER_STUB_IN
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/installer_stub.sh.in"
)
set(ENERGYTOP_INSTALLER_STUB
    "${CMAKE_CURRENT_BINARY_DIR}/energytop-installer.stub.sh"
)
set(ENERGYTOP_INSTALLER_PAYLOAD_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/installer_payload"
)
set(ENERGYTOP_INSTALLER_PAYLOAD_ARCHIVE
    "${CMAKE_CURRENT_BINARY_DIR}/energytop-installer.payload.tar.gz"
)
set(ENERGYTOP_INSTALLER_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/${ENERGYTOP_INSTALLER_NAME}"
)

configure_file("${ENERGYTOP_INSTALLER_STUB_IN}" "${ENERGYTOP_INSTALLER_STUB}" @ONLY)

add_custom_command(
  OUTPUT "${ENERGYTOP_INSTALLER_OUTPUT}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ENERGYTOP_INSTALLER_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${ENERGYTOP_INSTALLER_PAYLOAD_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:energytop>"
          "${ENERGYTOP_INSTALLER_PAYLOAD_DIR}/energytop"
  COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:energytopd>"
          "${ENERGYTOP_INSTALLER_PAYLOAD_DIR}/energytopd"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/energytop.ini"
          "${ENERGYTOP_INSTALLER_PAYLOAD_DIR}/energytop.ini"
  COMMAND /bin/bash -c
          "tar -C \"${ENERGYTOP_INSTALLER_PAYLOAD_DIR}\" -czf \"${ENERGYTOP_INSTALLER_PAYLOAD_ARCHIVE}\" energytop energytopd energytop.ini"
  COMMAND /bin/bash -c
          "cat \"${ENERGYTOP_INSTALLER_STUB}\" \"${ENERGYTOP_INSTALLER_PAYLOAD_ARCHIVE}\" > \"${ENERGYTOP_INSTALLER_OUTPUT}\""
  COMMAND /bin/chmod +x "${ENERGYTOP_INSTALLER_OUTPUT}"
  DEPENDS
    energytop
    energytopd
    "${CMAKE_CURRENT_SOURCE_DIR}/energytop.ini"
    "${ENERGYTOP_INSTALLER_STUB}"
  VERBATIM
)

add_custom_target(energytop_installer ALL DEPENDS "${ENERGYTOP_INSTALLER_OUTPUT}")

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
