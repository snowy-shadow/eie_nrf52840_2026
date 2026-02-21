#-------------------------------------------------------------------------------
# Zephyr Example Application
#
# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE main.cpp)
add_subdirectory(BLE)
