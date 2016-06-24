/** @file camogm_read.c
 * @brief Provides reading data written to raw device storage and saving the data to a device with file system.
 * @copyright  Copyright (C) 2016 Elphel, Inc.
 *
 * @par <b>License</b>
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

/** @brief This define is needed to use lseek64 and should be set before includes */
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
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <asm/byteorder.h>
#include <sys/statvfs.h>

#include "camogm_read.h"

/** @brief Offset in Exif where TIFF header starts */
#define TIFF_HDR_OFFSET           12
/** @brief Separator character between seconds and microseconds in JPEG file name */
#define SUBSEC_SEPARATOR          '.'
/** @brief The size of read buffer in bytes. The data will be read from disk in blocks of this size */
#define PHY_BLK_SZ                4096
/** @brief Include or exclude file start and stop markers from resulting file. This must be set to 1 for JPEG files */
#define INCLUDE_MARKERS           1
/** @brief The amount of free disk space in bytes that should be left on the device during copying from raw device buffer to this disk */
#define FREE_SIZE_LIMIT           10485760
/** @brief File starting marker on a raw device. It corresponds to SOI JPEG marker */
static unsigned char elphelst[] = {0xff, 0xd8};
/** @brief File ending marker on a raw device. It corresponds to EOI JPEG marker */
static unsigned char elphelen[] = {0xff, 0xd9};
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
 * @brief Exif data format table.
 *
 * This table is used to convert Exif data format to its byte length and then calculate the length
 * of a record in Image File Directory. The data format value 0 does not exist thus the first
 * element of the array is 0 too which gives zero length field. Exif data format can one of the
 * following:
 * Value | Format            | Bytes/Components
 * ------|-------------------|-----------------
 *  1    | unsigned byte     | 1
 *  2    | ascii string      | 1
 *  3    | unsigned short    | 2
 *  4    | unsigned long     | 4
 *  5    | unsigned rational | 8
 *  6    | signed byte       | 1
 *  7    | undefined         | 1
 *  8    | signed short      | 2
 *  9    | signed long       | 4
 *  10   | signed rational   | 8
 *  11   | signed float      | 4
 *  12   | double float      | 8
 */
const unsigned char exif_data_fmt[] = {
		0, 1, 1, 2, 4, 8, 1,
		1, 2, 4, 8, 4, 8
};

/**
 * @struct ifd_entry
 * @brief Represents a single Image File Directory record
 * @var ifd_entry::tag
 * Exif tag number
 * @var ifd_entry::format
 * Data format in the record
 * @var ifd_entry::len
 * The number of components in the record
 * @var ifd_entry::offset
 * The data value or offset to the data
 */
struct ifd_entry {
	unsigned short tag;
	unsigned short format;
	unsigned long len;
	unsigned long offset;
};

/**
 * @struct tiff_hdr
 * @brief TIFF header structure
 * @var tiff_hdr::byte_order
 * Motorola (MM) or Inter (II) byte order
 * @var tiff_hdr::mark
 * Tag mark
 * @var tiff_hdr::offset
 * Offset to first IFD
 */
struct tiff_hdr {
	unsigned short byte_order;
	unsigned short mark;
	unsigned long offset;
};

/**
 * @struct file_opts
 * @brief This structure holds data associated with currently opened file.
 * @var file_opts::fh
 * FILE pointer
 * @var file_opts::file_cntr
 * Indicates the number of files read from raw storage device
 * @var file_opts::file_state
 * Contains the state of current file operation which can be one from #file_ops enum
 */
struct file_opts {
	FILE                *fh;
	unsigned int        file_cntr;
	int                 file_state;
	camogm_state        *state;
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
static int find_marker(const unsigned char * restrict buff_ptr, ssize_t buff_sz, const unsigned char * restrict pattern, ssize_t pt_sz)
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
	}
	return ret;
}

/**
 * @brief Converts byte order for all fields in #ifd_entry structure
 * @param[in,out] ifd   a pointer to a structure which should be converted
 * @return        None
 */
