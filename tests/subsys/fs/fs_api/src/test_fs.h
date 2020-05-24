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

/* kenel only reserve two slots for specific file system.
 * By disable that two file systems, test cases can make
 * use of that slots to register a file systems for test
 */
#define TEST_FS_1 FS_FATFS
#define TEST_FS_2 FS_LITTLEFS
#define UNSUPPORTED_FS FS_TYPE_END

extern struct fs_file_system_t temp_fs;

struct test_fs_data {
	int reserve;
};

void test_fs_register(void);
void test_dir_file(void);
#endif
