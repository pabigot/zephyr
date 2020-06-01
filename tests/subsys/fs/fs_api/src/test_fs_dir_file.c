/*
 * Copyright (c) 2020 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_fs.h"
#include <stdio.h>
#include <string.h>

struct fs_file_system_t null_fs = {NULL};
static struct test_fs_data test_data;

static struct fs_mount_t test_fs_mnt_1 = {
		.type = TEMP_FS,
		.mnt_point = TEST_FS_MNTP,
		.fs_data = &test_data,
};

static struct fs_mount_t test_fs_mnt_unsupported_fs = {
		.type = UNSUPPORTED_FS,
		.mnt_point = "/MMCBLOCK:",
		.fs_data = &test_data,
};

static struct fs_mount_t test_fs_mnt_invalid_root = {
		.type = TEMP_FS,
		.mnt_point = "SDA:",
		.fs_data = &test_data,
};

static struct fs_mount_t test_fs_mnt_already_mounted = {
		.type = TEMP_FS,
		.mnt_point = TEST_FS_MNTP,
		.fs_data = &test_data,
};

static struct fs_mount_t test_fs_mnt_invalid_parm = {
		.type = TEMP_FS,
		.mnt_point = "/SDA",
		.fs_data = &test_data,
};

static struct fs_mount_t test_fs_mnt_no_op = {
		.type = TEMP_FS,
		.mnt_point = "/SDA:",
		.fs_data = &test_data,
};

static struct fs_file_t filep;
static const char test_str[] = "hello world!";

static int test_mount(void)
{
	int ret;

	TC_PRINT("\nmount tests:\n");
	TC_PRINT("Mount to a NULL directory\n");
	ret = fs_mount(NULL);
	if (!ret) {
		return TC_FAIL;
	}

	TC_PRINT("Mount an unsupported fs\n");
	ret = fs_mount(&test_fs_mnt_unsupported_fs);
	zassert_equal(ret, -ENOENT,
		      "unsupported: %d", ret);

	fs_register(TEMP_FS, &temp_fs);
	TC_PRINT("Mount to an invalid directory\n");
	ret = fs_mount(&test_fs_mnt_invalid_root);
	if (!ret) {
		return TC_FAIL;
	}

	TC_PRINT("Invalid parameter pass to file system operation interface\n");
	ret = fs_mount(&test_fs_mnt_invalid_parm);
	if (!ret) {
		return TC_FAIL;
	}

	ret = fs_mount(&test_fs_mnt_1);
	if (ret < 0) {
		TC_PRINT("Error mounting fs [%d]\n", ret);
		return TC_FAIL;
	}

	TC_PRINT("Mount to a directory that has file system mounted already\n");
	ret = fs_mount(&test_fs_mnt_already_mounted);
	if (!ret) {
		return TC_FAIL;
	}

	fs_unregister(TEMP_FS, &temp_fs);

	fs_register(TEMP_FS, &null_fs);
	TC_PRINT("Mount a file system has no interface implemented");
	ret = fs_mount(&test_fs_mnt_no_op);
	if (!ret) {
		TC_PRINT("Should not mount to a fs which has no interface\n");
		return TC_FAIL;
	}
	fs_unregister(TEMP_FS, &null_fs);


	return TC_PASS;
}

static int test_unmount(void)
{
	int ret;

	TC_PRINT("\nunmount tests:\n");

	TC_PRINT("\nunmount nothing:\n");
	ret = fs_unmount(NULL);
	if (!ret) {
		return TC_FAIL;
	}

	TC_PRINT("\nunmount file system that has never been mounted:\n");
	ret = fs_unmount(&test_fs_mnt_unsupported_fs);
	if (!ret) {
		return TC_FAIL;
	}

	TC_PRINT("\nunmount file system multiple times:\n");
	ret = fs_unmount(&test_fs_mnt_1);
	if (ret < 0) {
		TC_PRINT("Error mounting fs [%d]\n", ret);
		return TC_FAIL;
	}

	test_fs_mnt_1.fs = &temp_fs;
	ret = fs_unmount(&test_fs_mnt_1);
	if (!ret) {
		TC_PRINT("Can't unmount multi times\n");
		return TC_FAIL;
	}

	return TC_PASS;
}

static int test_file_statvfs(void)
{
	struct fs_statvfs stat;
	int ret;

	ret = fs_statvfs(NULL, &stat);
	if (!ret) {
		TC_PRINT("Should not get volume stats without path\n");
		return TC_FAIL;
	}

	ret = fs_statvfs("/SDCARD:", &stat);
	if (!ret) {
		TC_PRINT("Should not get volume stats with non-exist path\n");
		return TC_FAIL;
	}

	ret = fs_statvfs(TEST_FS_MNTP, NULL);
	if (!ret) {
		TC_PRINT("Don't get volume stats without stat structure\n");
		return TC_FAIL;
	}

	ret = fs_statvfs(TEST_FS_MNTP, &stat);
	if (ret) {
		TC_PRINT("Error getting volume stats [%d]\n", ret);
		return ret;
	}

	TC_PRINT("\n");
	TC_PRINT("Optimal transfer block size   = %lu\n", stat.f_bsize);
	TC_PRINT("Allocation unit size          = %lu\n", stat.f_frsize);
	TC_PRINT("Volume size in f_frsize units = %lu\n", stat.f_blocks);
	TC_PRINT("Free space in f_frsize units  = %lu\n", stat.f_bfree);

	return TC_PASS;
}

static int test_mkdir(void)
{
	int ret;

	TC_PRINT("\nmkdir tests:\n");

	ret = fs_mkdir(NULL);
	if (!ret) {
		TC_PRINT("Should not create a null directory\n");
		return TC_FAIL;
	}

	ret = fs_mkdir("/SDCARD:/testdir");
	if (!ret) {
		TC_PRINT("Don't create dir in a dir there in no fs mount\n");
		return TC_FAIL;
	}

	ret = fs_mkdir(TEST_FS_MNTP);
	if (!ret) {
		TC_PRINT("Should not create dir that is root dir\n");
		return TC_FAIL;
	}

	ret = fs_mkdir(TEST_DIR);
	if (ret) {
		TC_PRINT("Error creating dir[%d]\n", ret);
		return ret;
	}

	TC_PRINT("Created dir %s!\n", TEST_DIR);

	return ret;
}

static int test_opendir(void)
{
	int ret;
	struct fs_dir_t dirp;

	TC_PRINT("\nopendir tests:\n");

	memset(&dirp, 0, sizeof(dirp));
	TC_PRINT("Test null path\n");
	ret = fs_opendir(NULL, NULL);
	if (!ret) {
		return TC_FAIL;
	}

	TC_PRINT("Test root directory\n");
	ret = fs_opendir(&dirp, "/");
	if (ret) {
		return TC_FAIL;
	}

	TC_PRINT("Test non-exist mount point\n");
	ret = fs_opendir(&dirp, "/SDCARD:/test_dir");
	if (!ret) {
		return TC_FAIL;
	}

	ret = fs_opendir(&dirp, TEST_DIR);
	if (ret) {
		TC_PRINT("Error opening dir\n");
		return TC_FAIL;
	}

	TC_PRINT("Open same directory multi times\n");
	ret = fs_opendir(&dirp, TEST_DIR);
	if (!ret) {
		return TC_FAIL;
	}

	TC_PRINT("Opening dir successfully\n");
	return TC_PASS;
}

static int test_closedir(void)
{
	int ret;
	struct fs_dir_t dirp;

	TC_PRINT("\nclosedir tests: %s\n", TEST_DIR);
	memset(&dirp, 0, sizeof(dirp));
	ret = fs_opendir(&dirp, TEST_DIR);
	if (ret) {
		TC_PRINT("Error opening dir\n");
		return TC_FAIL;
	}

	ret = fs_closedir(&dirp);
	if (ret) {
		TC_PRINT("Error close dir\n");
		return TC_FAIL;
	}

	dirp.mp = &test_fs_mnt_1;
	ret = fs_closedir(&dirp);
	if (!ret) {
		return TC_FAIL;
	}

	return TC_PASS;
}

/**
 * @brief Test lsdir interface include opendir, readdir, closedir
 */

