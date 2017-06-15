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