static void ifd_byte_order(struct ifd_entry *ifd)
{
	ifd->tag = __be16_to_cpu(ifd->tag);
	ifd->format = __be16_to_cpu(ifd->format);
	ifd->len = __be32_to_cpu(ifd->len);
	ifd->offset = __be32_to_cpu(ifd->offset);
	if (exif_data_fmt[ifd->format] == 2) {
		ifd->offset = (ifd->offset >> 16) & 0xffff;
	}
}

/**
 * @brief Convert byte order for all fields in #tiff_hdr structure
 * @param[in,out] hdr   a pointer to a structure which should be converted
 * @return        None
 */
static void hdr_byte_order(struct tiff_hdr *hdr)
{
	hdr->byte_order = __be16_to_cpu(hdr->byte_order);
	hdr->mark = __be16_to_cpu(hdr->mark);
	hdr->offset = __be32_to_cpu(hdr->offset);
}

/**
 * @brief Read a string from Exif for a given record. This function is intended for
 * reading date and time from Exif and it omits any non-digit symbols from resulting string.
 * Spaces and tabs are converted to underscores. Terminating null byte is not added to the resulting string.
 * @param[in]   state   a pointer to a structure containing current state
 * @param[in]   ifd     Exif image file directory record containing string offset
 * @param[out]  buff    buffer for the string
 * @return      The number of bytes placed to the read buffer
 */
static size_t exif_get_text(camogm_state *state, struct ifd_entry *ifd, char *buff)
{
	size_t j = 0;
	size_t sz = ifd->len * exif_data_fmt[ifd->format];
	uint64_t curr_pos = state->rawdev.file_start + TIFF_HDR_OFFSET + ifd->offset;
	char read_buff[MAX_EXIF_SIZE] = {0};

	lseek64(state->rawdev.rawdev_fd, curr_pos, SEEK_SET);
	read(state->rawdev.rawdev_fd, read_buff, sz);
	for (int i = 0; i < sz; i++) {
		if (isdigit(read_buff[i]))
			buff[j++] = read_buff[i];
		else if (isblank(read_buff[i]))
			buff[j++] = '_';
	}

	return j;
}

/**
 * @brief Create JPEG file name from Exif data
 *
 * This function reads Exif data from file, extracts PageNumber, DateTimeOriginal and
 * SubSecTimeOriginal fields and assembles a file name in the following format:
 *
 * NN_YYYYMMDD_HHMMSS.UUUUUU.jpeg
 *
 * where NN is PageNumber; YYYY, MM and DD are year, month and date extracted from DateTimeOriginal
 * field; HH, MM and SS are hours, minutes and seconds extracted from DateTimeOriginal field; UUUUUU is us
 * value extracted from SubSecTimeOriginal field. The function assumes that @e name buffer is big enough
 * to hold the file name in the format shown above including the terminating null byte.
 * @param[in]   state   a pointer to a structure containing current state
 * @param[out]  name    resulting file name
 * @return      0 if file name was successfully created and negative value otherwise
 */