static int test_lsdir(const char *path)
{
	int ret;
	struct fs_dir_t dirp;
	struct fs_dirent entry;

	TC_PRINT("\nlsdir tests:\n");

	memset(&dirp, 0, sizeof(dirp));
	memset(&entry, 0, sizeof(entry));

	TC_PRINT("read an unopened dir\n");
	dirp.dirp = "somepath";
	ret = fs_readdir(&dirp, &entry);
	if (!ret) {
		return TC_FAIL;
	}

	dirp.mp = &test_fs_mnt_1;
	ret = fs_readdir(&dirp, NULL);
	if (!ret) {
		return TC_FAIL;
	}

	TC_PRINT("read an opened dir\n");
	ret = fs_opendir(&dirp, path);
	if (ret) {
		if (path) {
			TC_PRINT("Error opening dir %s [%d]\n", path, ret);
		}
		return TC_FAIL;
	}

	TC_PRINT("\nListing dir %s:\n", path);
	for (;;) {
		ret = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (ret || entry.name[0] == 0) {
			break;
		}

		if (entry.type == FS_DIR_ENTRY_DIR) {
			TC_PRINT("[DIR ] %s\n", entry.name);
		} else {
			TC_PRINT("[FILE] %s (size = %zu)\n",
				entry.name, entry.size);
		}
	}

	ret = fs_closedir(&dirp);
	if (ret) {
		TC_PRINT("Error close a directory\n");
		return TC_FAIL;
	}

	return ret;
}

