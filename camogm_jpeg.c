/** @file camogm_jpeg.c
 * @brief Provides writing to series of individual JPEG files for camogm
 * @copyright Copyright (C) 2016 Elphel, Inc.
 *
 * @par <b>License</b>
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** @brief This define is needed to use lseek64 and should be set before includes */
#define _LARGEFILE64_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#include "camogm_jpeg.h"

int camogm_init_jpeg(camogm_state *state)
{
	return 0;
}

void camogm_free_jpeg(void)
{
}

/**
 * @brief Called every time the JPEG files recording is started.
 *
 * This function checks if the raw device write is initiated and tries to open the device specified. The device
 * will be closed in #camogm_end_jpeg function.
 * @param[in]   state   a pointer to a structure containing current state
 * @return      0 if the device was opened successfully and negative error code otherwise
 */
int camogm_start_jpeg(camogm_state *state)
{
	char * slash;
	int rslt;

	if (!state->rawdev_op) {
		strcpy(state->path, state->path_prefix); //!make state->path a directory name (will be replaced when the frames will be written)
		slash = strrchr(state->path, '/');
		D2(fprintf(debug_file, "camogm_start_jpeg\n"));
		if (slash) {
			D3(fprintf(debug_file, "Full path %s\n", state->path));
			slash[0] = '\0'; //! truncate path to the directory name
			D3(fprintf(debug_file, "directory path %s\n", state->path));
			rslt = mkdir(state->path, 0777);
			D3(fprintf(debug_file, "mkdir (%s, 0777) returned %d, errno=%d\n", state->path, rslt, errno));
			if ((rslt < 0) && (errno != EEXIST)) { // already exists is OK
				D0(fprintf(debug_file, "Error creating directory %s, errno=%d\n", state->path, errno));
				return -CAMOGM_FRAME_FILE_ERR;
			}
		}
	} else {
		if (state->rawdev_op) {
			state->rawdev.rawdev_fd = open(state->rawdev.rawdev_path, O_RDWR);
			if (state->rawdev.rawdev_fd < 0) {
				D0(perror(__func__));
				D0(fprintf(debug_file, "Error opening raw device %s\n", state->rawdev.rawdev_path));
				return -CAMOGM_FRAME_FILE_ERR;
			}
			D3(fprintf(debug_file, "Open raw device %s; start_pos = %llu, end_pos = %llu, curr_pos = %llu\n", state->rawdev.rawdev_path,
					state->rawdev.start_pos, state->rawdev.end_pos, state->rawdev.curr_pos_w));
			lseek64(state->rawdev.rawdev_fd, state->rawdev.curr_pos_w, SEEK_SET);
		}
	}

	return 0;
}

/**
 * @brief Write single JPEG frame
 *
 * This function will write single JPEG file
 * @param   state   a pointer to a structure containing current state
 * @return
 */
