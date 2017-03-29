/** @file camogm_align.h
 * @brief Provides frame alignment functions use for recording to block device.
 * @copyright Copyright (C) 2017 Elphel, Inc.
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

#ifndef _CAMOGM_ALIGN_H
#define _CAMOGM_ALIGN_H

#include <unistd.h>
#include <sys/types.h>

#include "camogm.h"

#define PHY_BLOCK_SIZE            512            ///< Physical disk block size
#define JPEG_MARKER_LEN           2              ///< The size in bytes of JPEG marker
#define JPEG_SIZE_LEN             2              ///< The size in bytes of JPEG marker length field
#define INCLUDE_REM               1              ///< Include REM buffer to total size calculation
#define EXCLUDE_REM               0              ///< Exclude REM buffer from total size calculation
#define MAX_DATA_CHUNKS           9              ///< An array or JPEG frame chunks contains pointers to JPEG leading marker,
                                                 ///< JPEG header, Exif data if present, stuffing bytes chunk which aligns
                                                 ///< the frame size to disk sector boundary, JPEG data which
                                                 ///< can be split into two chunks, align buffers, JPEG
                                                 ///< trailing marker, and pointer to a buffer containing the remainder of a
                                                 ///< frame. Nine chunks of data in total.
#define ALIGNMENT_SIZE            32             ///< Align buffers length to this amount of bytes
/** Common buffer should be large enough to contain JPEG header, Exif, some alignment bytes and remainder from previous frame */
#define COMMON_BUFF_SZ            MAX_EXIF_SIZE + JPEG_HEADER_MAXSIZE + ALIGNMENT_SIZE + 2 * PHY_BLOCK_SIZE
#define REM_BUFF_SZ               2 * PHY_BLOCK_SIZE

///** This structure holds raw device buffer pointers */
//struct drv_pointers {
//	uint64_t lba_start;                          ///< raw buffer starting LBA
//	uint64_t lba_end;                            ///< raw buffer ending LBA
//	uint64_t lba_write;                          ///< current write pointer inside raw buffer
//	uint16_t wr_count;                           ///< the number of LBA to write next time
//};

/** Container structure for frame buffers */
//struct frame_buffers {
//	struct fvec exif_buff;                       ///< Exif buffer
//	struct fvec jpheader_buff;                   ///< JPEG header buffer
//	struct fvec trailer_buff;                    ///< buffer for trailing marker
//	struct fvec common_buff;                     ///< common buffer where other parts are combined
//	struct fvec rem_buff;                        ///< remainder from previous frame
//};

/** Symbolic names for slots in buffer pointers. Buffer alignment function relies on the order of these names, so
 * new names can be added but the overall order should not be changed */
enum {
	CHUNK_LEADER,                                ///< pointer to JPEG leading marker
	CHUNK_EXIF,                                  ///< pointer to Exif buffer
	CHUNK_HEADER,                                ///< pointer to JPEG header data excluding leading marker
	CHUNK_COMMON,                                ///< pointer to common buffer
	CHUNK_DATA_0,                                ///< pointer to JPEG data
	CHUNK_DATA_1,                                ///< pointer to the second half of JPEG data if a frame crosses circbuf boundary
	CHUNK_TRAILER,                               ///< pointer to JPEG trailing marker
	CHUNK_ALIGN,                                 ///< pointer to buffer where the second part of JPEG data should be aligned
	CHUNK_REM                                    ///< pointer to buffer containing the remainder of current frame. It will be recorded during next transaction
};

int init_align_buffers(camogm_state *state);
void deinit_align_buffers(camogm_state *state);
void align_frame(camogm_state *state);
void reset_chunks(struct iovec *vects, int all);
int update_lba(camogm_state *state);
int get_data_buffers(camogm_state *state, struct iovec *mapped, size_t all_sz);
int prep_last_block(camogm_state *state);
off64_t lba_to_offset(uint64_t lba);

#endif /* _CAMOGM_ALIGN_H */