/**
 * @brief Open a existing file or create a new file
 *
 * @addtogroup filesystem
 *
 */

static int test_file_open(void)
{
	int ret;

	TC_PRINT("\nOpen tests:\n");

	TC_PRINT("\nOpen a file without a path\n");
	ret = fs_open(&filep, NULL);
	if (!ret) {
		return TC_FAIL;
	}

	TC_PRINT("\nOpen a file with wrong abs path\n");
	ret = fs_open(&filep, "/test_file.txt");
	if (!ret) {
		return TC_FAIL;
	}

	ret = fs_open(&filep, TEST_FILE);
	if (ret) {
		TC_PRINT("Failed opening file [%d]\n", ret);
		return ret;
	}

	TC_PRINT("\nReopen the same file");
	ret = fs_open(&filep, TEST_FILE);
	if (!ret) {
		return TC_FAIL;
	}

	TC_PRINT("Opened file %s\n", TEST_FILE);

	return TC_PASS;
}

/**
 * @brief Write items of data of size bytes long
 *
 * @addtogroup filesystem
 *
 */

int test_file_write(void)
{
	ssize_t brw;
	int ret;

	TC_PRINT("\nWrite tests:\n");

	ret = fs_seek(&filep, 0, FS_SEEK_SET);
	if (ret) {
		TC_PRINT("fs_seek failed [%d]\n", ret);
		fs_close(&filep);
		return ret;
	}

	TC_PRINT("Write to file from a invalid source\n");
	brw = fs_write(&filep, NULL, strlen(test_str));
	if (brw >= 0) {
		return TC_FAIL;
	}

	TC_PRINT("Data written:\"%s\"\n\n", test_str);

	brw = fs_write(&filep, (char *)test_str, strlen(test_str));
	if (brw < 0) {
		TC_PRINT("Failed writing to file [%zd]\n", brw);
		fs_close(&filep);
		return brw;
	}

	if (brw < strlen(test_str)) {
		TC_PRINT("Unable to complete write. Volume full.\n");
		TC_PRINT("Number of bytes written: [%zd]\n", brw);
		fs_close(&filep);
		return TC_FAIL;
	}

	TC_PRINT("Data successfully written!\n");

	return ret;
}

/**
 * @brief Flush the cache of an open file
 *
 * @addtogroup filesystem
 *
 */

