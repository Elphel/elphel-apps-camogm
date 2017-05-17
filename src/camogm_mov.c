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
#include <assert.h>

#include "camogm_mov.h"

/** @brief QuickTime header length (w/o index tables) enough to accommodate static data */
#define QUICKTIME_MIN_HEADER      0x300
/** @brief The length in bytes of sample-to-chunk table entry as defined in QuickTime format specification */
#define S2C_ENTRY_LEN             12
/** @brief The number of entries in sample-to-chunk table. See camogm_start_mov for the reason why we need 3 entries. */
#define S2C_ENTRIES               3

// for the parser
const char hexStr[] = "0123456789abcdef";
const char qt_template_v[] = "/etc/qt_source";                  // QuickTime template for video files
const char qt_template_av[] = "/etc/qt_audio";                  // QuickTime template for video + audio files
char comStr[1024];
int width = 1280;
int height = 1024;
int nframes = 100;
int sample_dur = 80;
int samplesPerChunk = 10;
int framesize = 80000;
int timescale = 600;
int * sizes;                                                    // array of frame sizes
int iPos;                                                       // position in the string "iFile"
int ofd;                                                        // output file descriptor (file opened by the caller)
int iFileLen;
char * q_template = NULL;
long headerSize = 0;
const char *iFile = NULL;

unsigned long audio_rate = 0;
unsigned short audio_channels = 0;
int audio_timescale;
long audio_duration;

int quicktime_template_parser(camogm_state *state,
		const char * i_iFile,                                   // now - string containing header template
		int i_ofd,                                              // output file descriptor (opened)
		int i_width,                                            // width in pixels
		int i_height,
		int i_nframes,
		int i_sample_dur,
		int i_samplesPerChunk,
		int i_framesize,
		int i_timescale,
		int * i_sizes,
		int data_start                                          // put zero if the header is written before data (no gap)
);
void putBigEndian(unsigned long d, int l);
int parse_special(camogm_state *state);
int parse(camogm_state *state, int top);
static int camogm_audio_mov(struct audio *audio, void *buff, long len, long slen);
static inline bool is_audio_frame(unsigned long len);
static inline void mark_audio(unsigned long *len);
static inline void unmark_audio(unsigned long *len);

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
	const char *qt_template_file = state->audio.audio_enable ? qt_template_av : qt_template_v;

	if ((qt_header = fopen(qt_template_file, "r")) == NULL) {
		D0(fprintf(debug_file, "Error opening QuickTime header template %s for reading\n", qt_template_file));
		return -CAMOGM_FRAME_FILE_ERR;
	} else {
		D5(fprintf(debug_file, "QuickTime template file: %s\n", qt_template_file));
	}
	fseek(qt_header, 0, SEEK_END);
	size = ftell(qt_header);
	if (!((q_template = malloc(size + 1)))) {
		D0(fprintf(debug_file, "Could not allocate %d bytes of memory for QuickTime header template\n", (size + 1)));
		fclose(qt_header);
		return -CAMOGM_FRAME_MALLOC;
	}
	fseek(qt_header, 0, SEEK_SET); //rewind
	if (fread(q_template, size, 1, qt_header) < 1) {
		D0(fprintf(debug_file, "Could not read %d bytes of QuickTime header template from %s\n", (size + 1), qt_template_file));
		free(q_template);
		q_template = NULL;
		fclose(qt_header);
		return -CAMOGM_FRAME_FILE_ERR;
	}
	q_template[size] = 0;
	state->audio.write_samples = camogm_audio_mov;
	fclose(qt_header);

	return 0;
}

