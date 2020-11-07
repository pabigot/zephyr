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
	extern void z_vprintk(sys_vcbprintf_cb out, void *ctx, const char *fmt, va_list ap);

	va_list ap;

	if (true) {
		va_start(ap, fmt);
		z_vprintk(out, NULL, fmt, ap);
		va_end(ap);
	}

	if (true) {
		va_start(ap, fmt);
		xsys_vcbprintf(out, NULL, fmt, ap);
		va_end(ap);
	}

	if (true) {
		va_start(ap, fmt);
		sys_vcbprintf(out, NULL, fmt, ap);
		va_end(ap);
	}

	return 0;
}

void main(void)
{
	int nw = 0;

	do_print("Hello World! %s\n", CONFIG_BOARD);
	do_print("char '%c' '%c' '%c'\n", 'a', 'b', 'c');

	do_print("i hh '%hhi' h '%hi' '%i'\n", 0x12345678, 0x12345678, 0x12345678);
	do_print("x hh '%hhx' h '%hx' '%x'\n", 0x12345678, 0x12345678, 0x12345678);
	do_print("str %s %.2s\n", "abcd", "abcd");
	do_print("octal %o\n", 01234);
	do_print("dec %u\n", 1234);
	do_print("hex %x %X%n\n", 0x12ef, 0x12ef, &nw);
	do_print("nw %u; ptr %p %p\n", nw, main, NULL);
	do_print("hex %#15.8x\n", 0x123);
	do_print("left10 '%-10c' right10 '%10c'\n", 'L', 'R');
	//      float /1234.567/1.23e+03/1.235e+03/
	//do_print("float /%.3f/%.3g/%.3e/\n", 1234.567, 1234.567, 1234.567);
	do_print("frac /%.3f/%.3g/%.6e/ %x\n", 0.001234, 0.001234, 0.001234, 0xcafe);
}
