/** @file camogm_align.c
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

/** @brief This define is needed to use lseek64 and should be set before includes */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/uio.h>

#include "camogm_align.h"

static unsigned char app15[ALIGNMENT_SIZE] = {0xff, 0xef};

static inline size_t get_size_from(const struct iovec *vects, int index, size_t offset, int all);
static inline size_t align_bytes_num(size_t data_len, size_t align_len);
static inline void vectcpy(struct iovec *dest, void *src, size_t len);
static inline void vectshrink(struct iovec *vec, size_t len);
static inline unsigned char *vectrpos(struct iovec *vec, size_t offset);
static void dev_dbg(const char *prefix, const char *format, ...);
static void remap_vectors(camogm_state *state, struct iovec *chunks);
static size_t get_blocks_num(struct iovec *sgl, size_t n_elem);
/* debug functions */
static int check_chunks(struct iovec *vects);

/** Replace debug function with the same name from driver code with debug macro to reduce changes */
static void dev_dbg(const char *prefix, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	D6(vfprintf(debug_file, format, args));
	va_end(args);
}

/** Copy @e len bytes from buffer pointed by @e src vector to buffer pointed by @e dest vector */
static inline void vectcpy(struct iovec *dest, void *src, size_t len)
{
	unsigned char *d = (unsigned char *)dest->iov_base;

	memcpy(d + dest->iov_len, src, len);
	dest->iov_len += len;
}

/** Shrink vector length by @len bytes */
static inline void vectshrink(struct iovec *vec, size_t len)
{
	if (vec->iov_len >= len) {
		vec->iov_len -= len;
	}
}

/** This helper function is used to position a pointer @e offset bytes from the end
 * of a buffer. */
static inline unsigned char *vectrpos(struct iovec *vec, size_t offset)
{
	return (unsigned char *)vec->iov_base + (vec->iov_len - offset);
}

/** Calculate the size of current frame in bytes starting from vector and offset given */
static inline size_t get_size_from(const struct iovec *vects, int index, size_t offset, int all)
{
	int i;
	size_t total = 0;

	if (index >= MAX_DATA_CHUNKS || offset > vects[index].iov_len) {
		return 0;
	}

	for (i = index; i < MAX_DATA_CHUNKS; i++) {
		if (i == CHUNK_REM && all == EXCLUDE_REM)
			/* remainder should not be processed */
			continue;
		if (i == index)
			total += vects[i].iov_len - offset;
		else
			total += vects[i].iov_len;
	}

	return total;
}

/** Return the number of bytes needed to align @e data_len to @e align_len boundary */
static inline size_t align_bytes_num(size_t data_len, size_t align_len)
{
	size_t rem = data_len % align_len;
	if (rem == 0)
		return 0;
	else
		return align_len - rem;
}

static void remap_vectors(camogm_state *state, struct iovec *chunks)
{
	int chunk_index = 1;

	if (state->exif > 0) {
		chunks[CHUNK_LEADER].iov_base = (void *)state->packetchunks[chunk_index].chunk;
		chunks[CHUNK_LEADER].iov_len = state->packetchunks[chunk_index++].bytes;
		chunks[CHUNK_EXIF].iov_base = (void *)state->packetchunks[chunk_index].chunk;
		chunks[CHUNK_EXIF].iov_len = state->packetchunks[chunk_index++].bytes;
		chunks[CHUNK_HEADER].iov_base = (void *)state->packetchunks[chunk_index].chunk;
		chunks[CHUNK_HEADER].iov_len = state->packetchunks[chunk_index++].bytes;
	} else {
		chunks[CHUNK_LEADER].iov_base = (void *)state->packetchunks[chunk_index].chunk;
		chunks[CHUNK_LEADER].iov_len = JPEG_MARKER_LEN;
		chunks[CHUNK_HEADER].iov_base = (void *)(state->packetchunks[chunk_index].chunk + JPEG_MARKER_LEN);
		chunks[CHUNK_HEADER].iov_len = state->packetchunks[chunk_index++].bytes - JPEG_MARKER_LEN;
	}
	chunks[CHUNK_DATA_0].iov_base = (void *)state->packetchunks[chunk_index].chunk;
	chunks[CHUNK_DATA_0].iov_len = state->packetchunks[chunk_index++].bytes;
	if (state->writer_params.segments == 2) {
		chunks[CHUNK_DATA_0].iov_base = (void *)state->packetchunks[chunk_index].chunk;
		chunks[CHUNK_DATA_0].iov_len = state->packetchunks[chunk_index++].bytes;
	}
	chunks[CHUNK_TRAILER].iov_base = (void *)state->packetchunks[chunk_index].chunk;
	chunks[CHUNK_TRAILER].iov_len = state->packetchunks[chunk_index].bytes;

	/* some data may be left from previous frame, copy it to special buffer */
	if (chunks[CHUNK_REM].iov_len != 0) {
		vectcpy(&state->writer_params.prev_rem_vect, chunks[CHUNK_REM].iov_base, chunks[CHUNK_REM].iov_len);
		vectshrink(&chunks[CHUNK_REM], chunks[CHUNK_REM].iov_len);
	}
}

