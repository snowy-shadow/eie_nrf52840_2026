#-------------------------------------------------------------------------------
# Zephyr Example Application
#
# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(app LANGUAGES CXX C)

zephyr_include_directories(src)

target_include_directories(${CMAKE_PROJECT_NAME}
PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_subdirectory(src)
