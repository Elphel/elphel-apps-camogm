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
#include "camogm_read.h"
#include "camogm_align.h"

/** State file record format. It includes device path in /dev, starting, current and ending LBAs */
#define STATE_FILE_FORMAT         "%s\t%llu\t%llu\t%llu\n"

/* forward declarations */
static void *jpeg_writer(void *thread_args);
static int save_state_file(const rawdev_buffer *rawdev, uint64_t current_pos);
static int open_state_file(const rawdev_buffer *rawdev, uint64_t *current_pos);

/** Get starting and endign LBAs of the partition specified as raw device buffer */
static int get_disk_range(struct range *range)
{
	FILE *f;

	// get raw device buffer starting postion on disk
	f = fopen(SYSFS_AHCI_LBA_START, "r");
	if (f == NULL) {
		return -1;
	}
	fscanf(f, "%llu\n", &range->from);
	fclose(f);

	// get raw device buffer ending postion on disk
	f = fopen(SYSFS_AHCI_LBA_END, "r");
	if (f == NULL) {
		return -1;
	}
	fscanf(f, "%llu\n", &range->to);
	fclose(f);

	return 0;
}

/** Get write pointer from a file. This functions check not only the name of a partition, but
 * its geometry as well */
static int find_state(FILE *f, uint64_t *pos, const rawdev_buffer *rawdev)
{
	size_t len;
	uint64_t start_pos, curr_pos, end_pos;
	struct range range;
	char buff[ELPHEL_PATH_MAX];
	char dev_name[ELPHEL_PATH_MAX];

	if (f == NULL || pos == NULL)
		return -1;
	if (get_disk_range(&range) != 0) {
		return -1;
	}

	// skip first line containing file header
	fgets(buff, ELPHEL_PATH_MAX, f);
	while (fgets(buff, ELPHEL_PATH_MAX, f) != NULL) {
		sscanf(buff, STATE_FILE_FORMAT, dev_name, &start_pos, &curr_pos, &end_pos);
		len = strlen(dev_name);
		if (strncmp(rawdev->rawdev_path, dev_name, len) == 0 &&
				range.from == start_pos &&
				range.to == end_pos) {
			*pos = curr_pos;
			break;
		}
	}

	return 0;
}

/** Read state from file and restore disk write pointer */
static int open_state_file(const rawdev_buffer *rawdev, uint64_t *current_pos)
{
	int fd, len;
	FILE *f;
	int ret = 0;
	uint64_t lba_pos;
	char buff[SMALL_BUFF_LEN] = {0};

	if (strlen(rawdev->state_path) == 0) {
		return ret;
	}

	f = fopen(rawdev->state_path, "r");
	if (f != NULL) {
		if (find_state(f, &lba_pos, rawdev) != -1) {
			*current_pos = lba_pos;
			D0(fprintf(debug_file, "Got starting LBA from state file: %llu\n", lba_pos));
		}
		fclose(f);
	} else {
		ret = -1;
	}

	return ret;
}

/** Save current position of the disk write pointer */
static int save_state_file(const rawdev_buffer *rawdev, uint64_t current_pos)
{
	int ret = 0;
	FILE *f;
	struct range range;

	if (strlen(rawdev->state_path) == 0) {
		return ret;
	}
	if (get_disk_range(&range) != 0) {
		return -1;
	}

	// save pointers to a regular file
	f = fopen(rawdev->state_path, "w");
	if (f == NULL) {
		return -1;
	}
	fprintf(f, "Device\t\tStart LBA\tCurrent LBA\tEnd LBA\n");
	fprintf(f, STATE_FILE_FORMAT, rawdev->rawdev_path, range.from, current_pos, range.to);
	fflush(f);
	fsync(fileno(f));
	fclose(f);

	return ret;
}

/**
 * @brief Initialize synchronization resources for disk writing thread and then start this thread. This function
 * is call each time JPEG format is set or changed, thus we need to check the state of writing thread before
 * initialization to prevent spawning multiple threads.
 * @param[in]   state   a pointer to a structure containing current state
 * @return      0 if initialization was successful and negative value otherwise
 */
