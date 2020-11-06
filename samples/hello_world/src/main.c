/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <stdio.h>
#include <sys/printk.h>
#include <sys/vcbprintf.h>


int out(int c, void *ctx)
{
	printk("%c", c);
	return c;
}

int do_print(const char* fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	int r1 = xsys_vcbprintf(out, NULL, fmt, ap);
	va_end(ap);

	va_start(ap, fmt);
	int r2 = sys_vcbprintf(out, NULL, fmt, ap);
	va_end(ap);
}

void main(void)
{
	int nw = 0;

	do_print("Hello World! %s\n", CONFIG_BOARD);
	do_print("char '%c' '%c' '%c'\n", 'a', 'b', 'c');
	do_print("octal %o\n", 01234);
	do_print("dec %u\n", 1234);
	do_print("hex %x %X%n\n", 0x12ef, 0x12ef, &nw);
	do_print("nw %u; ptr %p %p\n", nw, main, NULL);
	do_print("hex %#15.8x\n", 0x123);
	do_print("left10 '%-10c' right10 '%10c'\n", 'L', 'R');
	//      float /1234.567/1.23e+03/1.235e+03/
	do_print("float /%.3f/%.3g/%.3e/\n", 1234.567, 1234.567, 1234.567);
}
