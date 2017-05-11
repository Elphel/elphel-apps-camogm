/** @file camogm_mov.c
 * @brief Provides writing to file compatible with Apple Quicktime(R) for @e camogm
 * @copyright Copyright (C) 2016 Elphel, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#include "camogm_mov.h"

/** @brief Quicktime header length (w/o index tables) enough to accommodate static data */
#define QUICKTIME_MIN_HEADER 0x300

// for the parser
const char hexStr[] = "0123456789abcdef";
const char qtSourceFileName[] = "/etc/qt_source";
char comStr[1024];
int width = 1280;
int height = 1024;
int nframes = 100;
int sample_dur = 80;
int samplesPerChunk = 10;
int framesize = 80000;
int timescale = 600;
int * sizes;    // array of frame sizes
int iPos;       // position in the string "iFile"
int ofd;        // output file descriptor (file opened by the caller)
int iFileLen;
char * q_template = NULL;
long headerSize = 0;
const char *iFile = NULL;


int quicktime_template_parser(camogm_state *state,
		const char * i_iFile,     // now - string containing header template
		int i_ofd,                // output file descriptor (opened)
		int i_width,              // width in pixels
		int i_height,
		int i_nframes,
		int i_sample_dur,
		int i_samplesPerChunk,
		int i_framesize,
		int i_timescale,
		int * i_sizes,
		int data_start            // put zero if the header is written before data (no gap)
);
void putBigEndian(unsigned long d, int l);
int parse_special(void);
int parse(camogm_state *state, int top);
static int camogm_audio_mov(void *buff, int len, int slen);

/**
 * @brief Called when format is changed to MOV (only once) and recording is stopped.
 * @param[in]   state   a pointer to a structure containing current state
 * Read frame template from the file if it is not done yet and set callback function for
 * audio stream recording.
 */
int camogm_init_mov(camogm_state *state)
{
	FILE* qt_header;
	int size;

	if ((qt_header = fopen(qtSourceFileName, "r")) == NULL) {
		D0(fprintf(debug_file, "Error opening Quicktime header template %s for reading\n", qtSourceFileName));
		return -CAMOGM_FRAME_FILE_ERR;
	}
	fseek(qt_header, 0, SEEK_END);
	size = ftell(qt_header);
	if (!((q_template = malloc(size + 1)))) {
		D0(fprintf(debug_file, "Could not allocate %d bytes of memory for Quicktime header template\n", (size + 1)));
		fclose(qt_header);
		return -CAMOGM_FRAME_MALLOC;
	}
	fseek(qt_header, 0, SEEK_SET); //rewind
	if (fread(q_template, size, 1, qt_header) < 1) {
		D0(fprintf(debug_file, "Could not read %d bytes of Quicktime header template from %s\n", (size + 1), qtSourceFileName));
		free(q_template);
		q_template = NULL;
		fclose(qt_header);
		return -CAMOGM_FRAME_FILE_ERR;
	}
	q_template[size] = 0;
	state->audio.write_samples = camogm_audio_mov;
	return 0;
}

void camogm_free_mov(void)
{
	if (q_template) {
		free(q_template);
		q_template = NULL;
	}
}

/**
 * @brief Start MOV recording
 * @param[in]   state   a pointer to a structure containing current state
 * @return      0 if recording started successfully and negative error code otherwise
 */
int camogm_start_mov(camogm_state *state)
{

	// allocate memory for the frame index table
	if (!((state->frame_lengths = malloc(4 * state->max_frames)))) return -CAMOGM_FRAME_MALLOC;
	// open file for writing
	sprintf(state->path, "%s%010ld_%06ld.mov", state->path_prefix, state->frame_params[state->port_num].timestamp_sec, state->frame_params[state->port_num].timestamp_usec);
	if (((state->ivf = open(state->path, O_RDWR | O_CREAT, 0777))) < 0) {
		D0(fprintf(debug_file, "Error opening %s for writing, returned %d, errno=%d\n", state->path, state->ivf, errno));
		return -CAMOGM_FRAME_FILE_ERR;
	}
	// skip header (plus extra)
	// Quicktime (and else?) - frame data start (0xff 0xd8...)
	state->frame_data_start = QUICKTIME_MIN_HEADER + 16 + 4 * (state->max_frames) + ( 4 * (state->max_frames)) / (state->frames_per_chunk); // 8 bytes for "skip" tag
	lseek(state->ivf, state->frame_data_start, SEEK_SET);
	return 0;
}