static int make_fname(camogm_state *state, char *name)
{
	int process = 2;
	size_t sz = 0;
	uint32_t data32;
	uint16_t num_entries = 0;
	uint64_t curr_pos;
	uint64_t subifd_offset = 0;
	struct tiff_hdr hdr;
	struct ifd_entry ifd;
	struct ifd_entry ifd_page_num;
	struct ifd_entry ifd_date_time;
	struct ifd_entry ifd_subsec;
	unsigned char read_buff[TIFF_HDR_OFFSET] = {0};
	uint64_t save_pos = lseek64(state->rawdev.rawdev_fd, 0, SEEK_CUR);

	if (name == NULL)
		return -1;

	curr_pos = state->rawdev.file_start;
	lseek64(state->rawdev.rawdev_fd, curr_pos, SEEK_SET);
	if (read(state->rawdev.rawdev_fd, read_buff, sizeof(read_buff)) <= 0) {
		lseek64(state->rawdev.rawdev_fd, save_pos, SEEK_SET);
		return -1;
	}
	if (read_buff[2] == 0xff && read_buff[3] == 0xe1) {
		// get IFD0 offset from TIFF header
		read(state->rawdev.rawdev_fd, &hdr, sizeof(struct tiff_hdr));
		hdr_byte_order(&hdr);
		curr_pos = state->rawdev.file_start + TIFF_HDR_OFFSET + hdr.offset;
		lseek64(state->rawdev.rawdev_fd, curr_pos, SEEK_SET);
		// process IFD0 and SubIFD fields
		do {
			read(state->rawdev.rawdev_fd, &num_entries, sizeof(num_entries));
			num_entries = __be16_to_cpu(num_entries);
			for (int i = 0; i < num_entries; i++) {
				read(state->rawdev.rawdev_fd, &ifd, sizeof(struct ifd_entry));
				ifd_byte_order(&ifd);
				switch (ifd.tag) {
				case Exif_Image_PageNumber:
					ifd_page_num = ifd;
					break;
				case Exif_Photo_DateTimeOriginal & 0xffff:
					ifd_date_time = ifd;
					break;
				case Exif_Image_ExifTag:
					subifd_offset = ifd.offset;
					break;
				case Exif_Photo_SubSecTimeOriginal & 0xffff:
					ifd_subsec = ifd;
					break;
				}
			}
			// ensure that IFD0 finished correctly (0x00000000 in the end), set file pointer to SubIFD and
			// process remaining fields
			read(state->rawdev.rawdev_fd, &data32, sizeof(data32));
			process -= (subifd_offset == 0 || data32 != 0) ? 2 : 1;
			curr_pos = state->rawdev.file_start + TIFF_HDR_OFFSET + subifd_offset;
			lseek64(state->rawdev.rawdev_fd, curr_pos, SEEK_SET);
		} while (process > 0);

		// assemble file name from collected Exif fields
		if (ifd_page_num.len != 0) {
			sz = sprintf(name, "%02lu_", ifd_page_num.offset);
		}
		if (ifd_date_time.len != 0) {
			sz += exif_get_text(state, &ifd_date_time, name + sz);
		}
		if (ifd_subsec.len != 0) {
			name[sz++] = SUBSEC_SEPARATOR;
			sz += exif_get_text(state, &ifd_subsec, name + sz);
		}
		sprintf(name + sz, ".jpeg");
	} else {
		// a fallback option in case exif is not found - randomly generate file name
		int num;
		srand(time(NULL));
		num = rand();
		sprintf(name, "%010d.jpeg", num);
		D3(fprintf(debug_file, "Exif marker is not found not found, using randomly generated file name: %s\n", name));
	}

	lseek64(state->rawdev.rawdev_fd, save_pos, SEEK_SET);
	return 0;
}

void dump_vfs(struct statvfs *vfs)
{
	fprintf(debug_file, "vfs.f_bsize = %lu\n", vfs->f_bsize);
	fprintf(debug_file, "vfs.f_frsize = %lu\n", vfs->f_frsize);
	fprintf(debug_file, "vfs.f_blocks = %lu\n", vfs->f_blocks);
	fprintf(debug_file, "vfs.f_bfree = %lu\n", vfs->f_bfree);
	fprintf(debug_file, "vfs.f_bavail = %lu\n", vfs->f_bavail);
	fprintf(debug_file, "vfs.f_files = %lu\n", vfs->f_files);
	fprintf(debug_file, "vfs.f_ffree= %lu\n", vfs->f_ffree);
	fprintf(debug_file, "vfs.f_favail = %lu\n", vfs->f_favail);
	fprintf(debug_file, "vfs.f_fsid = %lu\n", vfs->f_fsid);
	fprintf(debug_file, "vfs.f_flag = %lu\n", vfs->f_flag);
	fprintf(debug_file, "vfs.f_namemax = %lu\n", vfs->f_namemax);
}
/**
 * @brief Create new file name, check free space on disk and open a file
 * @param[in]   f_op   pointer to a structure holding information about currently opened file
 * @return      \e FILE_OK if file was successfully opened and negative error code otherwise
 */