/** Debug function, checks frame alignment */
static int check_chunks(struct iovec *vects)
{
	int i;
	int ret = 0;
	size_t sz = 0;
	for (i = 0; i < MAX_DATA_CHUNKS; i++) {
		if (i != CHUNK_REM) {
			sz += vects[i].iov_len;
			if ((vects[i].iov_len % ALIGNMENT_SIZE) != 0) {
				dev_dbg(NULL, "ERROR: unaligned write from slot %d, length %u\n", i, vects[i].iov_len);
				ret = -1;
			}
		}
		dev_dbg(NULL, "chunk[%d]: ptr = %p, size = %d\n", i, vects[i].iov_base, vects[i].iov_len);
	}
	if ((sz % PHY_BLOCK_SIZE) != 0) {
		dev_dbg(NULL, "ERROR: total length of the transaction is not aligned to sector boundary, total length %u\n", sz);
		ret = -1;
	} else {
		dev_dbg(NULL, "===== frame is OK =====\n");
	}
	return ret;
}

/** Calculate the number of blocks this frame will occupy. The frame must be aligned to block size */
static size_t get_blocks_num(struct iovec *sgl, size_t n_elem)
{
	int num;
	size_t total = 0;

	for (num = 0; num < n_elem; num++) {
		total += sgl[num].iov_len;
	}

	return total / PHY_BLOCK_SIZE;
}

int init_align_buffers(camogm_state *state)
{
	state->writer_params.data_chunks = (struct iovec *)malloc(MAX_DATA_CHUNKS * sizeof(struct iovec));
	if (state->writer_params.data_chunks == NULL) {
		return -1;
	}
	state->writer_params.common_buff = (unsigned char *)malloc(COMMON_BUFF_SZ);
	if (state->writer_params.common_buff == NULL) {
		deinit_align_buffers(state);
		return -1;
	}
	state->writer_params.rem_buff = (unsigned char *)malloc(REM_BUFF_SZ);
	if (state->writer_params.rem_buff == NULL) {
		deinit_align_buffers(state);
		return -1;
	}
	state->writer_params.prev_rem_buff = (unsigned char *)malloc(REM_BUFF_SZ);
	if (state->writer_params.prev_rem_buff == NULL) {
		deinit_align_buffers(state);
		return -1;
	}

	state->writer_params.data_chunks[CHUNK_COMMON].iov_base = (void *)state->writer_params.common_buff;
	state->writer_params.data_chunks[CHUNK_COMMON].iov_len = 0;

	state->writer_params.data_chunks[CHUNK_REM].iov_base = (void *)state->writer_params.rem_buff;
	state->writer_params.data_chunks[CHUNK_REM].iov_len = 0;

	state->writer_params.prev_rem_vect.iov_base = (void *)state->writer_params.prev_rem_buff;
	state->writer_params.prev_rem_vect.iov_len = 0;

	return 0;
}

