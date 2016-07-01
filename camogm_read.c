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
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

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
#include <math.h>
#include <asm/byteorder.h>
#include <sys/statvfs.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "camogm_read.h"

/** @brief Offset in Exif where TIFF header starts */
#define TIFF_HDR_OFFSET           12
/** @brief Separator character between seconds and microseconds in JPEG file name */
#define SUBSEC_SEPARATOR          '.'
#define EXIF_DATE_TIME_FORMAT     "%Y:%m:%d %H:%M:%S"
#define CMD_DELIMITER             "/?"
#define CMD_BUFF_LEN              1024
#define SMALL_BUFF_LEN            32
#define COMMAND_LOOP_DELAY        500000
#define PAGE_BOUNDARY_MASK        0xffffffffffffe000
#define INDEX_FORMAT_STR          "port number = %d; unix time = %ld; usec time = %06u; offset = 0x%010llx; file size = %u\n"
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

#define COMMAND_TABLE \
	X(CMD_BUILD_INDEX, "build_index") \
	X(CMD_GET_INDEX, "get_index") \
	X(CMD_READ_DISK, "read_disk") \
	X(CMD_READ_FILE, "read_file") \
	X(CMD_READ_ALL_FILES, "read_all_files") \
	X(CMD_STATUS, "status")

#define X(a, b) a,
enum socket_commands {
	COMMAND_TABLE
};
#undef X

#define X(a, b) b,
static const char *cmd_list[] = {
	COMMAND_TABLE
};
#undef X

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

enum search_state {
	SEARCH_SKIP,
	SEARCH_FILE_START,
	SEARCH_FILE_DATA
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

struct exit_state {
	camogm_state *state;
	struct disk_idir *idir;
	int ret_val;
	int *sockfd_const;
	int *sockfd_temp;
};

static inline void exit_thread(void *arg);
static void build_index(camogm_state *state, struct disk_idir *idir);

void dump_index_dir(const struct disk_idir *idir)
{
	struct disk_index *ind = idir->head;

	while (ind != NULL) {
		fprintf(debug_file, INDEX_FORMAT_STR,
				ind->port, ind->rawtime, ind->usec, ind->f_offset, ind->f_size);
		ind = ind->next;
	}
}

int create_node(struct disk_index **index)
{
	if (*index != NULL)
		return -1;

	*index = malloc(sizeof(struct disk_index));
	if (*index != NULL) {
		memset(*index, 0, sizeof(struct disk_index));
		return 0;
	} else {
		return -1;
	}
}

int add_node(struct disk_idir *idir, struct disk_index *index)
{
	if (idir->head == NULL && idir->tail == NULL) {
		idir->head = index;
		idir->tail = index;
		idir->size = 1;
	} else {
		index->prev = idir->tail;
		idir->tail->next = index;
		idir->tail = index;
		idir->size++;
	}

	return idir->size;
}

struct disk_index *find_by_offset(const struct disk_idir *idir, uint64_t offset)
{
	struct disk_index *index = idir->head;

	while (index != NULL) {
		if (index->f_offset == offset)
			break;
		index = index->next;
	}