static int test_file_sync(void)
{
	int ret;
	ssize_t brw;

	TC_PRINT("\nSync tests:\n");

	ret = fs_open(&filep, TEST_FILE);

	for (;;) {
		brw = fs_write(&filep, (char *)test_str, strlen(test_str));
		if (brw < strlen(test_str)) {
			break;
		}
		ret = fs_sync(&filep);
		if (ret) {
			TC_PRINT("Error syncing file [%d]\n", ret);
			fs_close(&filep);
			return ret;
		}

		ret = fs_tell(&filep);
		if (ret < 0) {
			TC_PRINT("Error tell file [%d]\n", ret);
			fs_close(&filep);
			return ret;
		}

	}

	TC_PRINT("Sync a overflowed file\n");
	ret = fs_sync(&filep);
	if (!ret) {
		fs_close(&filep);
		return TC_FAIL;
	}

	TC_PRINT("Tell a overflowed file\n");
	ret = fs_tell(&filep);
	if (!ret) {
		fs_close(&filep);
		return TC_FAIL;
	}

	fs_close(&filep);
	return TC_PASS;
}

/**
 * @brief Read items of data of size bytes long
 *
 * @addtogroup filesystem
 *
 */

static int test_file_read(void)
{
	ssize_t brw;
	char read_buff[80];
	size_t sz = strlen(test_str);

	TC_PRINT("\nRead tests:\n");

	TC_PRINT("Read to a invalid buffer\n");
	brw = fs_read(&filep, NULL, sz);
	if (brw >= 0) {
		return TC_FAIL;
	}

	brw = fs_read(&filep, read_buff, sz);
	if (brw < 0) {
		TC_PRINT("Failed reading file [%zd]\n", brw);
		fs_close(&filep);
		return brw;
	}

	read_buff[brw] = 0;

	TC_PRINT("Data read:\"%s\"\n\n", read_buff);

	if (strcmp(test_str, read_buff)) {
		TC_PRINT("Error - Data read does not match data written\n");
		TC_PRINT("Data read:\"%s\"\n\n", read_buff);
		return TC_FAIL;
	}

	TC_PRINT("Data read matches data written\n");

	return TC_PASS;
}

/**
 * @brief Truncate the file to the new length
 *
 * @details This test include three cases:
 *            - fs_seek, locate the position to truncate
 *            - fs_truncate
 *            - fs_tell, retrieve the current position
 *
 * @addtogroup filesystem
 *
 */

