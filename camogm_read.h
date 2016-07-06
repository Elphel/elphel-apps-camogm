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

/**
 * @struct disk_index
 * @brief Contains a single entry into disk index directory. Each node in
 * the disk index directory corresponds to a file in the raw device buffer and
 * hold its starting offset, sensor port number, time stamp and file size.
 * @var disk_index::next
 * Pointer to the next index node
 * @var disk_index::prev
 * Pointer to the previous disk index node
 * @var disk_index::rawtime
 * Time stamp in UNIX format
 * @var disk_index::usec
 * The microsecond part of the time stamp
 * @var disk_index::port
 * The sensor port number this frame was captured from
 * @var disk_index::f_size
 * File size in bytes
 * @var disk_index::f_offset
 * The offset of the file start in the raw device buffer (in bytes)
 */
struct disk_index {
	struct disk_index *next;
	struct disk_index *prev;
	time_t rawtime;
	unsigned int usec;
	uint32_t port;
	size_t f_size;
	uint64_t f_offset;
};

/**
 * @struct disk_idir
 * @brief Contains pointers to disk index directory
 * @var disk_idir::head
 * Pointer to the first node of disk index directory
 * @var disk_idir::tail
 * Pointer to the last node of disk index directory
 * @var disk_idir::size
 * The number of nodes in disk index directory
 */
struct disk_idir {
	struct disk_index *head;
	struct disk_index *tail;
	size_t size;
};

/**
 * @struct range
 * @brief Container for offsets in raw device buffer
 * @var range::from
 * Starting offset
 * @var range::to
 * Ending offset
 */
struct range {
	uint64_t from;
	uint64_t to;
};

void *reader(void *arg);

#endif /* _CAMOGM_READ_H */