void deinit_align_buffers(camogm_state *state)
{
	struct writer_params *params = &state->writer_params;

	if (params->data_chunks) {
		free(params->data_chunks);
		params->data_chunks = NULL;
	}
	if (params->common_buff) {
		free(params->common_buff);
		params->common_buff = NULL;
	}
	if (params->rem_buff) {
		free(params->rem_buff);
		params->rem_buff = NULL;
	}
	if (params->prev_rem_buff) {
		free(params->prev_rem_buff);
		params->prev_rem_buff = NULL;
	}
}

/** Align current frame to disk sector boundary and each individual buffer to #ALIGNMENT_SIZE boundary */
void align_frame(camogm_state *state)
{
	const char *dev = NULL;
	unsigned char *src;
	size_t len, total_sz, data_len;
	struct iovec *chunks = state->writer_params.data_chunks;
	struct iovec *cbuff = &chunks[CHUNK_COMMON];
	struct iovec *rbuff = &state->writer_params.prev_rem_vect;

	remap_vectors(state, chunks);

	total_sz = get_size_from(chunks, 0, 0, INCLUDE_REM) + rbuff->iov_len;
	if (total_sz < PHY_BLOCK_SIZE) {
		/* the frame length is less than sector size, delay this frame */
		if (rbuff->iov_len != 0) {
			/* some data may be left from previous frame */
			vectcpy(&chunks[CHUNK_REM], rbuff->iov_base, rbuff->iov_len);
			vectshrink(rbuff, rbuff->iov_len);
		}
		dev_dbg(dev, "frame size is less than sector size: %u bytes; delay recording\n", total_sz);
		vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_LEADER].iov_base, chunks[CHUNK_LEADER].iov_len);
		vectshrink(&chunks[CHUNK_LEADER], chunks[CHUNK_LEADER].iov_len);
		vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_EXIF].iov_base, chunks[CHUNK_EXIF].iov_len);
		vectshrink(&chunks[CHUNK_EXIF], chunks[CHUNK_EXIF].iov_len);
		vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_HEADER].iov_base, chunks[CHUNK_HEADER].iov_len);
		vectshrink(&chunks[CHUNK_HEADER], chunks[CHUNK_HEADER].iov_len);
		vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_DATA_0].iov_base, chunks[CHUNK_DATA_0].iov_len);
		vectshrink(&chunks[CHUNK_DATA_0], chunks[CHUNK_DATA_0].iov_len);
		vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_DATA_1].iov_base, chunks[CHUNK_DATA_1].iov_len);
		vectshrink(&chunks[CHUNK_DATA_1], chunks[CHUNK_DATA_1].iov_len);
		vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_TRAILER].iov_base, chunks[CHUNK_TRAILER].iov_len);
		vectshrink(&chunks[CHUNK_TRAILER], chunks[CHUNK_TRAILER].iov_len);
		return;
	}

	/* copy remainder of previous frame to the beginning of common buffer */
	if (rbuff->iov_len != 0) {
		len = rbuff->iov_len;
		dev_dbg(dev, "copy %u bytes from REM to common buffer\n", len);
		vectcpy(cbuff, rbuff->iov_base, len);
		vectshrink(rbuff, rbuff->iov_len);
	}

	/* copy JPEG marker */
	len = chunks[CHUNK_LEADER].iov_len;
	vectcpy(cbuff, chunks[CHUNK_LEADER].iov_base, len);
	vectshrink(&chunks[CHUNK_LEADER], chunks[CHUNK_LEADER].iov_len);

	/* copy Exif if present */
	if (chunks[CHUNK_EXIF].iov_len != 0) {
		len = chunks[CHUNK_EXIF].iov_len;
		dev_dbg(dev, "copy %u bytes from EXIF to common buffer\n", len);
		vectcpy(cbuff, chunks[CHUNK_EXIF].iov_base, len);
		vectshrink(&chunks[CHUNK_EXIF], chunks[CHUNK_EXIF].iov_len);
	}

	/* align common buffer to ALIGNMENT boundary, APP15 marker should be placed before header data */
	data_len = cbuff->iov_len + chunks[CHUNK_HEADER].iov_len;
	len = align_bytes_num(data_len, ALIGNMENT_SIZE);
	if (len < (JPEG_MARKER_LEN + JPEG_SIZE_LEN) && len != 0) {
		/* the number of bytes needed for alignment is less than the length of the marker itself, increase the number of stuffing bytes */
		len += ALIGNMENT_SIZE;
	}
	dev_dbg(dev, "total number of stuffing bytes in APP15 marker: %u\n", len);
	app15[3] = len - JPEG_MARKER_LEN;
	vectcpy(cbuff, app15, len);

	/* copy JPEG header */
	len = chunks[CHUNK_HEADER].iov_len;
	dev_dbg(dev, "copy %u bytes from HEADER to common buffer\n", len);
	vectcpy(cbuff, chunks[CHUNK_HEADER].iov_base, len);
	vectshrink(&chunks[CHUNK_HEADER], chunks[CHUNK_HEADER].iov_len);

	/* check if there is enough data to continue - JPEG data length can be too short */
	len = get_size_from(chunks, CHUNK_DATA_0, 0, EXCLUDE_REM);
	if (len < PHY_BLOCK_SIZE) {
		size_t num = align_bytes_num(cbuff->iov_len, PHY_BLOCK_SIZE);
		dev_dbg(dev, "jpeg data is too short, delay this frame\n");
		if (len >= num) {
			/* there is enough data to align common buffer to sector boundary */
			if (num >= chunks[CHUNK_DATA_0].iov_len) {
				vectcpy(cbuff, chunks[CHUNK_DATA_0].iov_base, chunks[CHUNK_DATA_0].iov_len);
				num -= chunks[CHUNK_DATA_0].iov_len;
				vectshrink(&chunks[CHUNK_DATA_0], chunks[CHUNK_DATA_0].iov_len);
			} else {
				src = vectrpos(&chunks[CHUNK_DATA_0], num);
				vectcpy(cbuff, chunks[CHUNK_DATA_0].iov_base, num);
				vectshrink(&chunks[CHUNK_DATA_0], num);
				num = 0;
			}
			if (num >= chunks[CHUNK_DATA_1].iov_len) {
				vectcpy(cbuff, chunks[CHUNK_DATA_1].iov_base, chunks[CHUNK_DATA_1].iov_len);
				num -= chunks[CHUNK_DATA_1].iov_len;
				vectshrink(&chunks[CHUNK_DATA_1], chunks[CHUNK_DATA_1].iov_len);
			} else {
				src = vectrpos(&chunks[CHUNK_DATA_1], num);
				vectcpy(cbuff, chunks[CHUNK_DATA_1].iov_base, num);
				vectshrink(&chunks[CHUNK_DATA_1], num);
				num = 0;
			}
			if (num >= chunks[CHUNK_TRAILER].iov_len) {
				vectcpy(cbuff, chunks[CHUNK_TRAILER].iov_base, chunks[CHUNK_TRAILER].iov_len);
				num -= chunks[CHUNK_TRAILER].iov_len;
				vectshrink(&chunks[CHUNK_TRAILER], chunks[CHUNK_TRAILER].iov_len);
			} else {
				src = vectrpos(&chunks[CHUNK_TRAILER], num);
				vectcpy(cbuff, chunks[CHUNK_TRAILER].iov_base, num);
				vectshrink(&chunks[CHUNK_TRAILER], num);
				num = 0;
			}
		} else {
			/* there is not enough data to align common buffer to sector boundary, truncate common buffer */
			data_len = cbuff->iov_len % PHY_BLOCK_SIZE;
			src = vectrpos(cbuff, data_len);
			vectcpy(&chunks[CHUNK_REM], src, data_len);
			vectshrink(cbuff, data_len);
		}
		vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_DATA_0].iov_base, chunks[CHUNK_DATA_0].iov_len);
		vectshrink(&chunks[CHUNK_DATA_0], chunks[CHUNK_DATA_0].iov_len);
		vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_DATA_1].iov_base, chunks[CHUNK_DATA_1].iov_len);
		vectshrink(&chunks[CHUNK_DATA_1], chunks[CHUNK_DATA_1].iov_len);
		vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_TRAILER].iov_base, chunks[CHUNK_TRAILER].iov_len);
		vectshrink(&chunks[CHUNK_TRAILER], chunks[CHUNK_TRAILER].iov_len);

		return;
	}

	/* align frame to sector size boundary; total size could have changed by the moment - recalculate */
	total_sz = get_size_from(chunks, 0, 0, INCLUDE_REM);
	len = total_sz % PHY_BLOCK_SIZE;
	dev_dbg(dev, "number of bytes crossing sector boundary: %u\n", len);
	if (len != 0) {
		if (len >= (chunks[CHUNK_DATA_1].iov_len + chunks[CHUNK_TRAILER].iov_len)) {
			/* current frame is not split or the second part of JPEG data is too short */
			data_len = len - chunks[CHUNK_DATA_1].iov_len - chunks[CHUNK_TRAILER].iov_len;
			src = vectrpos(&chunks[CHUNK_DATA_0], data_len);
			vectcpy(&chunks[CHUNK_REM], src, data_len);
			vectshrink(&chunks[CHUNK_DATA_0], data_len);
			vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_DATA_1].iov_base, chunks[CHUNK_DATA_1].iov_len);
			vectshrink(&chunks[CHUNK_DATA_1], chunks[CHUNK_DATA_1].iov_len);
			vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_TRAILER].iov_base, chunks[CHUNK_TRAILER].iov_len);
			vectshrink(&chunks[CHUNK_TRAILER], chunks[CHUNK_TRAILER].iov_len);
		} else if (len >= chunks[CHUNK_TRAILER].iov_len) {
			/* there is enough data in second part to align the frame */
			data_len = len - chunks[CHUNK_TRAILER].iov_len;
			src = vectrpos(&chunks[CHUNK_DATA_1], data_len);
			vectcpy(&chunks[CHUNK_REM], src, data_len);
			vectshrink(&chunks[CHUNK_DATA_1], data_len);
			vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_TRAILER].iov_base, chunks[CHUNK_TRAILER].iov_len);
			vectshrink(&chunks[CHUNK_TRAILER], chunks[CHUNK_TRAILER].iov_len);
		} else {
			/* the trailing marker is split by sector boundary, copy (PHY_BLOCK_SIZE - 1) bytes from
			 * JPEG data block(s) to remainder buffer and then add trailing marker */
			data_len = PHY_BLOCK_SIZE - (chunks[CHUNK_TRAILER].iov_len - len);
			if (data_len >= chunks[CHUNK_DATA_1].iov_len) {
				size_t cut_len = data_len - chunks[CHUNK_DATA_1].iov_len;
				src = vectrpos(&chunks[CHUNK_DATA_0], cut_len);
				vectcpy(&chunks[CHUNK_REM], src, cut_len);
				vectshrink(&chunks[CHUNK_DATA_0], cut_len);
				vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_DATA_1].iov_base, chunks[CHUNK_DATA_1].iov_len);
				vectshrink(&chunks[CHUNK_DATA_1], chunks[CHUNK_DATA_1].iov_len);
				vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_TRAILER].iov_base, chunks[CHUNK_TRAILER].iov_len);
				vectshrink(&chunks[CHUNK_TRAILER], chunks[CHUNK_TRAILER].iov_len);
			} else {
				src = vectrpos(&chunks[CHUNK_DATA_1], data_len);
				vectcpy(&chunks[CHUNK_REM], src, data_len);
				vectshrink(&chunks[CHUNK_DATA_1], data_len);
				vectcpy(&chunks[CHUNK_REM], chunks[CHUNK_TRAILER].iov_base, chunks[CHUNK_TRAILER].iov_len);
				vectshrink(&chunks[CHUNK_TRAILER], chunks[CHUNK_TRAILER].iov_len);
			}
		}
	} else {
		/* the frame is aligned to sector boundary but some buffers may be not */
		chunks[CHUNK_ALIGN].iov_base = vectrpos(cbuff, 0);
//		chunks[CHUNK_ALIGN].iov_dma = cbuff->iov_dma + cbuff->iov_len;
		chunks[CHUNK_ALIGN].iov_len = 0;
		if (chunks[CHUNK_DATA_1].iov_len == 0) {
			data_len = chunks[CHUNK_DATA_0].iov_len % ALIGNMENT_SIZE;
			src = vectrpos(&chunks[CHUNK_DATA_0], data_len);
			vectcpy(&chunks[CHUNK_ALIGN], src, data_len);
			vectshrink(&chunks[CHUNK_DATA_0], data_len);
		} else {
			data_len = chunks[CHUNK_DATA_1].iov_len % ALIGNMENT_SIZE;
			src = vectrpos(&chunks[CHUNK_DATA_1], data_len);
			vectcpy(&chunks[CHUNK_ALIGN], src, data_len);
			vectshrink(&chunks[CHUNK_DATA_1], data_len);
		}
		vectcpy(&chunks[CHUNK_ALIGN], chunks[CHUNK_TRAILER].iov_base, chunks[CHUNK_TRAILER].iov_len);
		vectshrink(&chunks[CHUNK_TRAILER], chunks[CHUNK_TRAILER].iov_len);
	}

	/* debug sanity check, should not happen */
	if (cbuff->iov_len >= COMMON_BUFF_SZ) {
		dev_dbg(NULL, "ERROR: the number of bytes copied to common buffer exceeds its size\n");
	}
	check_chunks(chunks);
}

