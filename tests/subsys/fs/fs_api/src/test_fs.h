/*
 * Copyright (c) 2020 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __TEST_FS_H__
#define __TEST_FS_H__

#include <zephyr.h>
#include <ztest.h>
#include <fs/fs.h>

#define TEST_FS_MNTP	"/NAND:"
#define TEST_FILE	TEST_FS_MNTP"/testfile.txt"
#define TEST_FILE_RN	TEST_FS_MNTP"/testfile_renamed.txt"
#define TEST_FILE_EX	TEST_FS_MNTP"/testfile_exist.txt"
#define TEST_DIR	TEST_FS_MNTP"/testdir"
#define TEST_DIR_FILE	TEST_FS_MNTP"/testdir/testfile.txt"

/* Expose the identifier to use for the stub file system implementation. */
#define TEMP_FS FS_TYPE_EXTERNAL_BASE

/* Expose an identifier for an unknown file system implementation */
#define UNSUPPORTED_FS (FS_TYPE_EXTERNAL_BASE + 1)

extern struct fs_file_system_t temp_fs;

struct test_fs_data {
	int reserve;
};

void test_fs_register(void);
void test_dir_file(void);
#endif