int camogm_init_jpeg(camogm_state *state)
{
	int ret = 0;
	int ret_val;

	if (state->writer_params.state == STATE_STOPPED) {
		ret_val = pthread_cond_init(&state->writer_params.main_cond, NULL);
		if (ret_val != 0) {
			D0(fprintf(debug_file, "Can not initialize conditional variable for main thread: %s\n", strerror(ret_val)));
			ret = -1;
		}
		ret_val = pthread_cond_init(&state->writer_params.writer_cond, NULL);
		if (ret_val != 0) {
			D0(fprintf(debug_file, "Can not initialize conditional variable for writing thread: %s\n", strerror(ret_val)));
			ret = -1;
		}
		ret_val = pthread_mutex_init(&state->writer_params.writer_mutex, NULL);
		if (ret_val != 0) {
			D0(fprintf(debug_file, "Can not initialize mutex for writing thread: %s\n", strerror(ret_val)));
			ret = -1;
		}
		ret_val = pthread_create(&state->writer_params.writer_thread, NULL, jpeg_writer, (void *)state);
		if (ret_val != 0) {
			D0(fprintf(debug_file, "Can not start writer thread: %s\n", strerror(ret_val)));
			ret = -1;
		}

		ret_val = init_align_buffers(state);
		if (ret_val != 0) {
			D0(fprintf(debug_file, "Can not initialize alignment buffers\n"));
			ret = -1;
		}
	}

	return ret;
}

/**
 * @brief Stop disk writing thread and free its resources. This function is called after the program receives 'exit' command.
 * @param[in]   state   a pointer to a structure containing current state
 * @return      None
 */
void camogm_free_jpeg(camogm_state *state)
{
	pthread_cond_destroy(&state->writer_params.main_cond);
	pthread_cond_destroy(&state->writer_params.writer_cond);
	pthread_mutex_destroy(&state->writer_params.writer_mutex);

	// terminate writing thread
	pthread_mutex_lock(&state->writer_params.writer_mutex);
	state->writer_params.exit_thread = true;
	pthread_cond_signal(&state->writer_params.writer_cond);
	pthread_mutex_unlock(&state->writer_params.writer_mutex);
	pthread_join(state->writer_params.writer_thread, NULL);
	state->writer_params.exit_thread = false;

	deinit_align_buffers(state);
}