int camogm_frame_jpeg(camogm_state *state)
{
	int i, j, k, split_index;
	int chunks_used = state->chunk_index - 1;
	ssize_t iovlen, l = 0;
	struct iovec chunks_iovec[8];
	unsigned char *split_ptr = NULL;
	long split_cntr = 0;
	int port = state->port_num;

	if (!state->rawdev_op) {
		l = 0;
		for (i = 0; i < (state->chunk_index) - 1; i++) {
			chunks_iovec[i].iov_base = state->packetchunks[i + 1].chunk;
			chunks_iovec[i].iov_len = state->packetchunks[i + 1].bytes;
			l += chunks_iovec[i].iov_len;
		}
		sprintf(state->path, "%s%010ld_%06ld.jpeg", state->path_prefix, state->this_frame_params[port].timestamp_sec, state->this_frame_params[port].timestamp_usec);
		if (((state->ivf = open(state->path, O_RDWR | O_CREAT, 0777))) < 0) {
			D0(fprintf(debug_file, "Error opening %s for writing, returned %d, errno=%d\n", state->path, state->ivf, errno));
			return -CAMOGM_FRAME_FILE_ERR;
		}
		iovlen = writev(state->ivf, chunks_iovec, (state->chunk_index) - 1);
		if (iovlen < l) {
			j = errno;
			D0(fprintf(debug_file, "writev error %d (returned %d, expected %d)\n", j, iovlen, l));
			close(state->ivf);
			return -CAMOGM_FRAME_FILE_ERR;
		}
		close(state->ivf);
	} else {
		D0(fprintf(debug_file, "\n%s: current pointers start_pos = %llu, end_pos = %llu, curr_pos = %llu, data in buffer %d\n", __func__,
				state->rawdev.start_pos, state->rawdev.end_pos, state->rawdev.curr_pos_w, l));
		split_index = -1;
		for (int i = 0, total_len = 0; i < state->chunk_index - 1; i++) {
			total_len += state->packetchunks[i + 1].bytes;
			if (total_len + state->rawdev.curr_pos_w > state->rawdev.end_pos) {
				split_index = i;
				chunks_used++;
				D0(fprintf(debug_file, "\n>>> raw storage roll over detected\n"));
				break;
			}
		}
		k = 0;
		l = 0;
		for (int i = 0; i < chunks_used; i++) {
			++k;
			if (i == split_index) {
				// one of the chunks rolls over the end of the raw storage, split it into two segments and
				// use additional chunk in chunks_iovec for this additional segment
				split_cntr = state->rawdev.end_pos - (l + state->rawdev.curr_pos_w);
				split_ptr = state->packetchunks[k].chunk + split_cntr;

				D3(fprintf(debug_file, "Splitting chunk #%d: total chunk size = %ld, start address = 0x%p\n",
						i, state->packetchunks[k].bytes, state->packetchunks[k].chunk));

				// be careful with indexes here
				chunks_iovec[i].iov_base = state->packetchunks[k].chunk;
				chunks_iovec[i].iov_len = split_cntr;
				l += chunks_iovec[i].iov_len;
				chunks_iovec[++i].iov_base = split_ptr + 1;
				chunks_iovec[i].iov_len = state->packetchunks[k].bytes - split_cntr;
				l += chunks_iovec[i].iov_len;
			} else {
				chunks_iovec[i].iov_base = state->packetchunks[k].chunk;
				chunks_iovec[i].iov_len = state->packetchunks[k].bytes;
				l += chunks_iovec[i].iov_len;
			}
		}

		/* debug code follows */
		fprintf(debug_file, "\n=== raw device write, iovec dump ===\n");
		fprintf(debug_file, "split_cntr = %ld; split_ptr = %p; split_index = %d\n", split_cntr, split_ptr, split_index);
		for (int i = 0; i < chunks_used; i++) {
			fprintf(debug_file, "i = %d; iov_base = %p; iov_len = %u\n", i, chunks_iovec[i].iov_base, chunks_iovec[i].iov_len);
		}
		fprintf(debug_file, "total len = %d\n======\n", l);
		/* end of debug code */

		if (split_index < 0) {
			iovlen = writev(state->rawdev.rawdev_fd, chunks_iovec, chunks_used);
		} else {
			iovlen = writev(state->rawdev.rawdev_fd, chunks_iovec, split_index + 1);
			fprintf(debug_file, "write first part: split_index = %d, %d bytes written\n", split_index, iovlen);
			if (lseek64(state->rawdev.rawdev_fd, state->rawdev.start_pos, SEEK_SET) != state->rawdev.start_pos) {
				perror(__func__);
				D0(fprintf(debug_file, "error positioning file pointer to the beginning of raw device\n"));
				return -CAMOGM_FRAME_FILE_ERR;
			}
			state->rawdev.overrun++;
			iovlen += writev(state->rawdev.rawdev_fd, &chunks_iovec[split_index + 1], chunks_used - split_index);
			fprintf(debug_file, "write second part: split_index + 1 = %d, chunks_used - split_index = %d, %d bytes written in total\n",
					split_index + 1, chunks_used - split_index, iovlen);
		}
		if (iovlen < l) {
			j = errno;
			perror(__func__);
			D0(fprintf(debug_file, "writev error %d (returned %d, expected %d)\n", j, iovlen, l));
			return -CAMOGM_FRAME_FILE_ERR;
		}
		state->rawdev.curr_pos_w += l;
		if (state->rawdev.curr_pos_w > state->rawdev.end_pos)
			state->rawdev.curr_pos_w = state->rawdev.curr_pos_w - state->rawdev.end_pos + state->rawdev.start_pos;
		D0(fprintf(debug_file, "%d bytes written, curr_pos = %llu\n", l, state->rawdev.curr_pos_w));
	}

	return 0;
}

/**
 * @brief Finish JPEG file write operation
 *
 * This function checks whether raw device write was on and closes raw device file.
 * @param   state   a pointer to a structure containing current state
 * @return  0 if the device was closed successfully and -1 otherwise
 */
int camogm_end_jpeg(camogm_state *state)
{
	int ret = 0;
	if (state->rawdev_op) {
		ret = close(state->rawdev.rawdev_fd);
		D0(fprintf(debug_file, "Closing raw device %s\n", state->rawdev.rawdev_path));
	}
	return ret;
}