static int start_new_file(struct file_opts *f_op)
{
	int ret;
	int err;
	struct statvfs vfs;
	uint64_t free_size = 0;
	char file_name[ELPHEL_PATH_MAX] = {0};

	memset(&vfs, 0, sizeof(struct statvfs));
	ret = statvfs(f_op->state->path_prefix, &vfs);
	if (ret != 0) {
		D0(fprintf(debug_file, "Unable to get free size on disk, statvfs() returned %d\n", ret));
		return -CAMOGM_FRAME_FILE_ERR;
	}
	free_size = (uint64_t)vfs.f_bsize * (uint64_t)vfs.f_bfree;
	// statvfs can return irrelevant values in some fields for unsupported file systems,
	// thus free_size is checked to be equal to non-zero value
	if (free_size > 0 && free_size < FREE_SIZE_LIMIT) {
		fprintf(debug_file, "free_size = %llu; vfs.f_bsize = %lu; vfs.f_bfree = %lu; vfs.f_bsize * vfs.f_bfree = %llu\n",
				free_size, vfs.f_bsize, vfs.f_bfree, vfs.f_bsize * vfs.f_bfree);
		dump_vfs(&vfs);
		return -CAMOGM_NO_SPACE;
	}

	make_fname(f_op->state, file_name);
	sprintf(f_op->state->path, "%s%s", f_op->state->path_prefix, file_name);

	if ((f_op->fh = fopen(f_op->state->path, "w")) == NULL) {
		err = errno;
		D0(fprintf(debug_file, "Error opening %s for writing\n", file_name));
		D0(fprintf(debug_file, "%s\n", strerror(err)));
		return -CAMOGM_FRAME_FILE_ERR;
	}

	return FILE_OK;
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
static int check_edge_case(const struct iovec *from, const struct iovec *to, const struct iovec *marker, struct crb_ptrs *crbp)
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

	return MATCH_FOUND;
}

/**
 * @brief Perform file operation in accordance with current file state.
 * @param[in]   f_op   pointer to a structure holding information about currently opened file
 * @param[in]   from   start pointer to data buffer
 * @param[in]   to     end pointer to data buffer
 * @return      a constant of #file_result type
 */
