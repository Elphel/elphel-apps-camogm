/** @file camogm_read.c
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

/** @brief This define is needed to used lseek64 and should be set before includes */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <linux/limits.h>
#include <netinet/in.h>

#include "camogm.h"

/** @brief The size of read buffer in bytes. The data will be read from disk in blocks of this size */
#define PHY_BLK_SZ                4096
/** @brief Include or exclude file start and stop markers from resulting file. This must be set to 1 for JPEG files */
#define INCLUDE_MARKERS           1
/** @brief File starting marker on a raw device. It corresponds to SOI JPEG marker */
static const unsigned char elphelst[] = {0xff, 0xd8};
/** @brief File ending marker on a raw device. It corresponds to EOI JPEG marker */
static const unsigned char elphelen[] = {0xff, 0xd9};
static const struct iovec elphel_st = {
		.iov_base = elphelst,
		.iov_len = sizeof(elphelst)
};
static const struct iovec elphel_en = {
		.iov_base = elphelen,
		.iov_len = sizeof(elphelen)
};

/**
 * @enum file_result
 * @brief Return codes for file operations
 * @var file_result::FILE_OK
 * File operation has finished successfully
 * @var file_result::FILE_WR_ERR
 * An error occurred during file write
 * @var file_result::FILE_RD_ERR
 * An error occurred during file read
 * @var file_result::FILE_OPEN_ERR
 * Unable to open file
 * @var file_result::FILE_CLOSE_ERR
 * An error occurred during file close
 * @var file_result::FILE_OPT_ERR
 * Other file access errors
 */
enum file_result {
	FILE_OK             = 0,
	FILE_WR_ERR         = -1,
	FILE_RD_ERR         = -2,
	FILE_OPEN_ERR       = -3,
	FILE_CLOSE_ERR      = -4,
	FILE_OPT_ERR        = -5
};

/**
 * @enum match_result
 * @brief The result codes for file markers search function
 * @var match_result::MATCH_FOUND
 * File marker was found in the current data buffer
 * @var match_result::MATCH_NOT_FOUND
 * File marker was not found in the current data buffer
 * @var match_result::MATCH_PARTIAL
 * Only a fraction of a marker was found in the end of the current data buffer
 */
enum match_result {
	MATCH_FOUND         = -1,
	MATCH_NOT_FOUND     = -2,
	MATCH_PARTIAL       = -3
};

/**
 * @enum file_ops
 * @brief The states of file operations
 * @var file_ops::WRITE_START
 * Start writing data to a new file
 * @var file_ops::WRITE_RUNNING
 * Current data chunk will be written to already opened file
 * @var file_ops::WRITE_STOP
 * Write current data chunk to a file and close it
 */
enum file_ops {
	WRITE_START,
	WRITE_RUNNING,
	WRITE_STOP
};

/**
 * @struct file_opts
 * @brief This structure holds data associated with currently opened file.
 * @var file_opts::fh
 * FILE pointer
 * @var file_opts::path_prefix
 * Contains path to currently opened file
 * @var file_opts::file_name
 * Contains full path to file currently opened file
 * @var file_opts::file_cntr
 * Indicates the number of files read from raw storage device
 * @var file_opts::file_state
 * Contains the state of current file operation which can be one from #file_ops enum
 */
struct file_opts {
	FILE                *fh;
	char                path_prefix[NAME_MAX];
	char                file_name[PATH_MAX];
	unsigned int        file_cntr;
	int                 file_state;
};

/**
 * @struct dev_opts
 * @brief This structure holds data associated with raw storage device.
 * @var dev_opts::dev_name
 * Contains full path to raw storage device
 * @var dev_opts::verbose
 * Indicates vebosity level, just a flag as for now
 * @var dev_opts::fd
 * Contains file descriptor
 * @var dev_opts::start_pos
 * Contains start pointer for raw device storage, used when starting position does not coincide with
 * the beginning of device file
 * @var dev_opts::end_pos
 * Contains end pointer for raw device storage, used when end position does not coincide with
 * the end of device file
 * @var dev_opts::curr_pos
 * Contains current read pointer for raw device storage
 */
struct dev_opts {
	char                dev_name[PATH_MAX];
	int                 verbose;
	int                 fd;
	uint64_t            start_pos;
	uint64_t            end_pos;
	uint64_t            curr_pos;
};

/**
 * @struct crb_ptrs
 * @brief A set of vectors pointing to a file marker crossing read buffer boundary.
 * @var crb_ptrs::first_buff
 * Points to the end of first read buffer where a file marker starts
 * @var crb_ptrs::second_buff
 * Points to the start of second read buffer where a file marker ends
 */