/**
 * @brief Write a frame to file
 * @param[in]   state   a pointer to a structure containing current state
 * @return      0 if frame was saved successfully and negative error code otherwise
 */
int camogm_frame_mov(camogm_state *state)
{
	int i, j;
	ssize_t iovlen, l;
	struct iovec chunks_iovec[7];

	l = 0;
	for (i = 0; i < (state->chunk_index) - 1; i++) {
		chunks_iovec[i].iov_base = state->packetchunks[i + 1].chunk;
		chunks_iovec[i].iov_len = state->packetchunks[i + 1].bytes;
		l += chunks_iovec[i].iov_len;
	}
	iovlen = writev(state->ivf, chunks_iovec, (state->chunk_index) - 1);
	if (iovlen < l) {
		j = errno;
		D0(fprintf(debug_file, "writev error %d (returned %d, expected %d, file descriptor %d, chn %d)\n", j, iovlen, l, state->ivf, state->port_num));
		perror(strerror(j));
		close(state->ivf);
		state->ivf = -1;
		return -CAMOGM_FRAME_FILE_ERR;
	}
	state->frame_lengths[state->frameno] = l;
	return 0;
}

/**
 * Write audio samples to file.
 * @param buff
 * @param len
 * @param slen
 * @return
 */
static int camogm_audio_mov(void *buff, int len, int slen)
{
	int ret_val = 0;

	D6(fprintf(debug_file, "write audio sample, len = %d, slen = %d\n", len, slen));

	return ret_val;
}

/**
 * @brief Move to the start of the file and insert generated header
 * @param[in]   state   pointer to the #camogm_state structure for current sensor port
 * @return      this function is always successful and returns 0
 */
int camogm_end_mov(camogm_state *state)
{
	off_t l;
	int port = state->port_num;
	timescale = 10000;                                                      // frame period measured in 1/10000 of a second?
	// that was in old code. If that works - try to switch to microseconds
	l = lseek(state->ivf, 0, SEEK_CUR) - (state->frame_data_start) + 8;     // 4-byte length+"mdat"
	// fill in the header in the beginning of the file
	lseek(state->ivf, 0, SEEK_SET);
	quicktime_template_parser(state,
			q_template,   // now - string containing header template
			state->ivf,   // output file descriptor (opened)
			state->width, // width in pixels
			state->height,
			state->frameno,
			state->frame_period[port] / (1000000 / timescale),
			state->frames_per_chunk,
			0,                    // frame size - will look in the table
			(int)((float)timescale / (state->timescale)),
			state->frame_lengths, // array of frame lengths to build an index
			state->frame_data_start
	);
#if 0
	// now we need to overwrite last mdat tag in the header to the skip the gap, instead of the length 'mdat
	// put length 'skip length 'mdat
	he = lseek(state->ivf, 0, SEEK_CUR);    // just after the original header end
	l = state->frame_data_start - he;       // should be >=
	D4(fprintf(debug_file, "Remaining gap between Quicktime header and the data is %d (it should be>=8) \n", (int)l));
	lseek(state->ivf, he - 8, SEEK_SET);
	read(state->ivf, mdat_tag, 8); //!read calculated length+'mdat' tag
	lseek(state->ivf, state->frame_data_start - 8, SEEK_SET);
	write(state->ivf, mdat_tag, 8);
	skip_tag[0] = (l >> 24) & 0xff;
	skip_tag[1] = (l >> 16) & 0xff;
	skip_tag[2] = (l >>  8) & 0xff;
	skip_tag[3] = (l      ) & 0xff;
	lseek(state->ivf, he - 8, SEEK_SET);
	write(state->ivf, skip_tag, 8);
#endif
	close(state->ivf);
	state->ivf = -1;
	// free memory used for index
	if (state->frame_lengths) {
		free(state->frame_lengths);
		state->frame_lengths = NULL;
	}
	return 0;
}

/**
 * @brief Starts with the input file pointer just after the opening "{",
 * and output file - at the beginning of it's output
 * on exit - input pointer - after closing "}", output after it's output
 * @param[in]   d
 * @param[in]   l
 * @return      None
 */
