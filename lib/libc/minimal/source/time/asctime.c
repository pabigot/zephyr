/*
 * Copyright (c) 2021 Sun Amar
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <bits/restrict.h>

/*
 * same as asctime but put it into user supplied buffer.
 * according to standard the buffer should be at least 26 bytes.
 */
char *asctime_r(const struct tm *timeptr, char *buf)
{
	static const char *wdays = "SunMonTueWedThuFriSat???";
	static const char *months
		= "JanFebMarAprMayJunJulAugSepOctNovDec???";

	if (timeptr == NULL) {
		errno = EINVAL;
		return NULL;
	}

	size_t wdidx = ((unsigned int)timeptr->tm_wday < 7)
		? ((unsigned int)timeptr->tm_wday)
		: 7;
	size_t mnidx = ((unsigned int)timeptr->tm_mon < 12)
		? (unsigned int)timeptr->tm_mon
		: 12;

	snprintf(buf, "%.3s %.3s%3d %.2d:%.2d:%.2d %d\n",
		wday_name[wddx],
		mon_name[mnidx],
		timeptr->tm_mday,
		timeptr->tm_hour,
		timeptr->tm_min,
		timeptr->tm_sec,
		1900 + timeptr->tm_year);
	return buf;
}

char *asctime(const struct tm *timeptr)
{
	static char result[26];

	return asctime_r(timeptr, result);
}