struct crb_ptrs {
	struct iovec        first_buff;
	struct iovec        second_buff;
};

void print_array(const struct iovec *buff)
{
	const int spl = 8;
	unsigned char *a = buff->iov_base;

	for (int i = 0; i < buff->iov_len; i++) {
		if (i % spl == 0)
			printf("\n");
		printf("0x%02x ", a[i]);
	}
	printf("\n");
}

/**
 * @brief Find pattern in a data buffer
 *
 * This function searches for the first occurrence of pattern in a data buffer and returns a pointer to
 * the position of this pattern in the buffer.
 * @param[in]   buff_ptr  pointer to an array of char values where the pattern should be found
 * @param[in]   buff_sz   size of the data array
 * @param[in]   pattern   pointer to an array of char values containing pattern
 * @param[in]   pt_sz     size of the pattern array
 * @return      the index in data buffer where pattern matches or error code from #match_result if it was not found
 */
int find_marker(const unsigned char * restrict buff_ptr, ssize_t buff_sz, const unsigned char * restrict pattern, ssize_t pt_sz)
{
	int ret = MATCH_NOT_FOUND;
	int j = 0;
	int i;

	for (i = 0; i < buff_sz; i++) {
		if (buff_ptr[i] != pattern[j]) {
			// current symbol in data buffer and first symbol of pattern does not match
			j = 0;
		} else if (buff_ptr[i] == pattern[j] && j < pt_sz - 1) {
			// pattern symbol match
			j++;
		} else if (buff_ptr[i] == pattern[j] && j == pt_sz - 1) {
			// last pattern symbol match
			ret = i - j;
			j = 0;
			break;
		}
	}
	if (j > 0) {
		// partial match found in the end of data buffer, we need more data for further comparison
		ret = MATCH_PARTIAL;
		fprintf(stderr, "Match partial; j = %d, loop conter = %d\n", j, i);
	}
	return ret;
}

/**
 * @brief Create new file name string and open a file
 * @param[in]   f_op   pointer to a structure holding information about currently opened file
 * @return      \e FILE_OK if file was successfully opened and \e FILE_OPEN_ERR otherwise
 * @todo retrieve time stamp and use it in file name
 */
int start_new_file(struct file_opts *f_op)
{
	int ret;
	static int name_ind = 1;

	if (f_op->path_prefix[strlen(f_op->path_prefix) - 1] == '/')
		ret = sprintf(f_op->file_name, "%s%d.jpeg", f_op->path_prefix, name_ind);
	else
		ret = sprintf(f_op->file_name, "%s%c%d.jpeg", f_op->path_prefix, '/', name_ind);
	name_ind++;

	if (ret > 0) {
		f_op->fh = fopen(f_op->file_name, "w");
		if (f_op->fh == NULL)
			ret = FILE_OPEN_ERR;
		else
			ret = FILE_OK;
	} else {
		ret = FILE_OPEN_ERR;
	}
	return ret;
}

/**
 * @brief Detect cases when file marker crosses read buffer boundary
 * @param[in]   from   pointer to current read buffer which can hold the beginning of
 * file marker
 * @param[in]   to     pointer to a buffer containing next chunk of data which can hold
 * the end of file marker
 * @param[in]   marker pointer to a buffer holding file marker to be detect
 * @param[in]   crbp   pointer to a structure which will store two cross border pointers
 * @return      a constant of #match_result type
 */
int check_edge_case(const struct iovec *from, const struct iovec *to, const struct iovec *marker, struct crb_ptrs *crbp)
{
	unsigned char *start_ptr = from->iov_base + from->iov_len - marker->iov_len;
	unsigned char *end_ptr = from->iov_base + from->iov_len;
	unsigned char *marker_ptr = marker->iov_base;
	unsigned int bytes_processed = 0;
	int match = 0;

	// search for the first part of marker in the end of *from* array
	while (start_ptr <= end_ptr) {
		if (*start_ptr == *marker_ptr && !match) {
			crbp->first_buff.iov_base = start_ptr;
			crbp->first_buff.iov_len = end_ptr - start_ptr;
			match = 1;
		}
		if (*start_ptr == *marker_ptr && match) {
			marker_ptr++;
			bytes_processed++;
		} else {
			break;
		}
		start_ptr++;
	}
	if (start_ptr != end_ptr) {
		// match not found in the end of *from* array
		return MATCH_NOT_FOUND;
	}

	fprintf(stderr, "First array checked, %d bytes of marker processed\n", bytes_processed);

	// search for the second part of marker in the beginning of *to* array
	start_ptr = to->iov_base;
	end_ptr = to->iov_base + (marker->iov_len - bytes_processed);
	while (start_ptr <= end_ptr) {
		if (*start_ptr++ != *marker_ptr++)
			break;
	}
	if (start_ptr != end_ptr) {
		// match not found in the beginning of *to* array
		return MATCH_NOT_FOUND;
	}
	crbp->second_buff.iov_base = to->iov_base;
	crbp->second_buff.iov_len = marker->iov_len - bytes_processed;

	fprintf(stderr, "Second array checked, match found\n");

	return MATCH_FOUND;
}

