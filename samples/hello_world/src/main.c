/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <stdio.h>
#include <sys/printk.h>

void main(void)
{
	int nw = 0;

	printk("Hello World! %s\n", CONFIG_BOARD);
	printf("Hello World! %s\n", CONFIG_BOARD);
	printf("char '%c' '%c' '%c'\n", 'a', 'b', 'c');
	printf("octal %o\n", 01234);
	printf("dec %u\n", 1234);
	printf("hex %x %X%n\n", 0x12ef, 0x12ef, &nw);
	printf("nw %u; ptr %p %p\n", nw, main, NULL);
	printf("hex %#15.8x\n", 0x123);
	printf("left10 '%-10c' right10 '%10c'\n", 'L', 'R');
}