/**
 * Free resources allocated during MOV file recording
 * @return   None
 */
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
	int data_offset;
	struct audio *audio = &state->audio;

	state->frame_index = 0;

	if (audio->audio_enable) {
		/* Allocate memory for sample-to-chunk buffers. For simplicity, all audio chunks must be the same size and
		 * we enforce this by reading from ALSA buffer (see camogm_audio.c ) only when it contains the appropriate
		 * number of samples. Such approach simplifies the building of sample-to-chunk atoms, although there are
		 * two corner cases: the first and the last chunks in file can contain different number of samples, thus we
		 * need 3 entries in total (first, last and all in between). That is why S2C_ENTRIES = 3.
		 */
		audio->audio_samples_to_chunk = malloc(S2C_ENTRY_LEN * S2C_ENTRIES);
		if (!audio->audio_samples_to_chunk) {
			return -CAMOGM_FRAME_MALLOC;
		}
		for (int i = 0; i < S2C_ENTRIES; i++) {
			audio->audio_samples_to_chunk[i] = -1;
		}
	}

	// allocate memory for the frame index table
	if (!((state->frame_lengths = malloc(4 * state->max_frames)))) return -CAMOGM_FRAME_MALLOC;
	// open file for writing
	sprintf(state->path, "%s%010ld_%06ld.mov", state->path_prefix, state->frame_params[state->port_num].timestamp_sec, state->frame_params[state->port_num].timestamp_usec);
	if (((state->ivf = open(state->path, O_RDWR | O_CREAT, 0777))) < 0) {
		D0(fprintf(debug_file, "Error opening %s for writing, returned %d, errno=%d\n", state->path, state->ivf, errno));
		return -CAMOGM_FRAME_FILE_ERR;
	}
	/* skip QuickTime header as it will be filled in when the current file is finished and
	 * set the file pointer to where actual data will be recorded
	 */
	data_offset = QUICKTIME_MIN_HEADER + 16;
	data_offset += 4 * state->max_frames;                       // space for sample size atom - video
	data_offset += (4 * state->max_frames) / state->frames_per_chunk; // space for chunk offsets atom - video
	if (audio->audio_enable) {
		data_offset += 4 * state->max_frames;                   // space for chunk offsets atom - audio
		data_offset += S2C_ENTRY_LEN * S2C_ENTRIES;             // space for samples size atom - audio
	}

//	state->frame_data_start = QUICKTIME_MIN_HEADER + 16 + 4 * (state->max_frames) + ( 4 * (state->max_frames)) / (state->frames_per_chunk); // 8 bytes for "skip" tag
	state->frame_data_start = data_offset;
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
	int ret = 0;
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
		D0(fprintf(debug_file, "writev error %d (returned %d, expected %d, file descriptor %d, chn %ud)\n", j, iovlen, l, state->ivf, state->port_num));
		perror(strerror(j));
		close(state->ivf);
		state->ivf = -1;
		return -CAMOGM_FRAME_FILE_ERR;
	}
	state->frame_lengths[state->frame_index] = l;
	state->frame_index++;
	if (state->frame_index >= state->max_frames)
		return -CAMOGM_FRAME_CHANGED;

	return ret;
}

/**
 * Write audio samples to file.
 * @param[in]   buff   pointer to buffer containing audio samples
 * @param[in]   len    the size of buffer, in bytes
 * @param[in]   slen   the number of audio samples in buffer
 * @return      0 if data was recorded successfully and negative error code otherwise
 */