/** Calculate the total length of current frame */
int64_t camogm_get_jpeg_size(camogm_state *state)
{
	int64_t len = 0;

	for (int i = 0; i < state->chunk_index - 1; i++) {
		len += state->packetchunks[i + 1].bytes;
	}

	return len;
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
	off64_t offset;

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
		if (open_state_file(&state->rawdev, &state->writer_params.lba_current) != 0) {
			D0(fprintf(debug_file, "Could not get write pointer from state file, recording will start from the beginning of partition: "
					"%s\n", state->rawdev.rawdev_path));
		}
		state->writer_params.blockdev_fd = open(state->rawdev.rawdev_path, O_WRONLY);
		if (state->writer_params.blockdev_fd < 0) {
			D0(fprintf(debug_file, "Error opening block device: %s\n", state->rawdev.rawdev_path));
			return -CAMOGM_FRAME_FILE_ERR;
		}
		offset = lba_to_offset(state->writer_params.lba_current - state->writer_params.lba_start);
		lseek64(state->writer_params.blockdev_fd, offset, SEEK_SET);
		D6(fprintf(debug_file, "Open block device: %s, offset in bytes: %llu\n", state->rawdev.rawdev_path, offset));
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

	sprintf(state->path, "%s%d_%010ld_%06ld.jpeg", state->path_prefix, port, state->this_frame_params[port].timestamp_sec, state->this_frame_params[port].timestamp_usec);
	if (!state->rawdev_op) {
		l = 0;
		for (i = 0; i < (state->chunk_index) - 1; i++) {
			chunks_iovec[i].iov_base = state->packetchunks[i + 1].chunk;
			chunks_iovec[i].iov_len = state->packetchunks[i + 1].bytes;
			l += chunks_iovec[i].iov_len;
		}
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
		state->rawdev.last_jpeg_size = l;
		close(state->ivf);
	} else {
		D6(fprintf(debug_file, "\ndump iovect array for port %u\n", state->port_num));
		for (int i = 0; i < state->chunk_index - 1; i++) {
			D6(fprintf(debug_file, "ptr: %p, length: %ld\n", state->packetchunks[i + 1].chunk, state->packetchunks[i + 1].bytes));
		}
		// next frame is ready for recording, signal this to the writer thread
		pthread_mutex_lock(&state->writer_params.writer_mutex);
		while (state->writer_params.data_ready)
			pthread_cond_wait(&state->writer_params.main_cond, &state->writer_params.writer_mutex);
		D6(fprintf(debug_file, "_13a_"));

		D6(fprintf(debug_file, "\n"));
		align_frame(state);
		if (update_lba(state) == 1) {
			D0(fprintf(debug_file, "The end of block device reached, continue recording from start\n"));
			lseek(state->writer_params.blockdev_fd, 0, SEEK_SET);
		}
		D6(fprintf(debug_file, "Block device positions: start = %llu, current = %llu, end = %llu\n",
				state->writer_params.lba_start, state->writer_params.lba_current, state->writer_params.lba_end));

		// proceed if last frame was recorded without errors
		if (state->writer_params.last_ret_val == 0) {
			state->writer_params.data_ready = true;
			pthread_cond_signal(&state->writer_params.writer_cond);
		}
		pthread_mutex_unlock(&state->writer_params.writer_mutex);
		if (state->writer_params.last_ret_val != 0) {
			return state->writer_params.last_ret_val;
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
	int bytes;
	ssize_t iovlen;
	struct frame_data fdata = {0};

	if (state->rawdev_op) {
		// write any remaining data, do not use writer thread as there can be only one block left CHUNK_REM buffer
		pthread_mutex_lock(&state->writer_params.writer_mutex);
		bytes = prep_last_block(state);
		if (bytes > 0) {
			D6(fprintf(debug_file, "Write last block of data, size = %d\n", bytes));
			// the remaining data block is placed in CHUNK_COMMON buffer, write just this buffer
			iovlen = writev(state->writer_params.blockdev_fd, &state->writer_params.data_chunks[CHUNK_COMMON], 1);
			if (iovlen < bytes) {
				D0(fprintf(debug_file, "writev error: %s (returned %i, expected %i)\n", strerror(errno), iovlen, bytes));
				state->writer_params.last_ret_val = -CAMOGM_FRAME_FILE_ERR;
			} else {
				// update statistic, just one block written
				state->writer_params.lba_current += 1;
				state->rawdev.total_rec_len += bytes;
			}
			reset_chunks(state->writer_params.data_chunks, 1);
		}
		pthread_mutex_unlock(&state->writer_params.writer_mutex);

		D6(fprintf(debug_file, "Closing block device %s\n", state->rawdev.rawdev_path));
		ret = close(state->writer_params.blockdev_fd);
		if (ret == -1)
			D0(fprintf(debug_file, "Error: %s\n", strerror(errno)));

		save_state_file(&state->rawdev, state->writer_params.lba_current);
	}
	return ret;
}

/**
 * @brief Disk writing thread. This thread holds local copy of a structure containing current state
 * of the program and updates it on a signal from main thread every time new frame is ready for recording.
 * @param[in]   thread_args   a pointer to a structure containing current state
 * @return      None
 */
void *jpeg_writer(void *thread_args)
{
	int rslt = 0;
	int chunk_index;
	ssize_t iovlen, l;
	bool process = true;
	struct iovec chunks_iovec[FILE_CHUNKS_NUM];
	camogm_state *state = (camogm_state *)thread_args;
	struct writer_params *params = &state->writer_params;

	memset((void *)chunks_iovec, 0, sizeof(struct iovec) * FILE_CHUNKS_NUM);
	pthread_mutex_lock(&params->writer_mutex);
	params->state = STATE_RUNNING;
	while (process) {
		while (!params->data_ready && !params->exit_thread) {
			pthread_cond_wait(&params->writer_cond, &params->writer_mutex);
		}
		if (params->exit_thread) {
			process = false;
		}
		if (params->data_ready) {
			l = 0;
			state->writer_params.last_ret_val = 0;
			chunk_index = get_data_buffers(state, &chunks_iovec, FILE_CHUNKS_NUM);
			if (chunk_index > 0) {
				for (int i = 0; i < chunk_index; i++)
					l += chunks_iovec[i].iov_len;
				iovlen = writev(state->writer_params.blockdev_fd, chunks_iovec, chunk_index);
				if (iovlen < l) {
					D0(fprintf(debug_file, "writev error: %s (returned %i, expected %i)\n", strerror(errno), iovlen, l));
					state->writer_params.last_ret_val = -CAMOGM_FRAME_FILE_ERR;
				} else {
					// update statistic
					state->rawdev.last_jpeg_size = l;
					state->rawdev.total_rec_len += state->rawdev.last_jpeg_size;
					D6(fprintf(debug_file, "Current position in block device: %lld\n", lseek64(state->writer_params.blockdev_fd, 0, SEEK_CUR)));
				}
			} else {
				D0(fprintf(debug_file, "data vector mapping error: %d)\n", chunk_index));
				state->writer_params.last_ret_val = -CAMOGM_FRAME_FILE_ERR;
			}

			// release main thread
			reset_chunks(state->writer_params.data_chunks, 0);
			params->data_ready = false;
			pthread_cond_signal(&params->main_cond);
		}
	}
	params->state = STATE_STOPPED;
	pthread_mutex_unlock(&state->writer_params.writer_mutex);
	D5(fprintf(debug_file, "Exit from recording thread\n"));

	return NULL;
}
