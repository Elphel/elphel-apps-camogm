/*!***************************************************************************
   *! FILE NAME  : camogm_jpeg.c
   *! DESCRIPTION: Provides writing to series of individual JPEG files for camogm
   *! Copyright (C) 2007 Elphel, Inc.
   *! -----------------------------------------------------------------------------**
   *!  This program is free software: you can redistribute it and/or modify
   *!  it under the terms of the GNU General Public License as published by
   *!  the Free Software Foundation, either version 3 of the License, or
   *!  (at your option) any later version.
   *!
   *!  This program is distributed in the hope that it will be useful,
   *!  but WITHOUT ANY WARRANTY; without even the implied warranty of
   *!  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   *!  GNU General Public License for more details.
   *!
   *!  You should have received a copy of the GNU General Public License
   *!  along with this program.  If not, see <http://www.gnu.org/licenses/>.
   *! -----------------------------------------------------------------------------**
   *!
   *!  $Log: camogm_jpeg.c,v $
   *!  Revision 1.2  2009/02/25 17:50:51  spectr_rain
   *!  removed deprecated dependency
   *!
   *!  Revision 1.1.1.1  2008/11/27 20:04:01  elphel
   *!
   *!
   *!  Revision 1.3  2008/04/11 23:09:33  elphel
   *!  modified to handle kml generation
   *!
   *!  Revision 1.2  2007/11/19 03:23:21  elphel
   *!  7.1.5.5 Added support for *.mov files in camogm.
   *!
   *!  Revision 1.1  2007/11/16 08:49:57  elphel
   *!  Initial release of camogm - program to record video/image to the camera hard drive (or other storage)
   *!
 */

#define LARGEFILES64_SOURCE

//!Not all are needed, just copied from the camogm.c
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
//#include <signal.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <errno.h>
#include <sys/types.h>
//#include <sys/socket.h>
//#include <sys/stat.h>
//#include <ctype.h>
//#include <getopt.h>
//#include <time.h>
#include <string.h>

#include <netinet/in.h> /*little <-> big endian ?*/
//#include <sys/mman.h>   /* mmap */
//#include <sys/ioctl.h>

#include <c313a.h>
#include <asm/byteorder.h>
#include <assert.h>

#include <ogg/ogg.h>    // has to be before ogmstreams.h
#include "ogmstreams.h" // move it to <>?

#include "camogm_jpeg.h"

/** @brief Size of iovec structures holding data to be written */
#define IOVEC_SIZE      10

/** @brief File starting marker, contains "stelphel" string in ASCII symbols */
const unsigned char elphelst[] = {0x73, 0x74, 0x65, 0x6c, 0x70, 0x68, 0x65, 0x6c};
/** @brief File ending marker, contains "enelphel" string in ASCII symbols */
const unsigned char elphelen[] = {0x65, 0x6e, 0x65, 0x6c, 0x70, 0x68, 0x65, 0x6c};
static struct iovec start_marker = {
		.iov_base = elphelst,
		.iov_len = sizeof(elphelst)
};
static struct iovec end_marker = {
		.iov_base = elphelen,
		.iov_len = sizeof(elphelen)
};

//! may add something - called first time format is changed to this one (only once) recording is stopped
int camogm_init_jpeg(void)
{
	return 0;
}
void camogm_free_jpeg(void)
{
}

int camogm_start_jpeg(camogm_state *state)
{
//!TODO: make directory if it does not exist (find the last "/" in the state->path
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
		state->ivf = open(state->path_prefix, O_RDWR);
		if (state->ivf < 0) {
			D0(perror(__func__));
			D0(fprintf(debug_file, "Error opening raw device %s\n", state->path));
			return -CAMOGM_FRAME_FILE_ERR;
		}
		D0(fprintf(debug_file, "Open raw device %s; start_pos = %llu, end_pos = %llu, curr_pos = %llu\n", state->path,
				state->rawdev.start_pos, state->rawdev.end_pos, state->rawdev.curr_pos));
		lseek64(state->ivf, state->rawdev.curr_pos, SEEK_SET);
	}
	return 0;
}