static int test_file_truncate(void)
{
	int ret;
	off_t orig_pos;
	char read_buff[80];
	ssize_t brw;

	TC_PRINT("\nTruncate tests: max file size is 128byte\n");

	TC_PRINT("Truncating to size larger than 128byte\n");
	ret = fs_truncate(&filep, 256);
	if (!ret) {
		fs_close(&filep);
		return TC_FAIL;
	}

	/* Test truncating to 0 size */
	TC_PRINT("\nTesting shrink to 0 size\n");
	ret = fs_truncate(&filep, 0);
	if (ret) {
		TC_PRINT("fs_truncate failed [%d]\n", ret);
		fs_close(&filep);
		return ret;
	}

	TC_PRINT("File seek from invalid whence\n");
	ret = fs_seek(&filep, 0, 100);
	if (!ret) {
		fs_close(&filep);
		return TC_FAIL;
	}

	fs_seek(&filep, 0, FS_SEEK_END);
	if (fs_tell(&filep) > 0) {
		TC_PRINT("Failed truncating to size 0\n");
		fs_close(&filep);
		return TC_FAIL;
	}

	TC_PRINT("Testing write after truncating\n");
	ret = test_file_write();
	if (ret) {
		TC_PRINT("Write failed after truncating\n");
		return ret;
	}

	fs_seek(&filep, 0, FS_SEEK_END);

	orig_pos = fs_tell(&filep);
	TC_PRINT("Original size of file = %d\n", (int)orig_pos);

	/* Test shrinking file */
	TC_PRINT("\nTesting shrinking\n");
	ret = fs_truncate(&filep, orig_pos - 5);
	if (ret) {
		TC_PRINT("fs_truncate failed [%d]\n", ret);
		fs_close(&filep);
		return ret;
	}

	fs_seek(&filep, 0, FS_SEEK_END);
	TC_PRINT("File size after shrinking by 5 bytes = %d\n",
						(int)fs_tell(&filep));
	if (fs_tell(&filep) != (orig_pos - 5)) {
		TC_PRINT("File size after fs_truncate not as expected\n");
		fs_close(&filep);
		return TC_FAIL;
	}

	/* Test expanding file */
	TC_PRINT("\nTesting expanding\n");
	fs_seek(&filep, 0, FS_SEEK_END);
	orig_pos = fs_tell(&filep);
	ret = fs_truncate(&filep, orig_pos + 10);
	if (ret) {
		TC_PRINT("fs_truncate failed [%d]\n", ret);
		fs_close(&filep);
		return ret;
	}

	fs_seek(&filep, 0, FS_SEEK_END);
	TC_PRINT("File size after expanding by 10 bytes = %d\n",
						(int)fs_tell(&filep));
	if (fs_tell(&filep) != (orig_pos + 10)) {
		TC_PRINT("File size after fs_truncate not as expected\n");
		fs_close(&filep);
		return TC_FAIL;
	}

	/* Check if expanded regions are zeroed */
	TC_PRINT("Testing for zeroes in expanded region\n");
	fs_seek(&filep, -5, FS_SEEK_END);

	brw = fs_read(&filep, read_buff, 5);

	if (brw < 5) {
		TC_PRINT("Read failed after truncating\n");
		fs_close(&filep);
		return -1;
	}

	for (int i = 0; i < 5; i++) {
		if (read_buff[i]) {
			TC_PRINT("Expanded regions are not zeroed\n");
			fs_close(&filep);
			return TC_FAIL;
		}
	}

	return TC_PASS;
}

/**
 * @brief Flush associated stream and close the file
 *
 * @addtogroup filesystem
 *
 */

int test_file_close(void)
{
	int ret;

	TC_PRINT("\nClose tests:\n");

	ret = fs_close(&filep);
	if (ret) {
		TC_PRINT("Error closing file [%d]\n", ret);
		return ret;
	}

	TC_PRINT("\nClose a closed file:\n");
	filep.mp = &test_fs_mnt_1;
	ret = fs_close(&filep);
	if (!ret) {
		return TC_FAIL;
	}

	TC_PRINT("Closed file %s\n", TEST_FILE);

	return TC_PASS;
}

/**
 * @brief Rename a file or directory
 *
 * @addtogroup filesystem
 *
 */

static int test_file_rename(void)
{
	int ret = TC_FAIL;

	TC_PRINT("\nRename file tests:\n");

	ret = fs_rename(NULL, NULL);
	if (!ret) {
		TC_PRINT("Should not rename a null file\n");
		ret = TC_FAIL;
	}

	ret = fs_rename("/SDCARD:/testfile.txt", TEST_FILE_RN);
	if (!ret) {
		TC_PRINT("Should no rename a non-exist file\n");
		ret = TC_FAIL;
	}

	ret = fs_rename(TEST_FILE, "/SDCARD:/testfile_renamed.txt");
	if (!ret) {
		TC_PRINT("Should not rename file to different mount point\n");
		ret = TC_FAIL;
	}

	ret = fs_rename(TEST_FILE, TEST_FILE_EX);
	if (!ret) {
		TC_PRINT("Should no rename a file to a file exist\n");
		ret = TC_FAIL;
	}

	ret = fs_rename(TEST_FILE, TEST_FILE_RN);
	if (ret) {
		TC_PRINT("Rename file failed\n");
		ret = TC_FAIL;
	}

	return ret;
}

/**
 * @brief Check the status of a file or directory specified by the path
 *
 * @addtogroup filesystem
 *
 */

