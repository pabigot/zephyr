/* fprintf.c */

/*
 * Copyright (c) 1997-2010, 2013-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stdio.h>
#include <sys/vcbprintf.h>

#define DESC(d) ((void *)d)

int fprintf(FILE *_MLIBC_RESTRICT F, const char *_MLIBC_RESTRICT format, ...)
{
	va_list vargs;
	int     r;

	va_start(vargs, format);
	r = sys_vcbprintf(fputc, DESC(F), format, vargs);
	va_end(vargs);

	return r;
}

int vfprintf(FILE *_MLIBC_RESTRICT F, const char *_MLIBC_RESTRICT format,
	     va_list vargs)
{
	int r;

	r = sys_vcbprintf(fputc, DESC(F), format, vargs);

	return r;
}

int printf(const char *_MLIBC_RESTRICT format, ...)
{
	va_list vargs;
	int     r;

	va_start(vargs, format);
	r = sys_vcbprintf(fputc, DESC(stdout), format, vargs);
	va_end(vargs);

	return r;
}

int vprintf(const char *_MLIBC_RESTRICT format, va_list vargs)
{
	int r;

	r = sys_vcbprintf(fputc, DESC(stdout), format, vargs);

	return r;
}