void putBigEndian(unsigned long d, int l)
{
	unsigned char od[4];

	od[3] = d;
	od[2] = d >> 8;
	od[1] = d >> 16;
	od[0] = d >> 24;
	if (l) write(ofd, &od[4 - l], l);
}

/** @brief Temporary replacement for fgets to read from string */
char * sfgets(char * str, int size, const char * stream, int * pos)
{
	int l;
	const char * eol = strchr(&stream[*pos], '\n');

	if (!eol) eol = stream + (strlen(stream) - 1);  // pointer to last before '\0'
	l = (eol - stream) - (*pos);
	if (l >= size) l = size - 1;
	memcpy(str, &stream[*pos], l);
	str[l] = '\0';
	*pos += l;
	return str;
}

int parse_special(void)
{
	time_t ltime;
	int n, j, l;
	char str[256];
	char c;
	int i = 0;
	int gap;

	while (((c = iFile[iPos++]) != 0x20) && (c != 0x09) && (c != 0x0a) && (c != 0x0d) && (c != 0x0) && (i < 255) && ( iPos < iFileLen )) str[i++] = c;
	str[i] = 0;

	D4(fprintf(debug_file, "parse_special, str=!%s\n", str));

	if (strcmp(str, "mdata") == 0) {
		putBigEndian(headerSize, 4); return 0;
	}                                                                    // will put zeroes on pass 1
	if (strcmp(str, "height") == 0) {
		putBigEndian(height, 2); return 0;
	}
	if (strcmp(str, "width") == 0) {
		putBigEndian(width, 2); return 0;
	}
	if (strcmp(str, "width") == 0) {
		putBigEndian(width, 2); return 0;
	}
	if (strcmp(str, "nframes") == 0) {
		putBigEndian(nframes, 4); return 0;
	}
	if (strcmp(str, "timescale") == 0) {
		putBigEndian(timescale, 4); return 0;
	}
	if (strcmp(str, "duration") == 0) {
		putBigEndian(nframes * sample_dur, 4); return 0;
	}
	if (strcmp(str, "frame_duration") == 0) {
		putBigEndian(sample_dur, 4); return 0;
	}
	if (strcmp(str, "samples_chunk") == 0) {
		putBigEndian(samplesPerChunk, 4); return 0;
	}                                                                                 // will put zeroes on pass 1
	if (strcmp(str, "sample_sizes") == 0) {
		if (sizes != NULL) for (i = 0; i < nframes; i++) putBigEndian(sizes[i], 4);
		else for (i = 0; i < nframes; i++) putBigEndian(framesize, 4);
		return 0;
	}
	if (strcmp(str, "chunk_offsets") == 0) {
		n = (nframes - 1) / samplesPerChunk + 1;
		putBigEndian(n, 4);
		if (sizes != NULL) {
			l = 0; j = 0;
			for (i = 0; i < nframes; i++) {
				if (j == 0) putBigEndian(headerSize + l, 4);
				j++; if (j >= samplesPerChunk) j = 0;
				l += sizes[i];
			}

		} else for (i = 0; i < n; i++) putBigEndian(headerSize + framesize * samplesPerChunk * i, 4);
		return 0;
	}
	// a hack - invlude length'skip if data position (header size is known and there is a gap)
	if (strcmp(str, "data_size") == 0) {
		gap = headerSize - lseek(ofd, 0, SEEK_CUR) - 8;
		if (gap > 0) { //it should be exactly 0 if there is no gap or >8 if there is
			D4(fprintf(debug_file, "Inserting a skip tag to compensate for a gap (%d bytes) between the header and the frame data\n", gap));
			if (gap < 8) {
				D0(fprintf(debug_file, "not enough room to insret 'skip' tag - %d (need 8)\n", gap));
				return -1;
			}
			D4(fprintf(debug_file, "writing hex %x, %x bytes\n", gap, 4));
			putBigEndian(gap, 4);
			D4(fprintf(debug_file, "writing string <%s>\n", "skip"));
			write(ofd, "skip", 4);
			lseek(ofd, gap - 8, SEEK_CUR); // lseek over the gap and proceed as before
		}
		if (sizes != NULL) {
			l = 0;
			for (i = 0; i < nframes; i++) l += sizes[i];
			D4(fprintf(debug_file, "writing hex %x, %x bytes\n", l, 4));
			putBigEndian(l, 4);
		} else putBigEndian(nframes * framesize, 4);
		return 0;
	}
	if (strcmp(str, "time") == 0) {
		time(&ltime);
		ltime += 2082801600;      // 1970->1904// 31,557,600 seconds/year
		putBigEndian(ltime, 4); return 0;
	}
	return -1;
}