/**
 * @brief Perform file operation in accordance with current file state.
 * @param[in]   f_op   pointer to a structure holding information about currently opened file
 * @param[in]   from   start pointer to data buffer
 * @param[in]   to     end pointer to data buffer
 * @return      a constant of #file_result type
 */
int write_buffer(struct file_opts *f_op, unsigned char *from, unsigned char *to)
{
	int ret = FILE_OK;
	int len;
	unsigned int sz;

	sz = to - from;
	fprintf(stderr, "%s: sz = %d, file state = %d\n", __func__, sz, f_op->file_state);
	switch (f_op->file_state) {
	case WRITE_RUNNING:
		len = fwrite(from, sz, 1, f_op->fh);
		if (len != 1) {
			perror(__func__);
			ret = FILE_WR_ERR;
		}
		break;
	case WRITE_START:
		if (start_new_file(f_op) == FILE_OK) {
			len = fwrite(from, sz, 1, f_op->fh);
			fprintf(stderr, "Starting new file %s\n", f_op->file_name);
			if (len != 1) {
				perror(__func__);
				ret = FILE_WR_ERR;
			}
		} else {
			f_op->fh = NULL;
			ret = FILE_OPEN_ERR;
		}
		break;
	case WRITE_STOP:
		len = fwrite(from, sz, 1, f_op->fh);
		if (len != 1) {
			perror(__func__);
			ret = FILE_WR_ERR;
		}
		if (fclose(f_op->fh) != 0) {
			perror(__func__);
			ret = FILE_CLOSE_ERR;
		} else {
			f_op->fh = NULL;
			f_op->file_cntr++;
		}
		break;
	default:
		ret = FILE_OPT_ERR;
	}
	return ret;
}

