/** @file camogm_read.c
 * @brief Provides reading data written to raw device storage and transmitting the data over a socket.
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

/**
 * @addtogroup SPECIAL_INCLUDES Special includes
 * These defines are needed to use lseek64, strptime and usleep and should be set before includes
 * @{
 */
/** Needed for lseek64 */
#define _LARGEFILE64_SOURCE
/** Needed for strptime and usleep */
#define _XOPEN_SOURCE
/** Needed for usleep */
#define _XOPEN_SOURCE_EXTENDED
/** @} */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
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
#include "index_list.h"

/** @brief Offset in Exif where TIFF header starts */
#define TIFF_HDR_OFFSET           12
/** @brief The date and time format of Exif field */
#define EXIF_DATE_TIME_FORMAT     "%Y:%m:%d %H:%M:%S"
/** @brief The date and time format of 'find_file' command */
#define EXIF_TIMESTAMP_FORMAT     "%04d:%02d:%02d_%02d:%02d:%02d"
/** @brief The format string used for file parameters reporting. Time and port number are extracted from Exif */
#define INDEX_FORMAT_STR          "port_number=%d;unix_time=%ld;usec_time=%06u;offset=0x%010llx;file_size=%u\n"
/** @brief The delimiters used to separate several commands in one command string sent over socket */
#define CMD_DELIMITER             "/?"
/** @brief The length of a buffer for command string */
#define CMD_BUFF_LEN              1024
/** @brief The length of a buffer for string formatting */
#define SMALL_BUFF_LEN            32
/** @brief 64 bit mask to align offsets to 4 kb page boundary */
#define PAGE_BOUNDARY_MASK        0xffffffffffffe000
/** @brief The size of read buffer in bytes. The data will be read from disk in blocks of this size */
#define PHY_BLK_SZ                4096
/** @brief Include or exclude file start and stop markers from resulting file. This must be set to 1 for JPEG files */
#define INCLUDE_MARKERS           1
/** @brief The size of file search window. This window is memory mapped. */
#define SEARCH_SIZE_WINDOW        ((uint64_t)4 * (uint64_t)1048576)
/** @brief Time window (in seconds) used for disk index search. Index within this window is considered a candidate */
#define SEARCH_TIME_WINDOW        600
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

/** @brief X Macro for commands. Add new commands to @e COMMAND_TABLE
 * @{
 */
#define COMMAND_TABLE \
	X(CMD_BUILD_INDEX, "build_index") \
	X(CMD_GET_INDEX, "get_index") \
	X(CMD_READ_DISK, "read_disk") \
	X(CMD_READ_FILE, "read_file") \
	X(CMD_FIND_FILE, "find_file") \
	X(CMD_NEXT_FILE, "next_file") \
	X(CMD_PREV_FILE, "prev_file") \
	X(CMD_READ_ALL_FILES, "read_all_files") \
	X(CMD_STATUS, "status")

/** @enum socket_commands */
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
/** @} */

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
 * @enum search_state
 * @brief The state of the program during file search in raw device buffer
 * @var search_state::SEARCH_SKIP
 * Skip data in read buffer
 * @var search_state::SEARCH_FILE_DATA
 * The data in read buffer is a file data with known start offset
 */