/** Discard buffer pointers which makes the command slot marked as empty */
void reset_chunks(struct iovec *vects, int all)
{
	int i;

	for (i = 0; i < MAX_DATA_CHUNKS; i++) {
		if (i != CHUNK_REM)
			vects[i].iov_len = 0;
	}
	if (all) {
		vects[CHUNK_REM].iov_len = 0;
	}
}

/** Calculate and update LBA offsets, do not count remainder buffer. Return 1 if file position should be reset to the start */
int update_lba(camogm_state *state)
{
	int ret = 0;
	size_t total_sz;
	struct iovec *chunks = state->writer_params.data_chunks;

	total_sz = get_blocks_num(chunks, MAX_DATA_CHUNKS - 1);
	if (state->writer_params.lba_current + total_sz <= state->writer_params.lba_end) {
		state->writer_params.lba_current += total_sz;
	} else {
		state->writer_params.lba_current = state->writer_params.lba_start;
		ret = 1;
	}

	return ret;
}

/** Go through all data buffers and pick only mapped ones excluding remainder buffer */
int get_data_buffers(camogm_state *state, struct iovec *mapped, size_t mapped_sz)
{
	int ret = 0;
	struct iovec *all = state->writer_params.data_chunks;

	if (mapped_sz <= 0)
		return ret;

	for (int i = 0, j = 0; i < MAX_DATA_CHUNKS; i++) {
		if (i != CHUNK_REM && all[i].iov_len != 0) {
			if (j < mapped_sz) {
				mapped[j++] = all[i];
				ret = j;
			} else {
				ret = -1;
				break;
			}
		}
	}

	return ret;
}

/** Prepare the last remaining block of data for recording, return the number of bytes ready for recording */
int prep_last_block(camogm_state *state)
{
	int ret = 0;
	size_t stuff_len;
	unsigned char *src;
	struct iovec *cvect = &state->writer_params.data_chunks[CHUNK_COMMON];
	struct iovec *rvect = &state->writer_params.data_chunks[CHUNK_REM];

	if (rvect->iov_len != 0) {
		stuff_len = PHY_BLOCK_SIZE - rvect->iov_len;
		src = vectrpos(rvect, 0);
		memset(src, 0, stuff_len);
		rvect->iov_len += stuff_len;
		ret = rvect->iov_len;
		vectcpy(cvect, rvect->iov_base, rvect->iov_len);
		vectshrink(rvect, rvect->iov_len);
	}

	return ret;
}

off64_t lba_to_offset(uint64_t lba)
{
	return lba * PHY_BLOCK_SIZE;
}