int parse(camogm_state *state, int top)      // if top - will not include length
{
	long out_start, out_end;
	char c;
	unsigned long d, l;
	char * cp;

	D4(fprintf(debug_file, "parse(%x)\n", top));
	c = iFile[iPos++];
	D5(fprintf(debug_file, "%c", c));
	out_start = lseek(ofd, 0, SEEK_CUR);

	if (!top) putBigEndian(0, 4);
	while (( iPos < iFileLen ) && (c != '}')) {
		// skip white spaces strchr
		if ((c != ' ') && (c != 0x9) && (c != 0xa) && (c != 0xd)) {
			if (c == '!') {
				if (parse_special() < 0) return -1;
			}
			// children atoms
			else if (c == '{') {
				if (parse(state, 0) < 0) return -1;
				// skip comments
			} else if (c == '#') sfgets( comStr, sizeof(comStr), iFile, &iPos);
			else if (c == '\'') {
				sfgets( comStr, sizeof(comStr), iFile, &iPos);
				if ((cp = strchr(comStr, 0x0a)) != NULL) cp[0] = 0;
				if ((cp = strchr(comStr, 0x0d)) != NULL) cp[0] = 0;
				if ((cp = strchr(comStr, '#')) != NULL) cp[0] = 0;
				cp = comStr + strlen(comStr) - 1;
				while ((cp > comStr) && ((cp[0] == 0x20) || (cp[0] == 0x09))) cp--;
				cp[1] = 0;
				write(ofd, comStr, strlen(comStr));
				D4(fprintf(debug_file, "writing string <%s>\n", comStr));
			} else if (strchr(hexStr, c)) {
				d = 0;
				l = 1;
				do {
					d = (d << 4) + (strchr(hexStr, c) - hexStr);
					l++;
				} while (( iPos < iFileLen ) && (l <= 8) && (strchr(hexStr, (c = iFile[iPos++]))) );
				l = (l) >> 1;
				putBigEndian(d, l);

				D4(fprintf(debug_file, "writing hex %lx, %lx bytes\n", d, l));


			} else if ((c == '}')) {
				break;

			} else {
				return -1;
			}

		}
		c = iFile[iPos++];

	}

	// fread fseek ftell
	if (!top) {
		out_end = lseek(ofd, 0, SEEK_CUR);
		lseek(ofd, out_start, SEEK_SET);
		putBigEndian((out_end - out_start), 4);
		lseek(state->ivf, out_end, SEEK_SET);
	}
	return 0;
}


int quicktime_template_parser( camogm_state *state,
		const char * i_iFile,     // now - string containing header template
		int i_ofd,                // output file descriptor (opened)
		int i_width,              // width in pixels
		int i_height,
		int i_nframes,
		int i_sample_dur,
		int i_samplesPerChunk,
		int i_framesize,
		int i_timescale,
		int * i_sizes,
		int data_start            // zero if dfata is not written yet (will be no gap)
)
{
	iFile =           i_iFile;
	width =           i_width;
	height =          i_height;
	nframes =         i_nframes;
	sample_dur =      i_sample_dur;
	samplesPerChunk = i_samplesPerChunk;
	framesize =       i_framesize;
	timescale =       i_timescale;
	sizes =           i_sizes;
	iPos = 0;                     // position in the string "iFile"
	ofd =             i_ofd;
	iFileLen =        strlen(iFile);
	lseek(ofd, 0, SEEK_SET);
	if (data_start) headerSize = data_start;
	D3(fprintf(debug_file, "PASS I\n"));

	while ( iPos < iFileLen ) parse(state, 1);  // pass 1
	if (!headerSize) headerSize = lseek(ofd, 0, SEEK_CUR);
	iPos = 0;
	lseek(ofd, 0, SEEK_SET);

	D3(fprintf(debug_file, "PASS II\n"));
	while ( iPos < iFileLen ) parse(state, 1);  // pass 2

	return 0;
}