int camogm_read(camogm_state *state)
{
	const int include_markers = INCLUDE_MARKERS;
	int process;
	int zero_cross;
	int file_op;
	ssize_t rd;
	int pos_start, pos_stop;
	int buff_processed;
	unsigned char buff[PHY_BLK_SZ];
	unsigned char next_buff[PHY_BLK_SZ];
	struct dev_opts d_opts;
	struct file_opts f_opts;
	unsigned char *save_from;
	unsigned char *save_to;
	uint64_t include_st_marker, include_en_marker;

	d_opts.curr_pos = 0;
	d_opts.verbose = 0;
	f_opts.fh = NULL;
	f_opts.file_cntr = 0;
	f_opts.file_state = WRITE_STOP;
	if (parse_cmd_opts(argc, argv, &f_opts, &d_opts) < 0) {
		print_help();
		return EXIT_FAILURE;
	}

	d_opts.fd = open(d_opts.dev_name, O_RDONLY);
	if (d_opts.fd < 0) {
		perror("Can not open device: ");
		return EXIT_FAILURE;
	}

	if (d_opts.verbose) {
		printf("Open block device %s\n", d_opts.dev_name);
		printf("Start reading from %lu to %lu\n", d_opts.start_pos, d_opts.end_pos);
	}

	if (include_markers) {
		include_st_marker = elphel_st.iov_len;
		include_en_marker = elphel_en.iov_len;
	} else {
		include_st_marker = 0;
		include_en_marker = 0;
	}
	process = 1;
	zero_cross = 0;
	file_op = FILE_OK;
	while (process) {
		rd = read(d_opts.fd, buff, sizeof(buff));
		fprintf(stderr, "read %ld bytes from %lu to %lu\n", rd, d_opts.curr_pos, d_opts.curr_pos + rd);
		if ((rd > 0) && (d_opts.curr_pos + rd > d_opts.end_pos)) {
			// read pointer jumped over the raw storage buffer end, truncate excessive data
			if (d_opts.verbose) {
				fprintf(stderr, "End of raw storage buffer is reached, will start from beginning\n");
				fprintf(stderr, "\tstart_pos = %lu, curr_pos = %lu, end_pos = %lu\n", d_opts.start_pos, d_opts.curr_pos, d_opts.end_pos);
			}
			rd = d_opts.end_pos - d_opts.curr_pos;
			zero_cross = 1;
			lseek64(d_opts.fd, d_opts.start_pos, SEEK_SET);
			d_opts.curr_pos = d_opts.start_pos;
			if (rd == 0) {
				continue;
			}
		} else if (rd < 0) {
			// read error or read pointer exceeded raw storage capacity, close files and terminate
			process = 0;
			if (d_opts.verbose) {
				fprintf(stderr, "read() was unsuccessful:\n");
				fprintf(stderr, "\tstart_pos = %lu, curr_pos = %lu, end_pos = %lu\n", d_opts.start_pos, d_opts.curr_pos, d_opts.end_pos);
			}
		} else if (rd == 0) {
			// end of device file reached
			if (d_opts.verbose) {
				fprintf(stderr, "End of raw storage device file is reached, will start from beginning\n");
			}
			zero_cross = 1;
			lseek64(d_opts.fd, d_opts.start_pos, SEEK_SET);
			d_opts.curr_pos = d_opts.start_pos;
		}
		if (process) {
			save_from = buff;
			save_to = buff + rd;
			buff_processed = 0;
			do {
				// process data in read buffer
				fprintf(stderr, "Searching start marker from %p to %p\n", save_from, save_to);
				pos_start = find_marker(save_from, save_to - save_from, elphel_st.iov_base, elphel_st.iov_len);
				fprintf(stderr, "\tmarker position = %d\n", pos_start);

				fprintf(stderr, "Searching stop marker from %p to %p\n", save_from, save_to);
				pos_stop = find_marker(save_from, save_to - save_from, elphel_en.iov_base, elphel_en.iov_len);
				fprintf(stderr, "\tmarker position = %d\n", pos_stop);

				if (pos_start == MATCH_NOT_FOUND && pos_stop == MATCH_NOT_FOUND &&
						(f_opts.file_state == WRITE_RUNNING || f_opts.file_state == WRITE_START)) {
					// writing in progress, got full buffer of new data
					f_opts.file_state = WRITE_RUNNING;
					file_op = write_buffer(&f_opts, save_from, save_to);
					buff_processed = 1;
					fprintf(stderr, "State 'writing'\n");
				} else if (pos_start >= 0 /*&& pos_stop == MATCH_NOT_FOUND */ &&
						f_opts.file_state == WRITE_STOP) {
					// not writing, start marker found - start writing
					f_opts.file_state = WRITE_START;
					save_from = save_from + pos_start + elphel_st.iov_len;
					file_op = write_buffer(&f_opts, save_from - include_st_marker, save_to);
					buff_processed = 1;
					f_opts.file_state = WRITE_RUNNING;
					fprintf(stderr, "State 'starting file', file_op = %d\n", file_op);
				} else if (pos_start == MATCH_NOT_FOUND && pos_stop >= 0 &&
						(f_opts.file_state == WRITE_RUNNING || f_opts.file_state == WRITE_START)) {
					// writing in progress, end marker found - stop writing
					f_opts.file_state = WRITE_STOP;
					save_to = save_from + pos_stop;
					file_op = write_buffer(&f_opts, save_from, save_to + include_en_marker);
					buff_processed = 1;
					if (zero_cross)
						process = 0;
					fprintf(stderr, "State 'finishing file'\n");
				} else if (pos_start >= 0 && pos_stop >= 0 && pos_start > pos_stop &&
						f_opts.file_state == WRITE_RUNNING) {
					// writing in progress, start marker following stop marker found - this indicates a new file
					f_opts.file_state = WRITE_STOP;
					save_to = save_from + pos_stop;
					file_op = write_buffer(&f_opts, save_from, save_to + include_en_marker);
					save_from = save_from + pos_start;
					save_to = buff + rd;
					if (zero_cross) {
						buff_processed = 1;
						process = 0;
					}
					fprintf(stderr, "State 'starting new file'\n");
				} else if (pos_start == MATCH_PARTIAL &&
						f_opts.file_state == WRITE_STOP) {
					// partial start marker found in the end of read buffer - get next chunk of data and try to find marker there
					enum match_result result;
					struct crb_ptrs field_markers;
					struct iovec next_chunk = {
							.iov_base = next_buff,
					};
					struct iovec curr_chunk = {
							.iov_base = save_from,
							.iov_len = save_to - save_from
					};
					ssize_t next_rd = read(d_opts.fd, next_buff, sizeof(next_buff));
					fprintf(stderr, "process MATCH_PARTIAL pos_start: read %ld\n", next_rd);
					next_chunk.iov_len = next_rd;
					d_opts.curr_pos += next_rd;
					result = check_edge_case(&curr_chunk, &next_chunk, &elphel_st, &field_markers);
					fprintf(stderr, "process MATCH_PARTIAL: check edge case, result = %d\n", result);
					if (result == MATCH_FOUND  && f_opts.file_state == WRITE_STOP) {
						fprintf(stderr, "process MATCH_PARTIAL: match found\n");
						f_opts.file_state = WRITE_START;
						if (include_markers) {
							unsigned char *from = field_markers.first_buff.iov_base;
							unsigned char *to = from + field_markers.first_buff.iov_len;
							file_op = write_buffer(&f_opts, from, to);
							from = field_markers.second_buff.iov_base;
							to = from + field_markers.second_buff.iov_len;
							file_op = write_buffer(&f_opts, from, to);
						}
						save_from = next_chunk.iov_base + field_markers.second_buff.iov_len;
						save_to = next_chunk.iov_base + next_chunk.iov_len;
					} else {
						fprintf(stderr, "process MATCH_PARTIAL: match not found\n");
//						file_op = write_buffer(&f_opts, save_from, save_to);
						save_from = next_chunk.iov_base;
						save_to = next_chunk.iov_base + next_chunk.iov_len;
					}
					fprintf(stderr, "State 'check elphel_st cross boundary'; result = %d\n", result);
				} else if (pos_stop == MATCH_PARTIAL &&
						f_opts.file_state != WRITE_STOP) {
					// partial end marker found in the end of read buffer - get next chunk of data and try to find marker there
					enum match_result result;
					struct crb_ptrs field_markers;
					struct iovec next_chunk = {
							.iov_base = next_buff,
					};
					struct iovec curr_chunk = {
							.iov_base = save_from,
							.iov_len = save_to - save_from
					};
					ssize_t next_rd = read(d_opts.fd, next_buff, sizeof(next_buff));
					fprintf(stderr, "process MATCH_PARTIAL pos_stop: read %ld\n", next_rd);
					next_chunk.iov_len = next_rd;
					d_opts.curr_pos += next_rd;
					result = check_edge_case(&curr_chunk, &next_chunk, &elphel_en, &field_markers);
					fprintf(stderr, "process MATCH_PARTIAL: check edge case, result = %d\n", result);
					if (result == MATCH_FOUND) {
						fprintf(stderr, "process MATCH_PARTIAL: match found\n");
						f_opts.file_state = WRITE_STOP;
						file_op = write_buffer(&f_opts, save_from, save_to);
						if (include_markers) {
							unsigned char *from = field_markers.first_buff.iov_base;
							unsigned char *to = from + field_markers.first_buff.iov_len;
							file_op = write_buffer(&f_opts, from, to);
							from = field_markers.second_buff.iov_base;
							to = from + field_markers.second_buff.iov_len;
							file_op = write_buffer(&f_opts, from, to);
						}
						save_from = next_chunk.iov_base + field_markers.second_buff.iov_len;
						save_to = next_chunk.iov_base + next_chunk.iov_len;
					} else {
						fprintf(stderr, "process MATCH_PARTIAL: match not found\n");
						file_op = write_buffer(&f_opts, save_from, save_to);
						save_from = next_chunk.iov_base;
						save_to = next_chunk.iov_base + next_chunk.iov_len;
					}
					fprintf(stderr, "State 'check elphel_en' cross boundary:; result = %d\n", result);
				} else {
					// no markers found and new file has not bee started yet - skip data
					fprintf(stderr, "Undefined state: pos_start = %d, pos_stop = %d, file_state = %d\n",
							pos_start, pos_stop, f_opts.file_state);
					buff_processed = 1;
					if (zero_cross)
						process = 0;
				}
				fprintf(stderr, "buff_processed = %d, file_op = %d\n", buff_processed, file_op);
			} while (buff_processed == 0 && file_op == FILE_OK);
			if (file_op != FILE_OK) {
				process = 0;
			}
			d_opts.curr_pos += rd;
		}
	}

	fprintf(stderr, "\n%d files read from %s\n", f_opts.file_cntr, d_opts.dev_name);

	if (close(d_opts.fd) != 0) {
		perror("Unable to close raw device: ");
		return EXIT_FAILURE;
	}
	if (f_opts.fh != NULL && fclose(f_opts.fh) != 0) {
		perror("Unable to close data file: ");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
