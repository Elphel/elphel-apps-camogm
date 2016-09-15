/** @file camogm_jpeg.c
 * @brief Provides writing to series of individual JPEG files for @e camogm
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
#include <elphel/c313a.h>
#include <elphel/ahci_cmd.h>

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
		strcpy(state->path, state->path_prefix);   // make state->path a directory name (will be replaced when the frames will be written)
		slash = strrchr(state->path, '/');
		D2(fprintf(debug_file, "camogm_start_jpeg\n"));
		if (slash) {
			D3(fprintf(debug_file, "Full path %s\n", state->path));
			slash[0] = '\0';                       // truncate path to the directory name
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
			state->rawdev.sysfs_fd = open(SYSFS_AHCI_WRITE, O_WRONLY);
			fprintf(debug_file, "Open sysfs file: %s\n", SYSFS_AHCI_WRITE);
			if (state->rawdev.sysfs_fd < 0) {
				D0(fprintf(debug_file, "Error opening sysfs file: %s\n", SYSFS_AHCI_WRITE));
				return -CAMOGM_FRAME_FILE_ERR;
			}
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
	int i, j;
	ssize_t iovlen, l = 0;
	struct iovec chunks_iovec[8];
	int port = state->port_num;
	struct frame_data fdata = {0};

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
		fprintf(debug_file, "dump iovect array\n");
		for (int i = 0; i < state->chunk_index - 1; i++) {
			fprintf(debug_file, "ptr: %p, length: %ld\n", state->packetchunks[i + 1].chunk, state->packetchunks[i + 1].bytes);
		}
		fdata.sensor_port = port;
		fdata.cirbuf_ptr = state->cirbuf_rp[port];
		fdata.jpeg_len = state->jpeg_len;
		if (state->exif) {
			fdata.meta_index = state->this_frame_params[port].meta_index;
			fdata.cmd |= DRV_CMD_EXIF;
		}
		fdata.cmd |= DRV_CMD_WRITE;
		if (write(state->rawdev.sysfs_fd, &fdata, sizeof(struct frame_data)) < 0) {
			D0(fprintf(debug_file, "Can not pass IO vector to driver: %s\n", strerror(errno)));
			return -CAMOGM_FRAME_FILE_ERR;
		}
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
	struct frame_data fdata = {0};

	if (state->rawdev_op) {
		fdata.cmd = DRV_CMD_FINISH;
		if (write(state->rawdev.sysfs_fd, &fdata, sizeof(struct frame_data)) < 0) {
			D0(fprintf(debug_file, "Error sending 'finish' command to driver\n"));
		}
		D0(fprintf(debug_file, "Closing sysfs file %s\n", SYSFS_AHCI_WRITE));
		ret = close(state->rawdev.sysfs_fd);
		if (ret == -1)
			D0(fprintf(debug_file, "Error: %s\n", strerror(errno)));
	}
	return ret;
}
