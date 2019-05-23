#
# Copyright (c) 2018 Peter Bigot Consulting, LLC
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.8.2)
find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(sht21)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
