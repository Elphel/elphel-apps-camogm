/**
 * @file thelper.c
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

#include "thelper.h"

/**
 * Make sure that time represented by timeval structure has correct format, i.e.
 * the value of microseconds does not exceed 1000000.
 * @param   tv   time values to normalize
 * @return  None
 */
void time_normalize(struct timeval *tv)
{
	tv->tv_sec += tv->tv_usec / 1000000;
	tv->tv_usec = tv->tv_usec % 1000000;
}

/**
 * Compare two times represented by timeval structure
 * @param   t1   first time value
 * @param   t2   second time value
 * @return  1 if t1 > t2, 0 in case values are equal and -1 if t1 < t2
 */
int time_comp(struct timeval *t1, struct timeval *t2)
{
	if (t1->tv_sec > t2->tv_sec)
		return 1;
	if (t1->tv_sec == t2->tv_sec) {
		if (t1->tv_usec > t2->tv_usec)
			return 1;
		if (t1->tv_usec == t2->tv_usec)
			return 0;
	}
	return -1;
}

/**
 * Subtract one time value from another and return the difference
 * @param   tv1   time value to subtract from
 * @param   tv2   time value to be subtracted
 * @return  tv1 - tv2
 */
struct timeval time_sub(const struct timeval *tv1, const struct timeval *tv2)
{
	struct timeval ret_val = *tv1;

	ret_val.tv_sec -= 1;
	ret_val.tv_usec += 1000000;
	ret_val.tv_sec -= tv2->tv_sec;
	ret_val.tv_usec -= tv2->tv_usec;
	time_normalize(&ret_val);

	return ret_val;
}

/**
 * Add one time value to another and return the sum
 */
struct timeval time_add(const struct timeval *tv1, const struct timeval *tv2)
{
	struct timeval ret_val = *tv1;

	ret_val.tv_sec += tv2->tv_sec;
	ret_val.tv_usec += tv2->tv_usec;
	time_normalize(&ret_val);

	return ret_val;
}