int test_file_stat(void)
{
	int ret;
	struct fs_dirent entry;

	TC_PRINT("\nStat file tests:\n");

	ret = fs_stat(NULL, &entry);
	if (!ret) {
		TC_PRINT("Should no stat a null directory\n");
		ret = TC_FAIL;
	}

	ret = fs_stat("/SDCARD", &entry);
	if (!ret) {
		TC_PRINT("Should no stat a non-exist directory\n");
		ret = TC_FAIL;
	}

	ret = fs_stat(TEST_DIR, NULL);
	if (!ret) {
		TC_PRINT("Should no stat a directory without entry\n");
		ret = TC_FAIL;
	}

	ret = fs_stat(TEST_DIR, &entry);
	if (ret) {
		TC_PRINT("Stat directory failed\n");
		ret = TC_FAIL;
	}

	ret = fs_stat(TEST_DIR_FILE, &entry);
	if (ret) {
		TC_PRINT("Stat file failed\n");
		ret = TC_FAIL;
	}

	return ret;
}

/**
 * @brief Delete the specified file or directory
 *
 * @addtogroup filesystem
 *
 */

static int test_file_unlink(void)
{
	int ret;

	TC_PRINT("\nDelete tests:\n");

	ret = fs_unlink(NULL);
	if (!ret) {
		TC_PRINT("Should not delete a null file\n");
		return TC_FAIL;
	}

	ret = fs_unlink("/SDCARD:/test_file.txt");
	if (!ret) {
		TC_PRINT("Should no delete a non-exist file\n");
		return TC_FAIL;
	}

	ret = fs_unlink(TEST_FS_MNTP);
	if (!ret) {
		TC_PRINT("Should not delete root dir\n");
		return TC_FAIL;
	}

	ret = fs_unlink(TEST_FILE_RN);
	if (ret) {
		TC_PRINT("Error deleting file [%d]\n", ret);
		return ret;
	}

	TC_PRINT("File (%s) deleted successfully!\n", TEST_FILE_RN);

	return ret;
}

/**
 * @brief Common file system operations through a general interface
 *
 * @details After register file system:
 *            - mount
 *            - statvfs
 *            - mkdir
 *            - opendir
 *            - readdir
 *            - closedir
 *            - open
 *            - write
 *            - read
 *            - lseek
 *            - tell
 *            - truncate
 *            - sync
 *            - close
 *            - rename
 *            - stat
 *            - unlink
 *            - unmount
 *            - unregister file system
 *          the order of test cases is critical, one case depend on ther
 *          case before it.
 *
 * @addtogroup filesystem
 *
 */

void test_dir_file(void)
{
	fs_register(TEMP_FS, &temp_fs);

	zassert_true(test_mount() == TC_PASS, NULL);
	zassert_true(test_file_statvfs() == TC_PASS, NULL);
	zassert_true(test_mkdir() == TC_PASS, NULL);
	zassert_true(test_opendir() == TC_PASS, NULL);
	zassert_true(test_closedir() == TC_PASS, NULL);
	zassert_true(test_lsdir(NULL) == TC_FAIL, NULL);
	zassert_true(test_lsdir("/") == TC_PASS, NULL);
	zassert_true(test_lsdir("/test") == TC_FAIL, NULL);
	zassert_true(test_lsdir(TEST_DIR) == TC_PASS, NULL);
	zassert_true(test_file_open() == TC_PASS, NULL);
	zassert_true(test_file_write() == TC_PASS, NULL);
	zassert_true(test_file_read() == TC_PASS, NULL);
	zassert_true(test_file_truncate() == TC_PASS, NULL);
	zassert_true(test_file_close() == TC_PASS, NULL);
	zassert_true(test_file_sync() == TC_PASS, NULL);
	zassert_true(test_file_rename() == TC_PASS, NULL);
	zassert_true(test_file_stat() == TC_PASS, NULL);
	zassert_true(test_file_unlink() == TC_PASS, NULL);
	zassert_true(test_unmount() == TC_PASS, NULL);

	fs_unregister(TEMP_FS, &temp_fs);
}
