/** @file camogm_read.h
 * @brief Provides reading data written to raw device storage and saving the data to a device with file system.
 * @copyright  Copyright (C) 2016 Elphel, Inc.
 *
 * <b>License:</b>
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _CAMOGM_READ_H
#define _CAMOGM_READ_H

#include "camogm.h"

struct disk_index {
	struct disk_index *next;
	struct disk_index *prev;
	time_t rawtime;
	unsigned int usec;
	uint32_t port;
	size_t f_size;
	uint64_t f_offset;
};

struct disk_idir {
	struct disk_index *head;
	struct disk_index *tail;
	size_t size;
};

void *build_index(void *arg);
void *reader(void *arg);

#endif /* _CAMOGM_READ_H */