static int write_buffer(struct file_opts *f_op, unsigned char *from, unsigned char *to)
{
	int ret = FILE_OK;
	int len;
	unsigned int sz;

	sz = to - from;
	switch (f_op->file_state) {
	case WRITE_RUNNING:
		len = fwrite(from, sz, 1, f_op->fh);
		if (len != 1) {
			perror(__func__);
			ret = FILE_WR_ERR;
		}
		break;
	case WRITE_START:
		if ((ret = start_new_file(f_op)) == FILE_OK) {
			len = fwrite(from, sz, 1, f_op->fh);
			if (len != 1) {
				perror(__func__);
				ret = FILE_WR_ERR;
			}
		} else {
			if (ret == -CAMOGM_NO_SPACE)
				D0(fprintf(debug_file, "No free space left on the disk\n"));
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

/**
 * @brief Extract JPEG files from raw device buffer
 *
 * Data from raw device is read to a buffer in #PHY_BLK_SZ blocks. The buffer is
 * analyzed for JPEG markers and then the data from buffer is written to a file.
 * @param[in]   state   a pointer to a structure containing current state
 * @return      0 if files were extracted successfully and negative error code otherwise
 */
int camogm_read(camogm_state *state)
{
	const int include_markers = INCLUDE_MARKERS;
	int process;
	int zero_cross;
	int file_op;
	int err;
	int pos_start, pos_stop;
	int buff_processed;
	int file_pos_saved;
	ssize_t rd;
	unsigned char buff[PHY_BLK_SZ];
	unsigned char next_buff[PHY_BLK_SZ];
	unsigned char *save_from = NULL;
	unsigned char *save_to = NULL;
	struct file_opts f_opts;
	uint64_t dev_curr_pos = 0;
	uint64_t include_st_marker, include_en_marker;

	memset(&f_opts, 0, sizeof(struct file_opts));
	f_opts.file_state = WRITE_STOP;
	f_opts.state = state;

	state->rawdev.rawdev_fd = open(state->rawdev.rawdev_path, O_RDONLY);
	if (state->rawdev.rawdev_fd < 0) {
		D0(perror(__func__));
		D0(fprintf(debug_file, "Error opening raw device %s\n", state->rawdev.rawdev_path));
		return -CAMOGM_FRAME_FILE_ERR;
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
		rd = read(state->rawdev.rawdev_fd, buff, sizeof(buff));
		err = errno;
		if ((rd > 0) && (dev_curr_pos + rd > state->rawdev.end_pos)) {
			// read pointer jumped over the raw storage buffer end, truncate excessive data
			D3(fprintf(stderr, "End of raw storage buffer is reached, will start from the beginning\n"));
			rd = state->rawdev.end_pos - dev_curr_pos;
			zero_cross = 1;
			lseek64(state->rawdev.rawdev_fd, state->rawdev.start_pos, SEEK_SET);
			dev_curr_pos = state->rawdev.start_pos;
			if (rd == 0) {
				continue;
			}
		} else if (rd < 0) {
			// read error or read pointer exceeded raw storage capacity, close files and terminate
			process = 0;
			D0(fprintf(stderr, "Raw device read was unsuccessful: %s\n", strerror(err)));
		} else if (rd == 0) {
			// end of device file reached
			D3(fprintf(stderr, "End of raw storage device file is reached, will start from the beginning\n"));
			zero_cross = 1;
			lseek64(state->rawdev.rawdev_fd, state->rawdev.start_pos, SEEK_SET);
			dev_curr_pos = state->rawdev.start_pos;
		}
		if (process) {
			save_from = buff;
			save_to = buff + rd;
			buff_processed = 0;
			file_pos_saved = 0;
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
					if (file_pos_saved == 0)
						state->rawdev.file_start = dev_curr_pos + pos_start;
					fprintf(stderr, "File start position: %llu\n", state->rawdev.file_start);
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
					state->rawdev.file_start = dev_curr_pos + pos_start;
					file_pos_saved = 1;
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
					ssize_t next_rd = read(state->rawdev.rawdev_fd, next_buff, sizeof(next_buff));
					fprintf(stderr, "process MATCH_PARTIAL pos_start: read %d\n", next_rd);
					next_chunk.iov_len = next_rd;
					dev_curr_pos += next_rd;
					result = check_edge_case(&curr_chunk, &next_chunk, &elphel_st, &field_markers);
					fprintf(stderr, "process MATCH_PARTIAL: check edge case, result = %d\n", result);
					if (result == MATCH_FOUND  && f_opts.file_state == WRITE_STOP) {
						fprintf(stderr, "process MATCH_PARTIAL: match found\n");
						f_opts.file_state = WRITE_START;
						state->rawdev.file_start = dev_curr_pos + pos_start;
						fprintf(stderr, "File start position: %llu\n", state->rawdev.file_start);
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
					ssize_t next_rd = read(state->rawdev.rawdev_fd, next_buff, sizeof(next_buff));
					fprintf(stderr, "process MATCH_PARTIAL pos_stop: read %ld\n", next_rd);
					next_chunk.iov_len = next_rd;
					dev_curr_pos += next_rd;
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
			dev_curr_pos += rd;
		}
	}

	fprintf(stderr, "\n%d files read from %s\n", f_opts.file_cntr, state->rawdev.rawdev_path);

	if (close(state->rawdev.rawdev_fd) != 0) {
		perror("Unable to close raw device: ");
		return -CAMOGM_FRAME_FILE_ERR;
	}
	if (f_opts.fh != NULL && fclose(f_opts.fh) != 0) {
		perror("Unable to close data file: ");
		return -CAMOGM_FRAME_FILE_ERR;
	}

	return 0;
}