enum search_state {
	SEARCH_SKIP,
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
 * @struct exit_state
 * @brief Container for the resources which should be freed before exit
 * @var exit_state::state
 * Pointer to #camogm_state structure containing current program state. This structure holds
 * file descriptors and memory mapped regions which should be closed and unmapped accordingly
 * @var exit_state::idir
 * Pointer to disk index directory. The nodes of this directory are dynamically allocated and must be freed
 * @var exit_state::sockfd_const
 * Socket descriptor
 * @var exit_state::sockfd_temp
 * Socket descriptor
 */
struct exit_state {
	camogm_state *state;
	struct disk_idir *idir;
	struct disk_idir *sparse_idir;
	int *sockfd_const;
	int *sockfd_temp;
};

static inline void exit_thread(void *arg);
static void build_index(camogm_state *state, struct disk_idir *idir);
static int mmap_disk(rawdev_buffer *rawdev, const struct range *range);
static int munmap_disk(rawdev_buffer *rawdev);

/**
 * @brief Debug function, prints the content of disk index directory
 * @param[in]   idir   pointer to disk index directory to be printed
 * @return      None
 */
void dump_index_dir(const struct disk_idir *idir)
{
	struct disk_index *ind = idir->head;

	printf("Head pointer = 0x%p, tail pointer = 0x%p\n", idir->head, idir->tail);
	while (ind != NULL) {
		fprintf(debug_file, INDEX_FORMAT_STR,
				ind->port, ind->rawtime, ind->usec, ind->f_offset, ind->f_size);
		fprintf(debug_file, "\tCurrent pointer: 0x%p, Prev pointer: 0x%p, next pointer: 0x%p\n", ind, ind->prev, ind->next);
		ind = ind->next;
	}
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
 * @param[in]   add_pattern include or exclude marker from resulting buffer offset
 * @return      The index in data buffer where pattern matches or error code from #match_result if it was not found
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

static int find_marker_backward(const unsigned char * restrict buff_ptr, ssize_t buff_sz, const unsigned char * restrict pattern, ssize_t pt_sz,
		int add_pattern)
{
	int ret = MATCH_NOT_FOUND;
	int j = 0;

	for (int i = buff_sz - 1; i > 0; i--) {
		if (buff_ptr[i] != pattern[j]) {
			// current symbol in data buffer and last symbol of pattern does not match
			j = pt_sz - 1;
		} else if (buff_ptr[i] == pattern[j] && j > 0) {
			// pattern symbol match
			j--;
		} else if (buff_ptr[i] == pattern[j] && j == 0) {
			// first pattern symbol match
			if (add_pattern)
				ret = i - j;
			else
				ret = i;
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
 * @brief Read a string from Exif for a given record.
 * @param[in,out]   rawdev   pointer to #rawdev_buffer structure containing
 * the current state of raw device buffer
 * @param[in]       tag     Exif image file directory record containing string offset
 * @param[out]      buff    buffer for the string to be read
 * @return          The number of bytes placed to the read buffer
 */
static size_t exif_get_text(rawdev_buffer *rawdev, struct ifd_entry *tag, char *buff)
{
	size_t bytes = 0;
	size_t str_len = tag->len * exif_data_fmt[tag->format];
	uint64_t curr_pos = rawdev->file_start + TIFF_HDR_OFFSET + tag->offset;

	lseek64(rawdev->rawdev_fd, curr_pos, SEEK_SET);
	bytes = read(rawdev->rawdev_fd, buff, str_len);

	return bytes;
}

/**
 * @brief Read Exif data from the file starting from #rawdev_buffer::file_start offset and
 * create a new node corresponding to the offset
 * @param[in,out]   rawdev   pointer to #rawdev_buffer structure containing
 * the current state of raw device buffer
 * @param[out]      indx     pointer to new disk index node
 * @return          0 if new node was successfully created and -1 otherwise
 */
static int read_index(rawdev_buffer *rawdev, struct disk_index **indx)
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
	char str_buff[SMALL_BUFF_LEN] = {0};
	uint64_t save_pos = lseek64(rawdev->rawdev_fd, 0, SEEK_CUR);

	if (indx == NULL)
		return -1;

	curr_pos = rawdev->file_start;
	lseek64(rawdev->rawdev_fd, curr_pos, SEEK_SET);
	if (read(rawdev->rawdev_fd, read_buff, sizeof(read_buff)) <= 0) {
		lseek64(rawdev->rawdev_fd, save_pos, SEEK_SET);
		return -1;
	}
	if (read_buff[2] == 0xff && read_buff[3] == 0xe1) {
		// get IFD0 offset from TIFF header
		read(rawdev->rawdev_fd, &hdr, sizeof(struct tiff_hdr));
		hdr_byte_order(&hdr);
		curr_pos = rawdev->file_start + TIFF_HDR_OFFSET + hdr.offset;
		lseek64(rawdev->rawdev_fd, curr_pos, SEEK_SET);
		// process IFD0 and SubIFD fields
		do {
			read(rawdev->rawdev_fd, &num_entries, sizeof(num_entries));
			num_entries = __be16_to_cpu(num_entries);
			for (int i = 0; i < num_entries; i++) {
				read(rawdev->rawdev_fd, &ifd, sizeof(struct ifd_entry));
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
			read(rawdev->rawdev_fd, &data32, sizeof(data32));
			process -= (subifd_offset == 0 || data32 != 0) ? 2 : 1;
			curr_pos = rawdev->file_start + TIFF_HDR_OFFSET + subifd_offset;
			lseek64(rawdev->rawdev_fd, curr_pos, SEEK_SET);
		} while (process > 0);

		// create and fill new disk index node with Exif data
		if (create_node(&node) == 0) {
			node->f_offset = rawdev->file_start;
			if (ifd_page_num.len != 0) {
				node->port = (uint32_t)ifd_page_num.offset;
			}
			if (ifd_date_time.len != 0) {
				struct tm tm;
				exif_get_text(rawdev, &ifd_date_time, str_buff);
				strptime(str_buff, EXIF_DATE_TIME_FORMAT, &tm);
				node->rawtime = mktime(&tm);
			}
			if (ifd_subsec.len != 0) {
				exif_get_text(rawdev, &ifd_subsec, str_buff);
				node->usec = strtoul(str_buff, NULL, 10);
			}
			if (node->rawtime != -1) {
				*indx = node;
			} else {
				free(node);
				ret = -1;
			}
		} else {
			ret = -1;
		}
	} else {
		ret = -1;
	}

	lseek64(rawdev->rawdev_fd, save_pos, SEEK_SET);
	return ret;
}
/**
 * @brief Calculate the size of current file and update the value in disk index directory
 * @param[in,out]   indx       pointer to disk index node which size should be calculated
 * @param[in]       pos_stop   the offset of the last byte of the current file
 * @return          0 if the file size was successfully updated and -1 otherwise
 * @note @e pos_stop points to the last byte of the file marker thus the size is incremented
 * by 1
 */
static int stop_index(struct disk_index *indx, uint64_t pos_stop)
{
	int ret = 0;

	if (indx != NULL) {
		indx->f_size = pos_stop - indx->f_offset + 1;
	} else {
		ret = -1;
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
 * @return      A constant of #match_result type
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
 * @brief Send mmaped buffer over opened socket
 * @param[in]   sockfd    opened socket descriptor
 * @param[in]   buff      pointer to memory mapped buffer
 * @param[in]   sz        the size of @e buff
 * @return      None
 */
static void send_buffer(int sockfd, unsigned char *buff, size_t sz)
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

/**
 * @brief Send file pointed by disk index node over opened socket
 * @param[in,out]   rawdev   pointer to #rawdev_buffer structure containing
 * the current state of raw device buffer
 * @param[in]       indx     disk index directory node
 * @param[in]       sockfd   opened socket descriptor
 * @return          0 in case disk index node was sent successfully and -1 otherwise
 */
static int send_file(rawdev_buffer *rawdev, struct disk_index *indx, int sockfd)
{
	uint64_t mm_file_start;
	struct range mmap_range;

	mmap_range.from = indx->f_offset & PAGE_BOUNDARY_MASK;
	mmap_range.to = indx->f_offset + indx->f_size;
	mm_file_start = indx->f_offset - mmap_range.from;
	if (mmap_disk(rawdev, &mmap_range) == 0) {
		send_buffer(sockfd, &rawdev->disk_mmap[mm_file_start], indx->f_size);
		if (munmap_disk(rawdev) != 0) {
			D0(fprintf(debug_file, "Unable to unmap memory region\n"));
			return -1;
		}
	} else {
		D0(fprintf(debug_file, "Unable to map disk to memory region:"
				"disk region start = 0x%llx, disk region end = 0x%llx\n", mmap_range.from, mmap_range.to));
		return -1;
	}
	 return 0;
}

/**
 * @brief Map a piece of raw device buffer to memory
 * @param[in,out]   rawdev   pointer to #rawdev_buffer structure containing
 * the current state of raw device buffer
 * @param[in]       range    pointer to #range structure holding the offsets of
 * mmaped region
 * @return          0 in case the region was mmaped successfully and -1 in case of an error
 */
static int mmap_disk(rawdev_buffer *rawdev, const struct range *range)
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

/**
 * @brief Unmap currently mapped raw device buffer
 * @param[in,out]   rawdev   pointer to #rawdev_buffer structure containing
 * the current state of raw device buffer
 * @return          0 in case the region was unmapped successfully and -1 in case of an error
 */
static int munmap_disk(rawdev_buffer *rawdev)
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

/**
 * @brief Check if the file pointed by disk index node is in the memory mapped region
 * @param[in]   range   pointer to #range structure holding the offsets of
 * memory mapped region
 * @param[in]   indx    pointer to the disk index node which should be checked for
 * presence in currently mapped region
 * @return      @b true if the file is the region and @b false otherwise
 */
static bool is_in_range(struct range *range, struct disk_index *indx)
{
	if (indx->f_offset >= range->from &&
			indx->f_offset <= range->to &&
			(indx->f_offset + indx->f_size) <= range->to)
		return true;
	else
		return false;
}

/**
 * @brief Prepare socket for communication
 * @param[out]   socket_fd   pointer to socket descriptor
 * @param[in]    port_num    socket port number
 * @return       None
 */
static void prep_socket(int *socket_fd, uint16_t port_num)
{
	int opt = 1;
	struct sockaddr_in sock;

	memset((char *)&sock, 0, sizeof(struct sockaddr_in));
	sock.sin_family = AF_INET;
	sock.sin_port = htons(port_num);
	*socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(*socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
	bind(*socket_fd, (struct sockaddr *) &sock, sizeof(struct sockaddr_in));
	listen(*socket_fd, 10);
}

/**
 * @brief Parse command line
 * @param[in]   cmd   pointer to command line buffer, this pointer is
 * updated to point to current command
 * @return      Positive command number from #socket_commands, -1 if command not
 * recognized and -2 to indicate that the command buffer has been fully processed
 */
static int parse_command(char **cmd)
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
 * @param[in]       cmd_len the length of the command in buffer
 * @return          None
 */
static void trim_command(char *cmd, ssize_t cmd_len)
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

/**
 * @brief Send a file crossing raw device buffer boundary.
 * Such a file is split into two parts, the one in the end of the buffer and the other
 * in the beginning, and should be sent in two steps
 * @param[in]   rawdev   pointer to #rawdev_buffer structure containing
 * the current state of raw device buffer
 * @param[in]   indx     pointer to the disk index node
 * @param[in]   sockfd   opened socket descriptor
 * @return      None
 */
static void send_split_file(rawdev_buffer *rawdev, struct disk_index *indx, int sockfd)
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

/**
 * @brief Send the number of files (or disk chunks) found over socket connection
 * @param[in]   sockfd   opened socket descriptor
 * @param[in]   num      the number to be sent
 * @return      None
 */
static void send_fnum(int sockfd, size_t num)
{
	char buff[SMALL_BUFF_LEN] = {0};
	int len;

	len = snprintf(buff, SMALL_BUFF_LEN - 1, "Number of files: %d\n", num);
	buff[len] = '\0';
	write(sockfd, buff, len);
}

/**
 * @brief Read file parameters from a string and fill in disk index node structure
 * @param[in]    cmd   pointer to a string with file parameters
 * @param[out]   indx  pointer to disk index node structure
 * @return       The number of parameters extracted from the string or -1 in case of an error
 */
static int get_indx_args(char *cmd, struct disk_index *indx)
{
	char *cmd_start = strchr(cmd, ':');

	if (cmd_start == NULL)
		return -1;
	return sscanf(++cmd_start, INDEX_FORMAT_STR, &indx->port, &indx->rawtime, &indx->usec, &indx->f_offset, &indx->f_size);
}

/**
 * @brief Read time stamp from a string and copy time in UNIX format to disk index
 * node structure
 * @param[in]   cmd   pointer to a string withe time stamp
 * @param[out]  indx  pointer to disk index node structure
 * @return      The number of input items matched from the string or -1 in case of an error
 */
static int get_timestamp_args(char *cmd, struct disk_index *indx)
{
	int ret;
	struct tm tm;
	char *cmd_start = strchr(cmd, ':');

	if (cmd_start == NULL)
		return -1;
	ret = sscanf(++cmd_start, EXIF_TIMESTAMP_FORMAT, &tm.tm_year, &tm.tm_mon,
			&tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	indx->rawtime = mktime(&tm);
	return ret;
}

/**
 * @brief Define memory mapped disk window where files will be searched
 * @param[in]   r   disk offsets range where memory mapped window will be located
 * @param[out]  s   search window
 * @return      0 if the window was successfully located in the region specified and
 * -1 in case of an error
 */
static int get_search_window(const struct range *r, struct range *s)
{
	if ((r->to - r->from) < SEARCH_SIZE_WINDOW || r->from > r->to)
		return -1;

	uint64_t middle = (r->to + r->from) / 2;
	s->from = (middle - SEARCH_SIZE_WINDOW / 2) & PAGE_BOUNDARY_MASK;
	s->to = middle + SEARCH_SIZE_WINDOW / 2;
	return 0;
}

/**
 * @brief Find JPEG file in a specified window. The window should be large enough to
 * contain at least one file.
 * @param[in]   rawdev   pointer to #rawdev_buffer structure containing
 * the current state of raw device buffer
 * @param[in]   wnd      pointer to a structure containing the offsets of a search window
 * @param[out]  indx     disk index structure which will hold the offset and size of a file
 * @return      0 if a file was found and -1 otherwise
 */
static int find_in_window(rawdev_buffer *rawdev, const struct range *wnd, struct disk_index **indx)
{
	int ret = -1;
	int pos_start, pos_stop;

	if (mmap_disk(rawdev, (const struct range *)wnd) == 0) {
		fprintf(debug_file, "Searching in mmapped window: from 0x%llx, to 0x%llx\n", wnd->from, wnd->from + rawdev->mmap_current_size);
		pos_start = find_marker(rawdev->disk_mmap, rawdev->mmap_current_size, elphel_st.iov_base, elphel_st.iov_len, 0);
		if (pos_start >= 0) {
			rawdev->file_start = rawdev->mmap_offset + pos_start;
			if (read_index(rawdev, indx) == 0) {
				pos_stop = find_marker(rawdev->disk_mmap + pos_start, rawdev->mmap_current_size - pos_start,
						elphel_en.iov_base, elphel_en.iov_len, 1);
				stop_index(*indx, rawdev->mmap_offset + pos_stop + pos_start);
				ret = 0;
			}
		}
		fprintf(debug_file, "\t%s: pos_start = %d, pos_stop = %d\n", __func__, pos_start, pos_stop);
		munmap_disk(rawdev);
	} else {
		fprintf(debug_file, "Error mmaping region from 0x%llx to 0x%llx\n", wnd->from, wnd->to);
	}

	return ret;
}

/**
 * @brief Find a file on disk having time stamp close to the time stamp given.
 * @param[in]   rawdev   pointer to #rawdev_buffer structure containing
 * the current state of raw device buffer
 * @param[in]   idir     pointer to sparse disk index directory where indexes are sorted
 * in time order
 * @param[in]   rawtime  time (in UNIX format) of a possible index candidate
 * @return      A pointer to disk index node found or NULL if there were no close
 * index candidates
 */
static struct disk_index *find_disk_index(rawdev_buffer *rawdev, struct disk_idir *idir, uint64_t *rawtime)
{
	bool indx_appended = false;
	bool process = true;
	struct range range;
	struct range search_window;
	struct disk_index *indx_found = NULL;
	struct disk_index *indx_ret = NULL;
	struct disk_index *nearest_indx = find_nearest_by_time((const struct disk_idir *)idir, *rawtime);

	/* debug code follows */
	struct tm *tm = gmtime(rawtime);
	fprintf(debug_file, "%s: looking for offset near %04d-%02d-%02d %02d:%02d:%02d\n", __func__,
			tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	uint64_t nr;
	nr = (nearest_indx == NULL) ? 0 : nearest_indx->f_offset;
	fprintf(debug_file, "Nearest index offset: 0x%llx\n", nr);
	/* end of debug code */

	// define disk offsets where search will be performed
	if (nearest_indx == NULL) {
		range.from = rawdev->start_pos;
		range.to = rawdev->end_pos;
	} else {
		if (*rawtime > nearest_indx->rawtime) {
			range.from = nearest_indx->f_offset;
			if (nearest_indx->next != NULL)
				range.to = nearest_indx->next->f_offset;
			else
				range.to = rawdev->end_pos;
		} else {
			range.to = nearest_indx->f_offset;
			if (nearest_indx->prev != NULL)
				range.from = nearest_indx->prev->f_offset;
			else
				range.from = rawdev->start_pos;
		}
	}

	fprintf(debug_file, "Starting search in range: from 0x%llx, to 0x%llx\n", range.from, range.to);

	while (process && get_search_window(&range, &search_window) == 0) {
		indx_found = NULL;
		indx_appended = false;
		fprintf(debug_file, "Search window: from 0x%llx, to 0x%llx\n", search_window.from, search_window.to);
		if (find_in_window(rawdev, &search_window, &indx_found) == 0) {
			double time_diff = difftime(indx_found->rawtime, *rawtime);
			fprintf(debug_file, "Index found: time diff %f, file offset 0x%llx, rawtime found %ld, rawtime given %ld\n",
					time_diff, indx_found->f_offset, indx_found->rawtime, *rawtime);
			if (fabs(time_diff) > SEARCH_TIME_WINDOW) {
				// the index found is not within search time window, update sparse index directory and
				// define a new search window
				if (time_diff > 0) {
					range.to = search_window.from;
				} else {
					range.from = search_window.to;
				}
			} else {
				// the index found is within search time window, stop search and return
				process = false;
				indx_ret = indx_found;
			}
			insert_node(idir, indx_found);
		} else {
			// index is not found in the search window, move toward the start of the range
			range.to = search_window.from;
		}
		fprintf(debug_file, "Updating range, new range: from 0x%llx, to 0x%llx\n", range.from, range.to);
	}

	fprintf(debug_file, "\nSparse index directory dump, %d nodes:\n", idir->size);
	dump_index_dir(idir);

	return indx_ret;
}

/**
 * @brief Raw device buffer reading function.
 *
 * This function is started in a separated thread right after the application has started. It opens a
 * communication socket, waits for commands sent over the socket and process them.
 * @param[in, out]   arg   pointer to #camogm_state structure
 * @return           None
 * @warning The main processing loop of the function is enclosed in @e pthread_cleanup_push and @e pthread_cleanup_pop
 * calls. The effect of use of normal @b return or @b break to prematurely leave this loop is undefined.
 * @todo print unrecognized command to debug output file
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
	struct disk_idir index_dir;
	struct disk_idir index_sparse;
	struct exit_state exit_state = {
			.state = state,
			.idir = &index_dir,
			.sparse_idir = &index_sparse,
			.sockfd_const = &sockfd,
			.sockfd_temp = &fd
	};
	memset(&index_dir, 0, sizeof(struct disk_index));
	memset(&index_sparse, 0, sizeof(struct disk_index));

	prep_socket(&sockfd, state->sock_port);
	pthread_cleanup_push(exit_thread, &exit_state);
	while (true) {
		fd = accept(sockfd, NULL, 0);
		if (fd == -1)
			continue;
		if (state->prog_state == STATE_STOPPED && state->rawdev_op) {
			pthread_mutex_lock(&state->mutex);
			state->prog_state = STATE_READING;
			pthread_mutex_unlock(&state->mutex);
		} else {
			close(fd);
			D0(fprintf(debug_file, "Can not change state of the program, check settings\n"));
			continue;
		}
		cmd_len = read(fd, cmd_buff, sizeof(cmd_buff) - 1);
		cmd_ptr = cmd_buff;
		trim_command(cmd_ptr, cmd_len);
		while ((cmd = parse_command(&cmd_ptr)) != -2 && state->rawdev.thread_state != STATE_CANCEL) {
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
				while (disk_chunks > 0 && transfer && state->rawdev.thread_state != STATE_CANCEL) {
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
				if (index_dir.size > 0) {
					struct disk_index indx;
					if (get_indx_args(cmd_ptr, &indx) > 0 &&
							(disk_indx = find_by_offset(&index_dir, indx.f_offset)) != NULL){
						send_file(rawdev, disk_indx, fd);
					}
				}
				break;
			case CMD_FIND_FILE: {
				struct disk_index indx;
				struct disk_index *indx_ptr = NULL;
				if (get_timestamp_args(cmd_ptr, &indx) > 0) {
					if (index_dir.size == 0) {
						indx_ptr = find_disk_index(rawdev, &index_sparse, &indx.rawtime);
						if (indx_ptr != NULL)
							index_sparse.curr_indx = indx_ptr;
					} else {
						indx_ptr = find_nearest_by_time(&index_dir, indx.rawtime);
						/* debug code follows */
						if (indx_ptr != NULL)
							printf("Index found in pre-built index directory: offset = 0x%llx\n", indx_ptr->f_offset);
						else
							printf("Index NOT found in pre-built index directory. Probably it should be rebuilt\n");
						/* end of debug code */
					}
					if (indx_ptr != NULL)
						send_file(rawdev, indx_ptr, fd);
				}
				break;
			}
			case CMD_NEXT_FILE: {
				struct range rng;
				struct disk_index *new_indx = NULL;
				struct disk_index *indx_ptr = NULL;
				uint64_t len;
				if (index_sparse.curr_indx != NULL) {
					if (index_sparse.curr_indx->next != NULL) {
						len = index_sparse.curr_indx->next->f_offset - index_sparse.curr_indx->f_offset - 1;
						if (len > 0) {
							rng.from = index_sparse.curr_indx->f_offset + index_sparse.curr_indx->f_size + 1;
							rng.to = index_sparse.curr_indx->next->f_offset;
						} else {
							indx_ptr = index_sparse.curr_indx->next;
						}
					} else {
						rng.from = index_sparse.curr_indx->f_offset + index_sparse.curr_indx->f_size;
						rng.to = rawdev->end_pos;
					}
					fprintf(debug_file, "Searching next file in rage from 0x%llx to 0x%llx\n", rng.from, rng.to);
					if (indx_ptr == NULL) {
						rng.from &= PAGE_BOUNDARY_MASK;
						if (rng.to - rng.from > rawdev->mmap_default_size)
							rng.to = rng.from + rawdev->mmap_default_size;
						if (find_in_window(rawdev, &rng, &new_indx) == 0) {
							insert_next(&index_sparse, index_sparse.curr_indx, new_indx);
							send_file(rawdev, new_indx, fd);
							index_sparse.curr_indx = new_indx;
						}
					} else {
						send_file(rawdev, indx_ptr, fd);
					}
				}
				break;
			}
			case CMD_PREV_FILE: {
				break;
			}
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
					while (file_cntr < index_dir.size && disk_indx != NULL && state->rawdev.thread_state != STATE_CANCEL) {
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
		pthread_mutex_lock(&state->mutex);
		state->prog_state = STATE_STOPPED;
		pthread_mutex_unlock(&state->mutex);
		usleep(COMMAND_LOOP_DELAY);
	}
	pthread_cleanup_pop(0);

	return (void *) 0;
}

/**
 * @brief Clean up after the reading thread is closed. This function is thread-cancellation handler and it is
 * passed to @e pthread_cleanup_push() function.
 * @param[in]   arg   pointer to #exit_state structure containing resources that
 * should be closed
 * @return      None
 */
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
	if (s->sparse_idir-> size != 0)
		delete_idir(s->sparse_idir);
	if (fstat(*s->sockfd_const, &buff) != EBADF)
		close(*s->sockfd_const);
	if (fstat(*s->sockfd_temp, &buff) != EBADF)
		close(*s->sockfd_temp);
}

/**
 * @brief Extract the position and parameters of JPEG files in raw device buffer and
 * build disk index directory for further file extraction.
 *
 * Data from raw device is read to a buffer in #PHY_BLK_SZ blocks. The buffer is
 * then analyzed for JPEG markers stored in #elphelst and #elphelen arrays. The offsets and
 * sizes of the files found in the buffer are recorded to disk index directory.
 * @param[in]   state   a pointer to a structure containing current state
 * @param[out]  idir    a pointer to disk index directory. This directory will contain
 * offset of the files found in the raw device buffer.
 * @return      None
 * @todo reorder decision tree
 */
static void build_index(camogm_state *state, struct disk_idir *idir)
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
	struct disk_index *node = NULL;

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
	while (process && state->rawdev.thread_state != STATE_CANCEL) {
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

				node = NULL;
				if (pos_start == MATCH_NOT_FOUND && pos_stop == MATCH_NOT_FOUND) {
					// normal condition, search in progress
					buff_processed = 1;
//					D6(fprintf(debug_file, "State 'skip data'\n"));
				} else if (pos_start >= 0 && pos_stop == MATCH_NOT_FOUND && search_state == SEARCH_SKIP) {
					// normal condition, new file found
					search_state = SEARCH_FILE_DATA;
					state->rawdev.file_start = dev_curr_pos + pos_start + (save_from - active_buff);
					idir_result = read_index(&state->rawdev, &node);
					if (idir_result == 0)
						add_node(idir, node);
					buff_processed = 1;
					D6(fprintf(debug_file, "New file found. File start position: %llu\n", state->rawdev.file_start));
					D6(fprintf(debug_file, "State 'starting file'\n"));
				} else if (pos_start >= 0 && pos_stop == MATCH_NOT_FOUND && search_state == SEARCH_FILE_DATA) {
					// error condition (normally should not happen), discard current index and start a new one
					buff_processed = 1;
					remove_node(idir, idir->tail);
					if (zero_cross == 0) {
						state->rawdev.file_start = dev_curr_pos + pos_start + (save_from - active_buff);
						idir_result = read_index(&state->rawdev, &node);
						if (idir_result == 0)
							add_node(idir, node);
					} else {
						process = 0;
					}
					D6(fprintf(debug_file, "State 'abnormal start marker, remove current disk index from directory and skip data'\n"));
				} else if (pos_start == MATCH_NOT_FOUND && pos_stop >= 0 &&
						search_state == SEARCH_FILE_DATA) {
					// normal condition, save current file size to index directory
					uint64_t disk_pos = dev_curr_pos + pos_stop + (save_from - active_buff);
					search_state = SEARCH_SKIP;
					idir_result = stop_index(idir->tail, disk_pos);
					buff_processed = 1;
					if (zero_cross)
						process = 0;
					D6(fprintf(debug_file, "State 'finishing file'\n"));
				} else if (pos_start == MATCH_NOT_FOUND && pos_stop >= 0 && search_state == SEARCH_SKIP) {
					// error condition (normally should not happen), drop current read buffer and do nothing
					buff_processed = 1;
					D6(fprintf(debug_file, "State 'abnormal stop marker, skip data'\n"));
				} else if (pos_start >= 0 && pos_stop >= 0 && pos_start > pos_stop) {
					// normal condition, start marker following stop marker found - this indicates a new file
					if (search_state == SEARCH_FILE_DATA) {
						uint64_t disk_pos = dev_curr_pos + pos_stop + (save_from - active_buff);
						idir_result = stop_index(idir->tail, disk_pos);
					}
					if (zero_cross == 0) {
						state->rawdev.file_start = dev_curr_pos + pos_start + (save_from - active_buff);
						idir_result = read_index(&state->rawdev, &node);
						if (idir_result == 0)
							add_node(idir, node);
						search_state = SEARCH_FILE_DATA;
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
						idir_result = read_index(&state->rawdev, &node);
						if (idir_result == 0)
							add_node(idir, node);
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
						idir_result = stop_index(idir->tail, disk_pos);
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
