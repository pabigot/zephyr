/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_fs.h"

/**
 * @brief Test file system interface implemented in kernel
 *
 * @defgroup filesystem File System
 *
 * @verify {@req{115}}
 *
 * @ingroup all_test
 */

void test_main(void)
{
	ztest_test_suite(fat_fs_basic_test,
			 ztest_unit_test(test_fs_register),
			 ztest_unit_test(test_dir_file)
			 );
	ztest_run_test_suite(fat_fs_basic_test);
}
