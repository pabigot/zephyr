#
# Copyright (c) 2019 Peter Bigot Consulting, LLC
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.8.2)
find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(sdp8xx)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