static int camogm_audio_mov(struct audio *audio, void *buff, long len, long slen)
{
	int ret_val = 0;
	unsigned long k;
	ssize_t wr_len;
	camogm_state *state = container_of(audio, camogm_state, audio);

	D6(fprintf(debug_file, "write audio sample, len = %d, slen = %d\n", len, slen));

	wr_len = write(state->ivf, buff, len);
	if (wr_len < len) {
		D0(fprintf(debug_file, "audio samples write error: %s; returned %d, expected %d\n", strerror(errno), wr_len, len));
		close(state->ivf);
		state->ivf = -1;
		return CAMOGM_FRAME_FILE_ERR;
	}
	k = len;
	mark_audio(&k);
	state->frame_lengths[state->frame_index] = k;
	state->frame_index++;

	if (audio->audio_samples_to_chunk[0] == -1) {
		// this slot contains the number of samples in first chunk in file
		audio->audio_samples_to_chunk[0] = slen;
	} else {
		// these slots contain the number of samples in the last and in the one before last chunks
		audio->audio_samples_to_chunk[1] = audio->audio_samples_to_chunk[2];
		audio->audio_samples_to_chunk[2] = slen;
	}
	audio->audio_frameno++;
	audio->audio_samples += slen;

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

	assert(state->frame_lengths);

	timescale = 10000;                                                      // frame period measured in 1/10000 of a second?
	// that was in old code. If that works - try to switch to microseconds
	l = lseek(state->ivf, 0, SEEK_CUR) - (state->frame_data_start) + 8;     // 4-byte length+"mdat"
	// fill in the header in the beginning of the file
	lseek(state->ivf, 0, SEEK_SET);
	quicktime_template_parser(state,
			q_template,           // string containing header template
			state->ivf,           // output file descriptor (opened)
			state->width,         // width in pixels
			state->height,
			state->frameno,       // the number of image frames
			state->frame_period[port] / (1000000 / timescale),
			state->frames_per_chunk,
			0,                    // frame size - will look in the table
			(int)((float)timescale / (state->timescale)),
//			state->frame_lengths, // array of frame lengths to build an index
			NULL,                 // array of frame lengths to build an index
			state->frame_data_start
	);
	close(state->ivf);
	state->ivf = -1;
	// free memory used for index
	if (state->frame_lengths) {
		free(state->frame_lengths);
		state->frame_lengths = NULL;
	}
	if (state->audio.audio_samples_to_chunk) {
		free(state->audio.audio_samples_to_chunk);
		state->audio.audio_samples_to_chunk = NULL;
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

int parse_special(camogm_state *state)
{
	time_t ltime;
	int n, j, l;
	char str[256];
	char c;
	int i = 0;
	int gap;
	unsigned long k;

	// non-printable characters from ASCII table
	while (((c = iFile[iPos++]) != 0x20) && (c != 0x09) && (c != 0x0a) && (c != 0x0d) && (c != 0x0) && (i < 255) && (iPos < iFileLen))
		str[i++] = c;
	str[i] = 0;

	D4(fprintf(debug_file, "parse_special, str=!%s\n", str));

	if (strcmp(str, "mdata") == 0) {
		putBigEndian(headerSize, 4); return 0;
	}                                                           // will put zeroes on pass 1
	if (strcmp(str, "height") == 0) {
		putBigEndian(height, 2); return 0;
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
	if (strcmp(str, "samples_chunk") == 0) {                    // 'stsc' video atom
		putBigEndian(samplesPerChunk, 4); return 0;
	}                                                           // will put zeroes on pass 1

	// atoms related to audio
	if (strcmp(str, "audio_channels") == 0) {
		putBigEndian(audio_channels, 2);
		return 0;
	}
	if (strcmp(str, "audio_rate") == 0) {
		putBigEndian(audio_rate, 4);
		return 0;
	}
	if (strcmp(str, "audio_timescale") == 0) {
		putBigEndian(audio_timescale, 4);
		return 0;
	}
	if (strcmp(str, "audio_duration") == 0) {
		putBigEndian(audio_duration, 4);
		return 0;
	}
	if (strcmp(str, "audio_frames") == 0) {
		putBigEndian(state->audio.audio_frameno, 4);
		return 0;
	}
	if (strcmp(str, "audio_samples") == 0) {
		putBigEndian(state->audio.audio_samples, 4);
		return 0;
	}
	if (strcmp(str, "audio_bytes_per_frame") == 0) {
		putBigEndian(state->audio.audio_channels * 2, 4);
		return 0;
	}
	if (strcmp(str, "audio_stsz") == 0) {
		putBigEndian(state->audio.audio_channels * 2, 4);
		/* sample size table in 'stsz' atom contains entry for every sample, sound samples are
		 * all the same size thus this table is not needed - put 0 as the number of entries here
		 */
		putBigEndian(0, 4);
		return 0;
	}
	if (strcmp(str, "audio_stco") == 0) {
		long offset = 0;
		n = state->audio.audio_frameno;
		putBigEndian(n, 4);
		j = 0;
		for (i = 0; i < state->frame_index; i++) {
			k = state->frame_lengths[i];
			if (is_audio_frame(k)) {
				l = offset;
				putBigEndian(headerSize + l, 4);
				j++;
			}
			unmark_audio(&k);
			offset += k;
		}
		if (j != n)
			D0(fprintf(debug_file, "Error MOV: wrong records for \"audio_stco\", have written %d, need to write %d\n", j, n));
		return 0;
	}
	if (strcmp(str, "audio_stsc") == 0) {
		n = 0;
		for (int entry = 0; entry < S2C_ENTRIES; entry++) {
			if (state->audio.audio_samples_to_chunk[entry] != -1) {
				n++;
			}
		}
		putBigEndian(n, 4);
		// first table entry refers to first audio chunk in file
		putBigEndian(1, 4);
		putBigEndian(state->audio.audio_samples_to_chunk[0], 4);
		putBigEndian(01, 4);
		// second table entry, most chunks in file refer here
		n = 2;
		if (state->audio.audio_samples_to_chunk[1] != -1) {
			putBigEndian(n, 4);
			putBigEndian(state->audio.audio_samples_to_chunk[1], 4);
			putBigEndian(01, 4);
			n = state->audio.audio_frameno;
		}
		// last table entry corresponds to the last audio chunk in file
		if (state->audio.audio_samples_to_chunk[2] != -1) {
			putBigEndian(n, 4);
			putBigEndian(state->audio.audio_samples_to_chunk[2], 4);
			putBigEndian(01, 4);
		}
		return 0;
	}
	if (strcmp(str, "sample_sizes") == 0) {                     // 'stsz' video atom
		// index for video stream only, audio index is build separately
		j = 0;
		for (i = 0; i < state->frame_index; i++) {
			k = state->frame_lengths[i];
			if (!is_audio_frame(k)) {
				putBigEndian(k, 4);
				j++;
			}
		}
		if (j != nframes)
			D0(fprintf(debug_file, "Error MOV: wrong records for \"samples_sizes\": have write: %d, need to write: %d\n", j, nframes));
		return 0;
	}
	if (strcmp(str, "chunk_offsets") == 0) {                    // 'stco' video atom
		int chunks = (nframes - 1) / samplesPerChunk + 1;
		putBigEndian(chunks, 4);
		l = 0; j = 0;
		for (i = 0; i < state->frame_index; i++) {
			// this chunk offset atom is for video frames only, it is built separately for audio
			k = state->frame_lengths[i];
			if (!is_audio_frame(k)) {
				if (j == 0)
					putBigEndian(headerSize + l, 4);
				j++;
				if (j >= samplesPerChunk)
					j = 0;
			}
			unmark_audio(&k);
			l += k;
		}
		return 0;
	}
	// a hack - include length'skip if data position (header size is known and there is a gap)
	if (strcmp(str, "data_size") == 0) {                        // 'mdat' atom, contains all data
		gap = headerSize - lseek(ofd, 0, SEEK_CUR) - 8;
		if (gap > 0) {                                          // it should be exactly 0 if there is no gap or >8 if there is
			D4(fprintf(debug_file, "Inserting a skip tag to compensate for a gap (%d bytes) between the header and the frame data\n", gap));
			if (gap < 8) {
				D0(fprintf(debug_file, "not enough room to insret 'skip' tag - %d (need 8)\n", gap));
				return -1;
			}
			D4(fprintf(debug_file, "writing hex %x, %x bytes\n", gap, 4));
			putBigEndian(gap, 4);
			D4(fprintf(debug_file, "writing string <%s>\n", "skip"));
			write(ofd, "skip", 4);
			lseek(ofd, gap - 8, SEEK_CUR);                      // lseek over the gap and proceed as before
		}
		// calculate and save the total size of 'mdat atom
		l = 0;
		for (i = 0; i < state->frame_index; i++)
			l += state->frame_lengths[i];
		D4(fprintf(debug_file, "writing hex %x, %x bytes\n", l, 4));
		putBigEndian(l, 4);
		return 0;
	}
	if (strcmp(str, "time") == 0) {
		time(&ltime);
		ltime += 2082801600;                                    // 1970->1904// 31,557,600 seconds/year
		putBigEndian(ltime, 4);
		return 0;
	}
	return -1;
}

/**
 * Parse QuickTime template and write data to file
 * @param[in]   state   a pointer to a structure containing current state
 * @param[in]   top     flag indicating that the length shoulb not be included
 * @return      0 if all is fine and -1 in case of an error
 */
int parse(camogm_state *state, int top)
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
				if (parse_special(state) < 0) return -1;
			}
			// children atoms
			else if (c == '{') {
				if (parse(state, 0) < 0) return -1;
				// skip comments
			} else if (c == '#') sfgets(comStr, sizeof(comStr), iFile, &iPos);
			else if (c == '\'') {
				sfgets(comStr, sizeof(comStr), iFile, &iPos);
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
				} while ((iPos < iFileLen) && (l <= 8) && (strchr(hexStr, (c = iFile[iPos++]))));
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

	audio_timescale = state->audio.audio_rate;
	audio_rate = audio_timescale;                               // QuickTime defines sample rate as unsigned 16.16 fixed-point number
	audio_rate <<= 16;
	audio_duration = state->audio.audio_samples;
	audio_channels = state->audio.audio_channels;

	if (data_start)
		headerSize = data_start;
	D3(fprintf(debug_file, "PASS I\n"));

	while (iPos < iFileLen) parse(state, 1);  // pass 1
	if (!headerSize) headerSize = lseek(ofd, 0, SEEK_CUR);
	iPos = 0;
	lseek(ofd, 0, SEEK_SET);

	D3(fprintf(debug_file, "PASS II\n"));
	while (iPos < iFileLen) parse(state, 1);  // pass 2

	return 0;
}

/**
 * Check if the length value in index table relates to audio frame
 * @param[in]  len   length value to check
 * @return     True if the value is related to audio frame and false otherwise
 */
static inline bool is_audio_frame(unsigned long len)
{
	if ((len & 0x80000000) == 0)
		return false;
	else
		return true;
}

/**
 * Mark index table entry as audio frame
 * @param[in]  len   length value to mark
 * @return     None
 */
static inline void mark_audio(unsigned long *len)
{
	*len |= 0x80000000;
}

/**
 * Reset audio flag from index table entry
 * @param[out]   len   length value to clear mark from
 * @param len
 */
static inline void unmark_audio(unsigned long *len)
{
	*len &= 0x7fffffff;
}