int camogm_frame_jpeg(camogm_state *state)
{
	int i, j, split_index;
	int chunks_used = state->chunk_index - 1;
	ssize_t iovlen, l;
	struct iovec chunks_iovec[8];
	unsigned char *split_ptr = NULL;
	long split_cntr;
	long total_len;
	const uint64_t storage_sz = state->rawdev.end_pos - state->rawdev.start_pos;

	if (!state->rawdev_op) {
		l = 0;
		for (i = 0; i < (state->chunk_index) - 1; i++) {
			chunks_iovec[i].iov_base = state->packetchunks[i + 1].chunk;
			chunks_iovec[i].iov_len = state->packetchunks[i + 1].bytes;
			l += chunks_iovec[i].iov_len;
		}

		sprintf(state->path, "%s%010ld_%06ld.jpeg", state->path_prefix, state->this_frame_params.timestamp_sec, state->this_frame_params.timestamp_usec);
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
				state->rawdev.start_pos, state->rawdev.end_pos, state->rawdev.curr_pos, l));
		l = 0;
		split_index = -1;
		for (int i = 0, total_len = 0; i < state->chunk_index - 1; i++) {
			total_len += state->packetchunks[i + 1].bytes;
			if (total_len + state->rawdev.curr_pos > state->rawdev.end_pos) {
				split_index = i;
				chunks_used++;
				D0(fprintf(debug_file, "\n>>> raw storage roll over detected\n"));
				break;
			}
		}
		chunks_iovec[0] = start_marker;
		l += start_marker.iov_len;
		chunks_used++;
		for (int i = 1; i < chunks_used; i++) {
			if (i == split_index) {
				// one of the chunks rolls over the end of the raw storage, split it into two segments and
				// use additional chunk in chunks_iovec for this additional segment
				split_cntr = state->rawdev.end_pos - (l + state->rawdev.curr_pos);
				split_ptr = state->packetchunks[i].chunk + split_cntr;

				// be careful with indexes here
				chunks_iovec[i].iov_base = state->packetchunks[i].chunk;
				chunks_iovec[i].iov_len = split_cntr;
				l += chunks_iovec[i].iov_len;
				chunks_iovec[++i].iov_base = split_ptr + 1;
				chunks_iovec[i].iov_len = state->packetchunks[i].bytes - split_cntr;
				l += chunks_iovec[i].iov_len;
			} else {
				chunks_iovec[i].iov_base = state->packetchunks[i].chunk;
				chunks_iovec[i].iov_len = state->packetchunks[i].bytes;
				l += chunks_iovec[i].iov_len;
			}
		}
		// consider start_marker here and increment chunks_used
		assert(chunks_used < IOVEC_SIZE);
		chunks_iovec[chunks_used] = end_marker;
		l += end_marker.iov_len;
		chunks_used++;

		/* debug code follows */
		fprintf(debug_file, "\n=== raw device write, iovec dump ===\n");
		fprintf(debug_file, "split_cntr = %ld; split_ptr = %p; split_index = %d\n", split_cntr, split_ptr, split_index);
		for (int i = 0; i < chunks_used; i++) {
			fprintf(debug_file, "i = %d; iov_base = %p; iov_len = %u\n", i, chunks_iovec[i].iov_base, chunks_iovec[i].iov_len);
		}
		fprintf(debug_file, "total len = %d\n======\n", l);
		/* end of debug code */

		iovlen = writev(state->ivf, chunks_iovec, chunks_used);
		if (iovlen < l) {
			j = errno;
			D0(fprintf(debug_file, "writev error %d (returned %d, expected %d)\n", j, iovlen, l));
			return -CAMOGM_FRAME_FILE_ERR;
		}
		state->rawdev.curr_pos += l;
		if (state->rawdev.curr_pos > state->rawdev.end_pos)
			state->rawdev.curr_pos = state->rawdev.curr_pos - state->rawdev.end_pos + state->rawdev.start_pos;
		D0(fprintf(debug_file, "%d bytes written, curr_pos = %llu\n", l, state->rawdev.curr_pos));
	}

	return 0;
}

int camogm_end_jpeg(camogm_state *state)
{
	close(state->ivf);
	D0(fprintf(debug_file, "Closing raw device %s\n", state->path_prefix));
	return 0;
}
