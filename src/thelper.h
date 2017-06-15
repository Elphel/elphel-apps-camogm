/**
 * @file thelper.h
 * @brief Various helper functions to work with timeval structures
 * @copyright Copyright (C) 2017 Elphel Inc.
 * @author AUTHOR <EMAIL>
 *
 * @par License:
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _THELPER_H
#define _THELPER_H

#include <sys/time.h>

void time_normalize(struct timeval *tv);
int time_comp(struct timeval *t1, struct timeval *t2);

/**
 * Convert time represented by timeval structure to time in microseconds
 * @param   tv   time value to convert
 * @return  time in microseconds
 */
inline unsigned long long time_to_us(const struct timeval *tv)
{
	unsigned long long t;

	t = tv->tv_sec * 1000000;
	t += tv->tv_usec;

	return t;
}

/**
 * Convert time in microseconds to time represented by timeval structure
 * @param   us   time in microseconds
 * @return  time in timeval structure
 */
inline struct timeval us_to_time(unsigned long us)
{
	struct timeval tv;

	tv.tv_sec = us / 1000000;
	tv.tv_usec = us % 1000000;

	return tv;
}

#endif /* _THELPER_H */