	return index;
}

int remove_node(struct disk_idir *idir, struct disk_index *node)
{
	if (node == NULL)
		return -1;

	if (node == idir->head) {
		idir->head = node->next;
		idir->head->prev = NULL;
	} else if (node == idir->tail) {
		idir->tail = node->prev;
		idir->tail->next = NULL;
	} else {
		struct disk_index *ind = idir->head;
		while (ind != NULL) {
			if (ind == node) {
				ind->prev->next = ind->next;
				ind->next->prev = ind->prev;
				break;
			}
			ind = ind->next;
		}
	}
	free(node);
	node = NULL;
	idir->size--;

	return idir->size;
}

int delete_idir(struct disk_idir *idir)
{
	struct disk_index *curr_ind;
	struct disk_index *next_ind;

	if (idir == NULL || idir->head == NULL)
		return -1;

	curr_ind = idir->head;
	next_ind = curr_ind->next;
	while (curr_ind != NULL) {
		free(curr_ind);
		curr_ind = next_ind;
		if (curr_ind != NULL)
			next_ind = curr_ind->next;
	}
	idir->head = idir->tail = NULL;
	idir->size = 0;

	return 0;
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
static int find_marker(const unsigned char * restrict buff_ptr, ssize_t buff_sz, const unsigned char * restrict pattern, ssize_t pt_sz,
		int add_pattern)
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
			if (add_pattern)
				ret = i;
			else
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
 * @todo update description
 */
static size_t exif_get_text(camogm_state *state, struct ifd_entry *tag, char *buff)
{
	size_t bytes = 0;
	size_t str_len = tag->len * exif_data_fmt[tag->format];
	uint64_t curr_pos = state->rawdev.file_start + TIFF_HDR_OFFSET + tag->offset;

	lseek64(state->rawdev.rawdev_fd, curr_pos, SEEK_SET);
	bytes = read(state->rawdev.rawdev_fd, buff, str_len);

	return bytes;
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
 * @todo update description
 */
static int save_index(camogm_state *state, struct disk_idir *idir)
{
	int ret = 0;
	int process = 2;
	uint32_t data32;
	uint16_t num_entries = 0;
	uint64_t curr_pos;
	uint64_t subifd_offset = 0;
	struct tiff_hdr hdr;
	struct ifd_entry ifd;
	struct ifd_entry ifd_page_num = {0};
	struct ifd_entry ifd_date_time = {0};
	struct ifd_entry ifd_subsec = {0};
	struct disk_index *node = NULL;
	unsigned char read_buff[TIFF_HDR_OFFSET] = {0};
	char str_buff[32] = {0};
	uint64_t save_pos = lseek64(state->rawdev.rawdev_fd, 0, SEEK_CUR);

	if (idir == NULL)
		return -1;

	if (create_node(&node) != 0)
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

		// fill disk index node with Exif data and add it to disk index directory
		node->f_offset = state->rawdev.file_start;
		if (ifd_page_num.len != 0) {
			node->port = (uint32_t)ifd_page_num.offset;
		}
		if (ifd_date_time.len != 0) {
			struct tm tm;
			exif_get_text(state, &ifd_date_time, str_buff);
			strptime(str_buff, EXIF_DATE_TIME_FORMAT, &tm);
			node->rawtime = mktime(&tm);
		}
		if (ifd_subsec.len != 0) {
			exif_get_text(state, &ifd_subsec, str_buff);
			node->usec = strtoul(str_buff, NULL, 10);
		}
		if (node->rawtime != -1)
			add_node(idir, node);
		else
			ret = -1;
	}

	lseek64(state->rawdev.rawdev_fd, save_pos, SEEK_SET);
	return ret;
}

int stop_index(struct disk_idir *idir, uint64_t pos_stop)
{
	int ret = 0;
	if (idir->tail != NULL) {
		idir->tail->f_size = pos_stop - idir->tail->f_offset;
	} else {
		ret = -1;
	}
	return ret;
}

/**
 * @brief Create new file name, check free space on disk and open a file
 * @param[in]   f_op   pointer to a structure holding information about currently opened file
 * @return      \e FILE_OK if file was successfully opened and negative error code otherwise
 */
//static int start_new_file(struct file_opts *f_op)
//{
//	int ret;
//	int err;
//	struct statvfs vfs;
//	uint64_t free_size = 0;
//	char file_name[ELPHEL_PATH_MAX] = {0};
//
//	memset(&vfs, 0, sizeof(struct statvfs));
//	ret = statvfs(f_op->state->path_prefix, &vfs);
//	if (ret != 0) {
//		D0(fprintf(debug_file, "Unable to get free size on disk, statvfs() returned %d\n", ret));
//		return -CAMOGM_FRAME_FILE_ERR;
//	}
//	free_size = (uint64_t)vfs.f_bsize * (uint64_t)vfs.f_bfree;
//	// statvfs can return irrelevant values in some fields for unsupported file systems,
//	// thus free_size is checked to be equal to non-zero value
//	if (free_size > 0 && free_size < FREE_SIZE_LIMIT) {
//		return -CAMOGM_NO_SPACE;
//	}
//
////	make_fname(f_op->state, file_name);
//	sprintf(f_op->state->path, "%s%s", f_op->state->path_prefix, file_name);
//
//	if ((f_op->fh = fopen(f_op->state->path, "w")) == NULL) {
//		err = errno;
//		D0(fprintf(debug_file, "Error opening %s for writing\n", file_name));
//		D0(fprintf(debug_file, "%s\n", strerror(err)));
//		return -CAMOGM_FRAME_FILE_ERR;
//	}
//
//	return FILE_OK;
//}

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
//static int write_buffer(struct file_opts *f_op, unsigned char *from, unsigned char *to)
//{
//	int ret = FILE_OK;
//	int len;
//	unsigned int sz;
//
//	sz = to - from;
//	switch (f_op->file_state) {
//	case WRITE_RUNNING:
//		len = fwrite(from, sz, 1, f_op->fh);
//		if (len != 1) {
//			perror(__func__);
//			ret = FILE_WR_ERR;
//		}
//		break;
//	case WRITE_START:
//		if ((ret = start_new_file(f_op)) == FILE_OK) {
//			len = fwrite(from, sz, 1, f_op->fh);
//			if (len != 1) {
//				perror(__func__);
//				ret = FILE_WR_ERR;
//			}
//		} else {
//			if (ret == -CAMOGM_NO_SPACE)
//				D0(fprintf(debug_file, "No free space left on the disk\n"));
//			f_op->fh = NULL;
//			ret = FILE_OPEN_ERR;
//		}
//		break;
//	case WRITE_STOP:
//		len = fwrite(from, sz, 1, f_op->fh);
//		if (len != 1) {
//			perror(__func__);
//			ret = FILE_WR_ERR;
//		}
//		if (fclose(f_op->fh) != 0) {
//			perror(__func__);
//			ret = FILE_CLOSE_ERR;
//		} else {
//			f_op->fh = NULL;
//			f_op->file_cntr++;
//		}
//		break;
//	default:
//		ret = FILE_OPT_ERR;
//	}
//	return ret;
//}

void send_buffer(int sockfd, unsigned char *buff, size_t sz)
{
	size_t bytes_left = sz;
	ssize_t bytes_written = 0;
	size_t offset = 0;

	while (bytes_left > 0) {
		bytes_written = write(sockfd, &buff[offset], bytes_left);
		if (bytes_written < 0) {
			perror(__func__);
			return;
		}
		bytes_left -= bytes_written;
		offset += bytes_written;
	}
}

int mmap_disk(rawdev_buffer *rawdev, const struct range *range)
{
	int ret = 0;
	size_t mmap_size = range->to - range->from;

	rawdev->rawdev_fd = open(rawdev->rawdev_path, O_RDONLY);
	if (rawdev->rawdev_fd < 0) {
		return -1;
	}
	rawdev->disk_mmap = mmap(0, mmap_size, PROT_READ, MAP_SHARED, rawdev->rawdev_fd, range->from);
	if (rawdev->disk_mmap == MAP_FAILED) {
		rawdev->disk_mmap = NULL;
		close(rawdev->rawdev_fd);
		return -1;
	}
	rawdev->mmap_offset = range->from;
	rawdev->mmap_current_size = mmap_size;

	return ret;
}

int munmap_disk(rawdev_buffer *rawdev)
{
	if (rawdev->disk_mmap == NULL)
		return 0;

	if (munmap(rawdev->disk_mmap, rawdev->mmap_current_size) != 0)
		return -1;
	if (close(rawdev->rawdev_fd) != 0)
		return -1;
	rawdev->mmap_offset = 0;
	rawdev->disk_mmap = NULL;

	return 0;
}

bool is_in_range(struct range *range, struct disk_index *indx)
{
	if (indx->f_offset >= range->from &&
			indx->f_offset <= range->to &&
			(indx->f_offset + indx->f_size) <= range->to)
		return true;
	else
		return false;
}

#define PORT_NUMBER 3456
void prep_socket(int *socket_fd)
{
	int opt = 1;
	struct sockaddr_in sock;

	memset((char *)&sock, 0, sizeof(struct sockaddr_in));
	sock.sin_family = AF_INET;
	sock.sin_port = htons(PORT_NUMBER);
	*socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(*socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
	bind(*socket_fd, (struct sockaddr *) &sock, sizeof(struct sockaddr_in));
	listen(*socket_fd, 10);
}

/**
 *
 * @param cmd
 * cmd pointer is updated to point to current command
 * @return -1 command not recognized, -2 end of buffer
 */
int parse_command(char **cmd)
{
	size_t cmd_len;
	int cmd_indx = -1;
	char *char_ptr;

	D6(fprintf(debug_file, "Parsing command line: %s\n", *cmd));
	char_ptr = strpbrk(*cmd, CMD_DELIMITER);
	if (char_ptr != NULL) {
		char_ptr[0] = '\0';
		char_ptr++;
		for (int i = 0; i < sizeof(cmd_list) / sizeof(cmd_list[0]); i++) {
			cmd_len = strlen(cmd_list[i]);
			if (strncmp(char_ptr, cmd_list[i], cmd_len) == 0) {
				cmd_indx = i;
				break;
			}
		}
		*cmd = char_ptr;
	} else {
		cmd_indx = -2;
	}

	return cmd_indx;
}

/**
 * @brief Break HTTP GET string after the command part as we do not need that part. The function
 * finds the first space character after the command part starts and replaces it with null.
 * @param[in,out]   cmd   pointer to HTTP GET string
 */
void trim_command(char *cmd, ssize_t cmd_len)
{
	char *ptr_start, *ptr_end;

	if (cmd_len >= 0 && cmd_len < CMD_BUFF_LEN)
		cmd[cmd_len] = '\0';
	ptr_start = strpbrk(cmd, CMD_DELIMITER);
	if (ptr_start) {
		ptr_end = strchr(ptr_start, ' ');
		if (ptr_end)
			ptr_end[0] = '\0';
	}
}

void send_split_file(rawdev_buffer *rawdev, struct disk_index *indx, int sockfd)
{
	ssize_t rcntr = 0;
	ssize_t scntr = 0;
	size_t head_sz = rawdev->end_pos - indx->f_offset;
	size_t tail_sz = indx->f_size - head_sz;
	uint64_t curr_pos = lseek64(rawdev->rawdev_fd, 0, SEEK_CUR);
	unsigned char *buff = malloc(indx->f_size);

	if (buff == NULL)
		return;

	lseek64(rawdev->rawdev_fd, indx->f_offset, SEEK_SET);
	while (rcntr < head_sz && rcntr != -1)
		rcntr += read(rawdev->rawdev_fd, &buff[rcntr], head_sz - rcntr);
	rcntr = 0;
	lseek64(rawdev->rawdev_fd, rawdev->start_pos, SEEK_SET);
	while (rcntr < tail_sz && rcntr != -1)
		rcntr += read(rawdev->rawdev_fd, &buff[head_sz + rcntr], tail_sz - rcntr);

	while (scntr < indx->f_size && scntr != -1)
		scntr += write(sockfd, &buff[scntr], head_sz - scntr);

	fprintf(debug_file, "%s: %d bytes sent\n", __func__, scntr);

	lseek64(rawdev->rawdev_fd, curr_pos, SEEK_SET);
}

void send_fnum(int sockfd, size_t num)
{
	char buff[SMALL_BUFF_LEN] = {0};
	int len;

	len = snprintf(buff, SMALL_BUFF_LEN - 1, "Number of files: %d\n", num);
	buff[len] = '\0';
	write(sockfd, buff, len);
}

/**
 *
 * @param arg
 * @todo print unrecognized command
 */
void *reader(void *arg)
{
	int sockfd, fd;
	int disk_chunks;
	int cmd;
	char cmd_buff[CMD_BUFF_LEN] = {0};
	char *cmd_ptr;
	char send_buff[CMD_BUFF_LEN] = {0};
	bool transfer;
	ssize_t cmd_len;
	size_t mm_file_start, mm_file_size;
	size_t file_cntr;
	camogm_state *state = (camogm_state *)arg;
	rawdev_buffer *rawdev = &state->rawdev;
	struct stat stat_buff;
	struct range mmap_range;
	struct disk_index *disk_indx, *cross_boundary_indx;
	struct disk_idir index_dir = {
			.head = NULL,
			.tail = NULL,
			.size = 0
	};
	struct exit_state exit_state = {
			.state = state,
			.idir = &index_dir,
			.sockfd_const = &sockfd,
			.sockfd_temp = &fd,
			.ret_val = 0

	};

	prep_socket(&sockfd);
	pthread_cleanup_push(exit_thread, &exit_state);
	while (true) {
		fd = accept(sockfd, NULL, 0);
		if (fd == -1)
			continue;
		cmd_len = read(fd, cmd_buff, sizeof(cmd_buff) - 1);
		cmd_ptr = cmd_buff;
		trim_command(cmd_ptr, cmd_len);
		while ((cmd = parse_command(&cmd_ptr)) != -2) {
			if (cmd >= 0)
				D6(fprintf(debug_file, "Got command '%s', number %d\n", cmd_list[cmd], cmd));
			switch (cmd) {
			case CMD_BUILD_INDEX:
				// scan raw device buffer and create disk index directory
				if (index_dir.size != 0) {
					delete_idir(&index_dir);
				}
				build_index(state, &index_dir);
				D3(fprintf(debug_file, "%d files read from %s\n", index_dir.size, state->rawdev.rawdev_path));
				break;
			case CMD_GET_INDEX:
				// send the content of disk index directory over socket
				if (index_dir.size > 0) {
					int len;
					disk_indx = index_dir.head;
					while (disk_indx != NULL) {
						len = snprintf(send_buff, CMD_BUFF_LEN - 1, INDEX_FORMAT_STR,
								disk_indx->port, disk_indx->rawtime, disk_indx->usec, disk_indx->f_offset, disk_indx->f_size);
						send_buff[len] = '\0';
						write(fd, send_buff, len);
						disk_indx = disk_indx->next;
					}
				} else {
					D0(fprintf(debug_file, "Index directory does not contain any files. Try to rebuild index "
							"directory with 'build_index' command\n"));
				}
				break;
			case CMD_READ_DISK:
				// mmap raw device buffer in MMAP_CHUNK_SIZE chunks and send them over socket
				mmap_range.from = rawdev->start_pos & PAGE_BOUNDARY_MASK;
				mmap_range.to = mmap_range.from + rawdev->mmap_default_size;
				disk_chunks = (size_t)ceil((double)(rawdev->end_pos - rawdev->start_pos) / (double)rawdev->mmap_default_size);
				transfer = true;
				mm_file_start = rawdev->start_pos;
				mm_file_size = rawdev->mmap_default_size - rawdev->start_pos;
				send_fnum(fd, disk_chunks);
				close(fd);
				while (disk_chunks > 0 && transfer) {
					fd = accept(sockfd, NULL, 0);
					if (mmap_disk(rawdev, &mmap_range) == 0) {
						send_buffer(fd, &rawdev->disk_mmap[mm_file_start], mm_file_size);
					} else {
						transfer = false;
						D0(fprintf(debug_file, "Unable to map disk to memory region:"
								"disk region start = 0x%llx, disk region end = 0x%llx\n", mmap_range.from, mmap_range.to));
					}
					if (munmap_disk(rawdev) != 0) {
						transfer = false;
						D0(fprintf(debug_file, "Unable to unmap memory region\n"));
					}
					mm_file_start = 0;
					mm_file_size = rawdev->mmap_default_size;
					disk_chunks--;
					mmap_range.from = mmap_range.to;
					mmap_range.to = mmap_range.from + rawdev->mmap_default_size;
					if (mmap_range.to > rawdev->end_pos) {
						mmap_range.to = rawdev->end_pos;
						mm_file_size = mmap_range.to - mmap_range.from;
					}
					close(fd);
				}
				break;
			case CMD_READ_FILE:
				break;
			case CMD_READ_ALL_FILES:
				// read files from raw device buffer and send them over socket; the disk index directory
				// should be built beforehand
				if (index_dir.size > 0) {
					send_fnum(fd, index_dir.size);
					close(fd);
					mmap_range.from = rawdev->start_pos;
					mmap_range.to = rawdev->start_pos + rawdev->mmap_default_size;
					disk_indx = index_dir.head;
					cross_boundary_indx = NULL;
					file_cntr = 0;
					transfer = true;
					while (file_cntr < index_dir.size && disk_indx != NULL) {
						if (is_in_range(&mmap_range, disk_indx) && rawdev->disk_mmap != NULL) {
							fd = accept(sockfd, NULL, 0);
							mm_file_start = disk_indx->f_offset - rawdev->mmap_offset;
							send_buffer(fd, &rawdev->disk_mmap[mm_file_start], disk_indx->f_size);
							close(fd);
							disk_indx = disk_indx->next;
							file_cntr++;
						} else {
							if (munmap_disk(rawdev) == 0) {
								mmap_range.from = disk_indx->f_offset & PAGE_BOUNDARY_MASK;
								mmap_range.to = mmap_range.from + rawdev->mmap_default_size;
								if (mmap_range.to > rawdev->end_pos) {
									mmap_range.to = rawdev->end_pos;
								}
								if (disk_indx->f_offset + disk_indx->f_size <= mmap_range.to) {
									if (mmap_disk(rawdev, &mmap_range) < 0) {
										disk_indx = NULL;
										D0(fprintf(debug_file, "Unable to map disk to memory region:"
												" disk region start = 0x%llx, disk region end = 0x%llx\n", mmap_range.from, mmap_range.to));
									}
								} else {
									cross_boundary_indx = disk_indx;
									disk_indx = NULL;
								}
							} else {
								disk_indx = NULL;
								D0(fprintf(debug_file, "Unable to unmap memory region\n"));
							}
						}
					}
					munmap_disk(rawdev);
					if (cross_boundary_indx != NULL) {
						send_split_file(rawdev, cross_boundary_indx, fd);
						close(fd);
					}
				} else {
					D0(fprintf(debug_file, "Index directory does not contain any files. Try to rebuild index "
							"directory with 'build_index' command\n"));
				}
				break;
			case CMD_STATUS:
				break;
			default:
				D0(fprintf(debug_file, "Unrecognized command is skipped\n"));
			}
		}
		if (fstat(fd, &stat_buff) != EBADF)
			close(fd);
		usleep(COMMAND_LOOP_DELAY);
	}
	pthread_cleanup_pop(0);

	return (void *) 0;
}

static inline void exit_thread(void *arg)
{
	struct exit_state *s = (struct exit_state *)arg;
	struct stat buff;

	if (fstat(s->state->rawdev.rawdev_fd, &buff) != EBADF) {
		if (s->state->rawdev.disk_mmap != NULL)
			munmap(s->state->rawdev.disk_mmap, s->state->rawdev.mmap_current_size);
		close(s->state->rawdev.rawdev_fd);
		s->state->rawdev.rawdev_fd = -1;
	}
	if (s->idir->size != 0)
		delete_idir(s->idir);
	if (fstat(*s->sockfd_const, &buff) != EBADF)
		close(*s->sockfd_const);
	if (fstat(*s->sockfd_temp, &buff) != EBADF)
		close(*s->sockfd_temp);
	s->state->rawdev.thread_finished = true;
}

/**
 * @brief Extract JPEG files from raw device buffer
 *
 * Data from raw device is read to a buffer in #PHY_BLK_SZ blocks. The buffer is
 * analyzed for JPEG markers and then the data from buffer is written to a file.
 * @param[in]   state   a pointer to a structure containing current state
 * @return      0 if files were extracted successfully and negative error code otherwise
 * @warning The main processing loop of the function is enclosed in @e pthread_cleanup_push and @e pthread_cleanup_pop
 * calls. The effect of use of normal @b return or @b break to prematurely leave this loop is undefined.
 * @todo update description, reorder decision tree
 */
void build_index(camogm_state *state, struct disk_idir *idir)
{
	const int include_markers = INCLUDE_MARKERS;
	int process;
	int zero_cross;
	int err;
	int pos_start, pos_stop;
	int buff_processed;
	int search_state;
	int idir_result;
	ssize_t rd;
	unsigned char buff[PHY_BLK_SZ];
	unsigned char next_buff[PHY_BLK_SZ];
	unsigned char *active_buff = buff;
	unsigned char *save_from = NULL;
	unsigned char *save_to = NULL;
	uint64_t dev_curr_pos = 0;
	uint64_t include_st_marker, include_en_marker;
	size_t add_stm_len, add_enm_len;

	state->rawdev.rawdev_fd = open(state->rawdev.rawdev_path, O_RDONLY);
	if (state->rawdev.rawdev_fd < 0) {
		D0(perror(__func__));
		D0(fprintf(debug_file, "Error opening raw device %s\n", state->rawdev.rawdev_path));
		return;
	}

	if (include_markers) {
		include_st_marker = 0;
		add_stm_len = elphel_st.iov_len;
		include_en_marker = 1;
		add_enm_len = 0;
	} else {
		include_st_marker = 0;
		add_stm_len = 0;
		include_en_marker = 1;
		add_enm_len = elphel_en.iov_len;
	}

	process = 1;
	zero_cross = 0;
	search_state = SEARCH_SKIP;
	idir_result = 0;
	while (process) {
		rd = read(state->rawdev.rawdev_fd, buff, sizeof(buff));
		err = errno;
		if ((rd > 0) && (dev_curr_pos + rd > state->rawdev.end_pos)) {
			// read pointer jumped over the raw storage buffer end, truncate excessive data
			D3(fprintf(debug_file, "End of raw storage buffer is reached, will start from the beginning\n"));
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
			D0(fprintf(debug_file, "Raw device read was unsuccessful: %s\n", strerror(err)));
		} else if (rd == 0) {
			// end of device file reached
			D3(fprintf(debug_file, "End of raw storage device file is reached, will start from the beginning\n"));
			zero_cross = 1;
			lseek64(state->rawdev.rawdev_fd, state->rawdev.start_pos, SEEK_SET);
			dev_curr_pos = state->rawdev.start_pos;
		}
		if (process) {
			save_from = buff;
			save_to = buff + rd;
			active_buff = buff;
			buff_processed = 0;
			do {
				// process data in read buffer
				pos_start = find_marker(save_from, save_to - save_from, elphel_st.iov_base, elphel_st.iov_len, include_st_marker);
				pos_stop = find_marker(save_from, save_to - save_from, elphel_en.iov_base, elphel_en.iov_len, include_en_marker);

				if (pos_start == MATCH_NOT_FOUND && pos_stop == MATCH_NOT_FOUND) {
					// normal condition, search in progress
					buff_processed = 1;
//					D6(fprintf(debug_file, "State 'skip data'\n"));
				} else if (pos_start >= 0 && pos_stop == MATCH_NOT_FOUND && search_state == SEARCH_SKIP) {
					// normal condition, new file found
					search_state = SEARCH_FILE_DATA;
					state->rawdev.file_start = dev_curr_pos + pos_start + (save_from - active_buff);
					idir_result = save_index(state, idir);
					buff_processed = 1;
					D6(fprintf(debug_file, "New file found. File start position: %llu\n", state->rawdev.file_start));
					D6(fprintf(debug_file, "State 'starting file'\n"));
				} else if (pos_start >= 0 && pos_stop == MATCH_NOT_FOUND && search_state == SEARCH_FILE_DATA) {
					// error condition (normally should not happen), discard current index and start a new one
					buff_processed = 1;
					remove_node(idir, idir->tail);
					if (zero_cross == 0) {
						state->rawdev.file_start = dev_curr_pos + pos_start + (save_from - active_buff);
						idir_result = save_index(state, idir);
					} else {
						process = 0;
					}
					D6(fprintf(debug_file, "State 'abnormal start marker, remove current disk index from directory and skip data'\n"));
				} else if (pos_start == MATCH_NOT_FOUND && pos_stop >= 0 &&
						search_state == SEARCH_FILE_DATA) {
					// normal condition, save current file size to index directory
					uint64_t disk_pos = dev_curr_pos + pos_stop + (save_from - active_buff);
					search_state = SEARCH_SKIP;
					idir_result = stop_index(idir, disk_pos);
					buff_processed = 1;
					if (zero_cross)
						process = 0;
					D6(fprintf(debug_file, "State 'finishing file'\n"));
				} else if (pos_start == MATCH_NOT_FOUND && pos_stop >= 0 && search_state == SEARCH_SKIP) {
					// error condition (normally should not happen), drop current read buffer and do nothing
					buff_processed = 1;
					D6(fprintf(debug_file, "State 'abnormal stop marker, skip data'\n"));
				} else if (pos_start >= 0 && pos_stop >= 0 && pos_start > pos_stop && search_state == SEARCH_FILE_DATA) {
					// normal condition, start marker following stop marker found - this indicates a new file
					uint64_t disk_pos = dev_curr_pos + pos_stop + (save_from - active_buff);
					idir_result = stop_index(idir, disk_pos);
					if (zero_cross == 0) {
						state->rawdev.file_start = dev_curr_pos + pos_start + (save_from - active_buff);
						idir_result = save_index(state, idir);
						save_from = save_from + pos_start + add_stm_len;
						// @todo: replace with pointer to current buffer
						save_to = buff + rd;
					} else {
						buff_processed = 1;
						process = 0;
					}
					D6(fprintf(debug_file, "State 'stop current file and start new file'\n"));
				} else if (pos_start == MATCH_PARTIAL && search_state == SEARCH_SKIP) {
					// partial start marker found in the end of read buffer, get next chunk of data and try to find marker there
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
					next_chunk.iov_len = next_rd;
					result = check_edge_case(&curr_chunk, &next_chunk, &elphel_st, &field_markers);
					if (result == MATCH_FOUND) {
						search_state = SEARCH_FILE_DATA;
						state->rawdev.file_start = dev_curr_pos + pos_start + (save_from - active_buff);
						idir_result = save_index(state, idir);
						D6(fprintf(debug_file, "File start position: %llu\n", state->rawdev.file_start));
						save_from = next_chunk.iov_base + field_markers.second_buff.iov_len;
						save_to = next_chunk.iov_base + next_chunk.iov_len;
					} else {
						save_from = next_chunk.iov_base;
						save_to = next_chunk.iov_base + next_chunk.iov_len;
					}
					dev_curr_pos += next_rd;
					active_buff = next_buff;
					D6(fprintf(debug_file, "State 'check elphel_st cross boundary'; result = %d\n", result));
				} else if (pos_stop == MATCH_PARTIAL && search_state == SEARCH_FILE_DATA) {
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
					next_chunk.iov_len = next_rd;
					result = check_edge_case(&curr_chunk, &next_chunk, &elphel_en, &field_markers);
					if (result == MATCH_FOUND) {
						search_state = SEARCH_SKIP;
						uint64_t disk_pos = dev_curr_pos + pos_stop + (save_from - active_buff);
						idir_result = stop_index(idir, disk_pos);
						save_from = next_chunk.iov_base + field_markers.second_buff.iov_len;
						save_to = next_chunk.iov_base + next_chunk.iov_len;
					} else {
						save_from = next_chunk.iov_base;
						save_to = next_chunk.iov_base + next_chunk.iov_len;
					}
					dev_curr_pos += next_rd;
					active_buff = next_buff;
					D6(fprintf(debug_file, "State 'check elphel_en' cross boundary:; result = %d\n", result));
				} else {
					// no markers found and new file has not bee started yet - skip data
					D6(fprintf(debug_file, "Undefined state: pos_start = %d, pos_stop = %d, search_state = %d\n",
							pos_start, pos_stop, search_state));
					buff_processed = 1;
					if (zero_cross)
						process = 0;
				}
			} while (buff_processed == 0 && idir_result == 0);
			if (idir_result != 0) {
				process = 0;
			}
			dev_curr_pos += rd;
			state->rawdev.curr_pos_r = dev_curr_pos;
		}
	}

	if (close(state->rawdev.rawdev_fd) != 0) {
		perror("Unable to close raw device: ");
	}
	state->rawdev.rawdev_fd = -1;
}
