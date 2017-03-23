/** @file camogm.c
 * @brief Program to write captured video (and audio) to camera file system
 * using Ogg container.
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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/fs.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <elphel/ahci_cmd.h>

#include "camogm_ogm.h"
#include "camogm_jpeg.h"
#include "camogm_mov.h"
#include "camogm_kml.h"
#include "camogm_read.h"

/** @brief Default debug level */
#define DEFAULT_DEBUG_LVL         6
/** @brief JPEG trailer size in bytes */
#define TRAILER_SIZE              0x02
/** @brief Default segment duration in seconds */
#define DEFAULT_DURATION          600
/** @brief Default segment length in bytes */
#define DEFAULT_LENGTH            1073741824
/** @brief Behavior for the files: 0 clean buffer, 1 - save as much as possible */
#define DEFAULT_GREEDY            0
/** @brief 0 - restartf file if fps changed, 1 - ignore variable fps (and skip less frames) */
#define DEFAULT_IGNORE_FPS        0
/** @brief Maximal number of frames in file segment (each need 4* (1 + 1/frames_per_chunk) bytes for the frame index.
 * This parameter is mostrly for Quicktime */
#define DEFAULT_FRAMES            16384
/** @brief Second sparse index - for Quicktime fast forward */
#define DEFAULT_FRAMES_PER_CHUNK  10
/** @brief Use Exif */
#define DEFAULT_EXIF              1
/** @brief Bit mask indicating all 4 sensor port channels are active */
#define ALL_CHN_ACTIVE            0x0f
/** @brief Bit mask indicating all 4 sensor port channels are inactive */
#define ALL_CHN_INACTIVE          0x00
/** @brief This is a basic helper macro for processing all sensor ports at a time */
#define FOR_EACH_PORT(indtype, indvar) for (indtype indvar = 0; indvar < SENSOR_PORTS; indvar++)

char trailer[TRAILER_SIZE] = { 0xff, 0xd9 };

#if 0
const char *exifFileNames[] = { "/dev/exif_exif0", "/dev/exif_exif1",
                                "/dev/exif_exif2", "/dev/exif_exif3"
};

const char *headFileNames[] = { "/dev/jpeghead0", "/dev/jpeghead1",
                                "/dev/jpeghead2", "/dev/jpeghead3"
};
const char *ctlFileNames[] = {   "/dev/frameparsall0", "/dev/frameparsall1",
                                "/dev/frameparsall2", "/de/framepars3"
};
const char *circbufFileNames[] = {"/dev/circbuf0", "/dev/circbuf1",
                                "/dev/circbuf2", "/dev/circbuf3"
};
#else
const char *exifFileNames[] = { DEV393_PATH(DEV393_EXIF0), DEV393_PATH(DEV393_EXIF1),
                                DEV393_PATH(DEV393_EXIF2), DEV393_PATH(DEV393_EXIF3)
};

const char *headFileNames[] = { DEV393_PATH(DEV393_JPEGHEAD0), DEV393_PATH(DEV393_JPEGHEAD1),
                                DEV393_PATH(DEV393_JPEGHEAD2), DEV393_PATH(DEV393_JPEGHEAD3)
};
const char *ctlFileNames[] = {  DEV393_PATH(DEV393_FRAMEPARS0), DEV393_PATH(DEV393_FRAMEPARS1),
                                DEV393_PATH(DEV393_FRAMEPARS2), DEV393_PATH(DEV393_FRAMEPARS3)
};
const char *circbufFileNames[] = {DEV393_PATH(DEV393_CIRCBUF0), DEV393_PATH(DEV393_CIRCBUF1),
                                  DEV393_PATH(DEV393_CIRCBUF2), DEV393_PATH(DEV393_CIRCBUF3)
};
#endif

int lastDaemonBit[SENSOR_PORTS] = {DAEMON_BIT_CAMOGM};
struct framepars_all_t   *frameParsAll[SENSOR_PORTS];
struct framepars_t       *framePars[SENSOR_PORTS];
/** @brief Parameters that are not frame-related, their changes do not initiate any actions */
unsigned long            *aglobalPars[SENSOR_PORTS];
/** @brief Global structure containing current state of the running program */
camogm_state sstate;
/** @brief Memory mapped circular buffer arrays */
unsigned long * ccam_dma_buf[SENSOR_PORTS];

pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @enum path_type
 * @brief Define the path type passed to a function
 * @var path_type::RAW_PATH
 * The path specified is a path to raw device buffer and it starts with /dev
 * @var path_type::FILE_PATH
 * The path specified is a regular path
 */
typedef enum {
	RAW_PATH,
	FILE_PATH
} path_type;

enum sysfs_path_type {
	TYPE_START,
	TYPE_SIZE
};

int debug_level;
FILE* debug_file;

void camogm_init(camogm_state *state, char *pipe_name, uint16_t port_num);
int camogm_start(camogm_state *state);
int camogm_stop(camogm_state *state);
void camogm_reset(camogm_state *state);
int camogm_debug(camogm_state *state, const char *fname);
int camogm_debug_level(int d);
void  camogm_set_segment_duration(camogm_state *state, int sd);
void  camogm_set_segment_length(camogm_state *state, int sl);
void  camogm_set_greedy(camogm_state *state, int d);
void  camogm_set_ignore_fps(camogm_state *state, int d);

void  camogm_set_save_gp(camogm_state *state, int d);
void  camogm_set_prefix(camogm_state *state, const char * p, path_type type);
void  camogm_set_exif(camogm_state *state, int d);
void  camogm_set_timescale(camogm_state *state, double d);   // set timescale, default=1.0
void  camogm_set_frames_skip(camogm_state *state, int d);    // set number of frames to skip, if negative - seconds between frames
void  camogm_set_format(camogm_state *state, int d);

void  camogm_kml_set_enable(camogm_state *state, int d);
void  camogm_kml_set_horHalfFov(camogm_state *state, double dd);
void  camogm_kml_set_vertHalfFov(camogm_state *state, double dd);
void  camogm_kml_set_height_mode(camogm_state *state, int d);
void  camogm_kml_set_height(camogm_state *state, double dd);
void  camogm_kml_set_period(camogm_state *state, int d);
void  camogm_kml_set_near(camogm_state *state, double dd); // distance to PhotoOverlay

int   parse_cmd(camogm_state *state, FILE* npipe);
char * getLineFromPipe(FILE* npipe);

int  sendImageFrame(camogm_state *state);

void  camogm_set_start_after_timestamp(camogm_state *state, double d);
void  camogm_set_max_frames(camogm_state *state, int d);
void  camogm_set_frames_per_chunk(camogm_state *state, int d);

static uint64_t get_disk_size(const char *name);
static int get_sysfs_name(const char *dev_name, char *sys_name, size_t str_sz, int type);
static int get_disk_range(const char *name, struct range *rng);
static int set_disk_range(const struct range *rng);
static void get_disk_info(camogm_state *state);
int open_files(camogm_state *state);
unsigned long getGPValue(unsigned int port, unsigned long GPNumber);
void setGValue(unsigned int port, unsigned long GNumber, unsigned long value);
unsigned int select_port(camogm_state *states);
inline void set_chn_state(camogm_state *s, unsigned int port, unsigned int new_state);
inline int is_chn_active(camogm_state *s, unsigned int port);
void clean_up(camogm_state *state);

void put_uint16(void *buf, u_int16_t val)
{
	unsigned char  *tmp;

	tmp = (unsigned char*)buf;

	tmp[0] = val & 0xff;
	tmp[1] = (val >>= 8) & 0xff;
}

void put_uint32(void *buf, u_int32_t val)
{
	unsigned char  *tmp;

	tmp = (unsigned char*)buf;

	tmp[0] = val & 0xff;
	tmp[1] = (val >>= 8) & 0xff;
	tmp[2] = (val >>= 8) & 0xff;
	tmp[3] = (val >>= 8) & 0xff;
}

void put_uint64(void *buf, u_int64_t val)
{
	unsigned char  *tmp;

	tmp = (unsigned char*)buf;

	tmp[0] = val & 0xff;
	tmp[1] = (val >>= 8) & 0xff;
	tmp[2] = (val >>= 8) & 0xff;
	tmp[3] = (val >>= 8) & 0xff;
	tmp[4] = (val >>= 8) & 0xff;
	tmp[5] = (val >>= 8) & 0xff;
	tmp[6] = (val >>= 8) & 0xff;
	tmp[7] = (val >>= 8) & 0xff;
}

/**
 * @brief Detect running compressors and update active channels mask. This function should be
 * called right after all driver files are opened.
 * @param   state   pointer to #camogm_state structure containing program state
 * @return  none
 */
void check_compressors(camogm_state *state)
{
	for (int i = 0; i < SENSOR_PORTS; i++) {
		if (getGPValue(i, P_COMPRESSOR_RUN) != COMPRESSOR_RUN_STOP) {
			state->active_chn |= 1 << i;
		}
	}
}

/**
 * @brief Initialize the state of the program
 * @param[in]   state     pointer to #camogm_state structure containing program state
 * @param[in]   pipe_name pointer to command pipe name string
 * @param[in]   port_num  communication socket port number
 * @return      none
 */
void camogm_init(camogm_state *state, char *pipe_name, uint16_t port_num)
{
	const char sserial[] = "elp0";
	int * ipser = (int*)sserial;

	memset(state, 0, sizeof(camogm_state));
	camogm_set_segment_duration(state, DEFAULT_DURATION);
	camogm_set_segment_length(state, DEFAULT_LENGTH);
	camogm_set_greedy(state, DEFAULT_GREEDY);
	camogm_set_ignore_fps(state, DEFAULT_IGNORE_FPS);
	camogm_set_max_frames(state, DEFAULT_FRAMES);
	camogm_set_frames_per_chunk(state, DEFAULT_FRAMES_PER_CHUNK);
	camogm_reset(state);                    // sets state->buf_overruns =- 1
	state->serialno = ipser[0];
	debug_file = stderr;
	camogm_debug_level(DEFAULT_DEBUG_LVL);
	strcpy(state->debug_name, "stderr");
	camogm_set_timescale(state, 1.0);
	camogm_set_frames_skip(state, 0);       // don't skip
	camogm_set_format(state, CAMOGM_FORMAT_MOV);
	state->exif = DEFAULT_EXIF;
	state->frame_lengths = NULL;

	// reading thread has not been started yet, mutex lock is not necessary
	state->prog_state = STATE_STOPPED;
	state->rawdev.thread_state = STATE_STOPPED;

	// kml stuff
	camogm_kml_set_horHalfFov(state, 20.0);
	camogm_kml_set_vertHalfFov(state, 15.0);
	camogm_kml_set_height_mode(state, 0);
	camogm_kml_set_height(state, 10.0);
	camogm_kml_set_period(state, 2);        // 2 sec
	camogm_kml_set_near(state, 40.0);       // 40 m (distance to PhotoOverlay)

	state->pipe_name = pipe_name;
	state->rawdev.start_pos = RAWDEV_START_OFFSET;
	state->rawdev.end_pos = state->rawdev.start_pos;
	state->rawdev.curr_pos_w = state->rawdev.start_pos;
	state->rawdev.curr_pos_r = state->rawdev.start_pos;
	state->active_chn = ALL_CHN_INACTIVE;
	state->rawdev.mmap_default_size = MMAP_CHUNK_SIZE;
	state->sock_port = port_num;
}

/**
 * @brief Set debug output file
 * @param[in]   state   a pointer to a structure containing current state
 * @param[in]   fname   file used for debug output
 * @return      0 if the file was opened successfully and negative error code otherwise
 */
int camogm_debug(camogm_state *state, const char * fname)
{
	int none = 1;

	if (fname && strlen(fname) && strcmp(fname, "none") && strcmp(fname, "null")  && strcmp(fname, "/dev/null")) none = 0;
	if (debug_file) {
		if (strcmp(state->debug_name, "stdout") && strcmp(state->debug_name, "stderr")) fclose(debug_file);
		debug_file = NULL;
		state->debug_name[0] = '\0';
	}
	if (!none) {
		if (strcmp(fname, "stdout") == 0) debug_file = stdout;
		else if (strcmp(fname, "stderr") == 0) debug_file = stderr;
		else debug_file = fopen(fname, "w+");
	}
	if (debug_file) {
		strncpy(state->debug_name, fname, sizeof(state->debug_name) - 1);
		state->debug_name[sizeof(state->debug_name) - 1] = '\0';
	}
	return 0;
}

/** @brief Set current debug level */
int camogm_debug_level(int d)
{
	debug_level = d;
	return 0;
}

/**
 * @brief Start recording for each active sensor port
 * @param[in]   state   a pointer to a structure containing current state
 * @return      0 if the recording finished successfully and negative error code otherwise
 */
int camogm_start(camogm_state *state)
{
	int timestamp_start;
	int rslt;
	int next_metadata_start, next_jpeg_len, fp;
	int port = state->port_num;

	if (state->active_chn == ALL_CHN_INACTIVE) {
		D0(fprintf(debug_file, "All channels are disabled, will not start\n"));
		return CAMOGM_FRAME_OTHER;
	}
	if (state->rawdev_op && state->format != CAMOGM_FORMAT_JPEG) {
		D0(fprintf(debug_file, "Raw device write initiated, but file format is not JPEG. Will not start\n"));
		return CAMOGM_FRAME_OTHER;
	}

	D1(fprintf(debug_file, "Starting recording\n"));
	double dtime_stamp;
	state->frameno = 0;
	state->timescale = state->set_timescale; // current timescale, default 1
// debug
	int * ifp =      (int*)&(state->frame_params[port]);
	int * ifp_this = (int*)&(state->this_frame_params[port]);
	if (state->kml_enable) camogm_init_kml();  // do nothing

	if (state->format != state->set_format) {
		state->format =  state->set_format;
		switch (state->format) {
		case CAMOGM_FORMAT_NONE: rslt = 0; break;
		case CAMOGM_FORMAT_OGM:  rslt = camogm_init_ogm(); break;
		case CAMOGM_FORMAT_JPEG: rslt = camogm_init_jpeg(state); break;
		case CAMOGM_FORMAT_MOV:  rslt = camogm_init_mov(); break;
		}
		state->formats |= 1 << (state->format);
		// exit on unknown formats?
	}
	state->max_frames =       state->set_max_frames;
	state->frames_per_chunk = state->set_frames_per_chunk;
	pthread_mutex_lock(&state->mutex);
	state->prog_state = STATE_STARTING;
	pthread_mutex_unlock(&state->mutex);
	FOR_EACH_PORT(int, chn) {
		if (is_chn_active(state, chn)) {
			// Check/set circbuf read pointer
			D3(fprintf(debug_file, "1: state->cirbuf_rp=0x%x\n", state->cirbuf_rp[chn]));
			if ((state->cirbuf_rp[chn] < 0) || (lseek(state->fd_circ[chn], state->cirbuf_rp[chn], SEEK_SET) < 0) || (lseek(state->fd_circ[chn], LSEEK_CIRC_VALID, SEEK_END) < 0 )) {
				D3(fprintf(debug_file, "2: state->cirbuf_rp=0x%x\n", state->cirbuf_rp[chn]));
				/* In "greedy" mode try to save as many frames from the circbuf as possible */
				state->cirbuf_rp[chn] = lseek(state->fd_circ[chn], state->greedy ? LSEEK_CIRC_SCND : LSEEK_CIRC_LAST, SEEK_END);
				if (!state->ignore_fps) {                                                                               // don't even try in ignore mode
					if (((fp = lseek(state->fd_circ[chn], LSEEK_CIRC_PREV, SEEK_END))) >= 0) state->cirbuf_rp[chn] = fp;//try to have 2 frames available for fps
				}
				state->buf_overruns[chn]++;
				// file pointer here should match state->rp; so no need to do    lseek(state->fd_circ,state->cirbuf_rp,SEEK_SET);
				state->buf_min[chn] = getGPValue(chn, G_FREECIRCBUF);

			} else {
				if (state->buf_min[chn] > getGPValue(chn, G_FREECIRCBUF)) state->buf_min[chn] = getGPValue(chn, G_FREECIRCBUF);

			}
			D3(fprintf(debug_file, "3: state->cirbuf_rp=0x%x\n", state->cirbuf_rp[chn]));
			D3(fprintf(debug_file, "4:lseek(state->fd_circ,LSEEK_CIRC_READY,SEEK_END)=%d\n", (int)lseek(state->fd_circ[chn], LSEEK_CIRC_READY, SEEK_END)));

			// is this frame ready?
			if (lseek(state->fd_circ[chn], LSEEK_CIRC_READY, SEEK_END) < 0) return -CAMOGM_FRAME_NOT_READY;  // frame pointer valid, but no frames yet
			D3(fprintf(debug_file, "5: state->cirbuf_rp=0x%x\n", state->cirbuf_rp[chn]));
			state->metadata_start = (state->cirbuf_rp[chn]) - 32;
			if (state->metadata_start < 0) state->metadata_start += state->circ_buff_size[chn];

			// ==================================

			memcpy(&(state->frame_params[chn]), (unsigned long* )&ccam_dma_buf[chn][state->metadata_start >> 2], 32);
			state->jpeg_len = state->frame_params[chn].frame_length; // frame_params.frame_length are now the length of bitstream

			if (state->frame_params[chn].signffff != 0xffff) {
				D0(fprintf(debug_file, "%s:%d: wrong signature - %d\r\n", __FILE__, __LINE__, (int)state->frame_params[chn].signffff));
				state->cirbuf_rp[chn] = -1;
				ifp = (int *) &state->frame_params[chn];
				D1(fprintf(debug_file, "state->cirbuf_rp=0x%x\r\n", (int)state->cirbuf_rp[chn]));
				D1(fprintf(debug_file, "%08x %08x %08x %08x %08x %08x %08x %08x\r\n", ifp[0], ifp[1], ifp[2], ifp[3], ifp[4], ifp[5], ifp[6], ifp[7]));
				return -CAMOGM_FRAME_BROKEN;
			}
			// find location of the timestamp and copy it to the frame_params structure
			// ==================================
			timestamp_start = (state->cirbuf_rp[chn]) + ((state->jpeg_len + CCAM_MMAP_META + 3) & (~0x1f)) + 32 - CCAM_MMAP_META_SEC; // magic shift - should index first byte of the time stamp
			if (timestamp_start >= state->circ_buff_size[chn]) timestamp_start -= state->circ_buff_size[chn];
			memcpy(&(state->frame_params[chn].timestamp_sec), (unsigned long* )&ccam_dma_buf[chn][timestamp_start >> 2], 8);
			// New - see if current timestamp is later than start one, if not return "CAMOGM_TOO_EARLY" reset read pointer and buffer read pointer
			if (state->start_after_timestamp > 0.0) { // don't bother if it is 0
				dtime_stamp = 0.000001 * state->frame_params[chn].timestamp_usec + state->frame_params[chn].timestamp_sec;
				if (dtime_stamp < state->start_after_timestamp) {
					state->cirbuf_rp[chn] = -1;
					D3(fprintf(debug_file, "Too early to start, %f < %f\n", dtime_stamp, state->start_after_timestamp));
					return -CAMOGM_TOO_EARLY;
				}
			}
			D3(fprintf(debug_file, "6: state->cirbuf_rp=0x%x\n", state->cirbuf_rp[chn]));
			// see if next frame is available
			if ((lseek(state->fd_circ[chn], LSEEK_CIRC_NEXT, SEEK_END) < 0 ) ||
					// is that next frame ready?
					(((fp = lseek(state->fd_circ[chn], LSEEK_CIRC_READY, SEEK_END))) < 0)) {
				D3(fprintf(debug_file, "6a:lseek(state->fd_circ,LSEEK_CIRC_NEXT,SEEK_END)=0x%x,  fp=0x%x\n", (int)lseek(state->fd_circ[chn], LSEEK_CIRC_NEXT, SEEK_END), (int)lseek(state->fd_circ[chn], LSEEK_CIRC_READY, SEEK_END)));

				lseek(state->fd_circ[chn], state->cirbuf_rp[chn], SEEK_SET);      // just in case - restore pointer
				return -CAMOGM_FRAME_NOT_READY;                                   // frame pointer valid, but no frames yet
			}
			next_metadata_start = fp - 32;
			if (next_metadata_start < 0) next_metadata_start += state->circ_buff_size[chn];
			memcpy(&(state->this_frame_params[chn]), (unsigned long* )&ccam_dma_buf[chn][next_metadata_start >> 2], 32);
			next_jpeg_len = state->this_frame_params[chn].frame_length;  // frame_params.frame_length are now the length of bitstream
			if (state->this_frame_params[chn].signffff != 0xffff) {      // should not happen ever
				D0(fprintf(debug_file, "%s:%d: wrong signature - %d\r\n", __FILE__, __LINE__, (int)state->this_frame_params[chn].signffff));
				ifp_this = (int *) &state->this_frame_params[chn];
				D1(fprintf(debug_file, "fp=0x%x\r\n", (int)fp));
				D1(fprintf(debug_file, "%08x %08x %08x %08x %08x %08x %08x %08x\r\n", ifp_this[0], ifp_this[1], ifp_this[2], ifp_this[3], ifp_this[4], ifp_this[5], ifp_this[6], ifp_this[7]));
				state->cirbuf_rp[chn] = -1;
				return -CAMOGM_FRAME_BROKEN;
			}
			D3(fprintf(debug_file, "7: state->cirbuf_rp=0x%x\n", state->cirbuf_rp[chn]));

			// find location of the timestamp and copy it to the frame_params structure
			timestamp_start = fp + ((next_jpeg_len + CCAM_MMAP_META + 3) & (~0x1f)) + 32 - CCAM_MMAP_META_SEC; // magic shift - should index first byte of the time stamp
			if (timestamp_start >= state->circ_buff_size[chn]) timestamp_start -= state->circ_buff_size[chn];
			memcpy(&(state->this_frame_params[chn].timestamp_sec), (unsigned long* )&ccam_dma_buf[chn][timestamp_start >> 2], 8);
			// verify that the essential current frame params did not change, if they did - return an error (need new file header)
			if (!state->ignore_fps && ((state->frame_params[chn].width  != state->this_frame_params[chn].width) ||
					(state->frame_params[chn].height != state->this_frame_params[chn].height))) {
				// Advance frame pointer to the next (caller should try again)
				state->cirbuf_rp[chn] = fp;
				return -CAMOGM_FRAME_CHANGED; // no yet checking for the FPS
			}
			D3(fprintf(debug_file, "8: state->cirbuf_rp=0x%x\n", state->cirbuf_rp[chn]));

			// calcualte the frame period - time difference (in microseconds)
			state->frame_period[chn] = (state->this_frame_params[chn].timestamp_usec - state->frame_params[chn].timestamp_usec) +
					1000000 * (state->this_frame_params[chn].timestamp_sec  - state->frame_params[chn].timestamp_sec);

			// correct for timelapse modes:
			state->frames_skip =      state->set_frames_skip;
			if (state->frames_skip > 0) {
				state->frames_skip_left[chn] = 0;
				state->frame_period[chn] *= (state->frames_skip + 1);
			} else if (state->frames_skip < 0) {
				state->frame_period[chn] = -(state->frames_skip); // actual frame period will fluctuate to the nearest frame acquired (free running)
				state->frames_skip_left[chn] = state->frame_params[chn].timestamp_sec;
			}
			D3(fprintf(debug_file, "9: state->frame_period=0x%x\n", state->frame_period[chn]));

			state->time_unit = (ogg_int64_t)(((double)state->frame_period[chn]) * ((double)10) / ((double)state->timescale));
			state->width = state->frame_params[chn].width;
			state->height = state->frame_params[chn].height;

			// read JPEG header - it should stay the same for the whole file (restart new file if any parameters changed)
			// rebuild JPEG header:
			lseek(state->fd_head[chn], state->cirbuf_rp[chn] + 1, SEEK_END);  // +1 to avoid condition when jpeg_start==0. overloaded lseek will ignore 5 LSBs when SEEK_END
			state->head_size[chn] = lseek(state->fd_head[chn], 0, SEEK_END);  // In 8.0 the header size might change for some jp4 modes
			if (state->head_size[chn] > JPEG_HEADER_MAXSIZE) {
				D0(fprintf(debug_file, "%s:%d: Too big JPEG header (%d > %d)", __FILE__, __LINE__, state->head_size[chn], JPEG_HEADER_MAXSIZE ));
				return -2;
			}
			// and read it
			lseek(state->fd_head[chn], 0, 0);
			read(state->fd_head[chn], state->jpegHeader[chn], state->head_size[chn]);
			// Restore read pointer to the original (now there may be no frame ready there yet)
			lseek(state->fd_circ[chn], state->cirbuf_rp[chn], SEEK_SET);
		}
	}

// here we are ready to initialize Ogm (or other) file
	switch (state->format) {
	case CAMOGM_FORMAT_NONE: rslt = 0;  break;
	case CAMOGM_FORMAT_OGM:  rslt = camogm_start_ogm(state);  break;
	case CAMOGM_FORMAT_JPEG: rslt = camogm_start_jpeg(state); break;
	case CAMOGM_FORMAT_MOV:  rslt = camogm_start_mov(state);  break;
	default: rslt = 0; // do nothing
	}
	if (rslt) {
		D0(fprintf(debug_file, "camogm_start() error, rslt=0x%x\n", rslt));
		return rslt;
	}
	if (state->kml_enable) rslt = camogm_start_kml(state);  // will turn on state->kml_used if it can
	if (rslt) return rslt;
	pthread_mutex_lock(&state->mutex);
	state->prog_state = STATE_RUNNING;
	pthread_mutex_unlock(&state->mutex);
	D1(fprintf(debug_file, "Started OK\n"));
	return 0;
}

/**
 * @brief Save a single image from circular buffer to a file.
 * @param[in]   state   a pointer to a structure containing current state
 * @return      0 if the image was successfully saved and negative error code otherwise
 */
int sendImageFrame(camogm_state *state)
{
	int rslt;
	unsigned char frame_packet_type = PACKET_IS_SYNCPOINT;
	int timestamp_start;
	int * ifp_this = (int*)&(state->this_frame_params[state->port_num]);
	int fp;
	int port = state->port_num;

// This is probably needed only for Quicktime (not to exceed already allocated frame index)
	if (!state->rawdev_op && (state->frameno >= (state->max_frames))) {
		D3(fprintf(debug_file, "sendImageFrame:1: state->frameno(0x%x) >= state->max_frames(0x%x)\n", state->frameno, state->max_frames));
		return -CAMOGM_FRAME_CHANGED;
	}
// Format changed?
//   D3(fprintf (debug_file,"sendImageFrame: format=%d, set_format=%d\n", state->format, state->set_format));

	if (state->format != state->set_format) {
		D3(fprintf(debug_file, "sendImageFrame:2: state->format(0x%x) != state->set_format(0x%x)\n", state->format, state->set_format));
		return -CAMOGM_FRAME_CHANGED;
	}
//   check if file size is exceeded (assuming fopen),-CAMOGM_FRAME_CHANGED will trigger a new segment
	if ((state->vf) && (state->segment_length >= 0) && (ftell(state->vf) > state->segment_length)) {
		D3(fprintf(debug_file, "sendImageFrame:3: segment length exceeded\n"));
		return -CAMOGM_FRAME_CHANGED;
	}
//same for open
	if (((state->ivf) >= 0) && (state->segment_length >= 0) && (lseek(state->ivf, 0, SEEK_CUR) > state->segment_length)) {
		D3(fprintf(debug_file, "sendImageFrame:4: segment length exceeded\n"));
		return -CAMOGM_FRAME_CHANGED;
	}
// check the frame pointer is valid
	if ((fp = lseek(state->fd_circ[port], state->cirbuf_rp[port], SEEK_SET)) < 0) {
		D3(fprintf(debug_file, "sendImageFrame:5: invalid frame\n"));

		return -CAMOGM_FRAME_INVALID; //it will probably be that allready
	}
// is the frame ready?
	if (lseek(state->fd_circ[port], LSEEK_CIRC_READY, SEEK_END) < 0) {
		D3(fprintf(debug_file, "?6,fp=0x%x ", fp));     //frame not ready, frame pointer seems valid, but not ready
		return -CAMOGM_FRAME_NOT_READY;                 // frame pointer valid, but no frames yet
	}

// process skipping frames. TODO: add - skipping time between frames (or better -  actual time period - use the nearest frame) instead of the frame number
	if ( (state->frames_skip > 0) && (state->frames_skip_left[port] > 0 )) { //skipping frames, not seconds.
		state->cirbuf_rp[port] = lseek(state->fd_circ[port], LSEEK_CIRC_NEXT, SEEK_END);
//optionally save it to global read pointer (i.e. for debugging with imgsrv "/pointers")
		if (state->save_gp) lseek(state->fd_circ[port], LSEEK_CIRC_SETP, SEEK_END);
		state->frames_skip_left[port]--;
		D3(fprintf(debug_file, "?7 ")); //frame not ready
		return -CAMOGM_FRAME_NOT_READY; // the required frame is not ready
	}

// Get metadata
	D3(fprintf(debug_file, "_1_"));
	state->metadata_start = state->cirbuf_rp[port] - 32;
	if (state->metadata_start < 0) state->metadata_start += state->circ_buff_size[port];
	memcpy(&(state->this_frame_params[port]), (unsigned long* )&ccam_dma_buf[state->port_num][state->metadata_start >> 2], 32);
	state->jpeg_len = state->this_frame_params[port].frame_length; // frame_params.frame_length are now the length of bitstream
	if (state->this_frame_params[port].signffff != 0xffff) {
		D0(fprintf(debug_file, "%s:%d: wrong signature - %d\r\n", __FILE__, __LINE__, (int)state->this_frame_params[port].signffff));
		D1(fprintf(debug_file, "state->cirbuf_rp=0x%x\r\n", (int)state->cirbuf_rp[port]));
		D1(fprintf(debug_file, "%08x %08x %08x %08x %08x %08x %08x %08x\r\n", ifp_this[0], ifp_this[1], ifp_this[2], ifp_this[3], ifp_this[4], ifp_this[5], ifp_this[6], ifp_this[7]));

		D3(fprintf(debug_file, "sendImageFrame:8: frame broken\n"));
		return -CAMOGM_FRAME_BROKEN;
	}
	D3(fprintf(debug_file, "_2_"));
//   find location of the timestamp and copy it to the frame_params structure
	timestamp_start = state->cirbuf_rp[port] + ((state->jpeg_len + CCAM_MMAP_META + 3) & (~0x1f)) + 32 - CCAM_MMAP_META_SEC; // magic shift - should index first byte of the time stamp
	if (timestamp_start >= state->circ_buff_size[port]) timestamp_start -= state->circ_buff_size[port];
	D3(fprintf(debug_file, "_3_"));
	memcpy(&(state->this_frame_params[port].timestamp_sec), (unsigned long* )&ccam_dma_buf[state->port_num][timestamp_start >> 2], 8);
// verify that the essential current frame params did not change, if they did - return an error (need new file header)
	if (!state->ignore_fps && ((state->frame_params[port].width  != state->this_frame_params[port].width) ||
				   (state->frame_params[port].height != state->this_frame_params[port].height))) {
		D3(fprintf(debug_file, "sendImageFrame:9: WOI changed\n"));
		return -CAMOGM_FRAME_CHANGED; // not yet checking for the FPS
	}
//   check if file duration (in seconds) exceeded ,-CAMOGM_FRAME_CHANGED will trigger a new segment
	if (!state->rawdev_op && (state->segment_duration > 0) &&
	    ((state->this_frame_params[port].timestamp_sec - state->frame_params[port].timestamp_sec) > state->segment_duration)) {
		D3(fprintf(debug_file, "sendImageFrame:10: segment duration in seconds exceeded\n"));
		return -CAMOGM_FRAME_CHANGED;
	}
// check if (in timelapse mode)  it is too early for the frame to be stored
	if ((state->frames_skip < 0) && (state->frames_skip_left[port] > state->this_frame_params[port].timestamp_sec) ) {
		state->cirbuf_rp[port] = lseek(state->fd_circ[port], LSEEK_CIRC_NEXT, SEEK_END);
//optionally save it to global read pointer (i.e. for debugging with imgsrv "/pointers")
		if (state->save_gp) lseek(state->fd_circ[port], LSEEK_CIRC_SETP, SEEK_END);
		D3(fprintf(debug_file, "sendImageFrame:11: timelapse: frame will be skipped\n"));
		return -CAMOGM_FRAME_NOT_READY; // the required frame is not ready
	}


	D3(fprintf(debug_file, "_4_"));
	if (state->exif) {
		D3(fprintf(debug_file, "_5_"));
// update the Exif header with the current frame metadata
		state->exifSize[port] = lseek(state->fd_exif[port], 1, SEEK_END); // at the beginning of page 1 - position == page length
		if (state->exifSize[port] > 0) {
//state->this_frame_params.meta_index
			lseek(state->fd_exif[port], state->this_frame_params[port].meta_index, SEEK_END); // select meta page to use (matching frame)
			rslt = read(state->fd_exif[port], state->ed[port], state->exifSize[port]);
			if (rslt < 0) rslt = 0;
			state->exifSize[port] = rslt;
		} else state->exifSize[port] = 0;
	} else state->exifSize[port] = 0;

	D3(fprintf(debug_file, "_6_"));

// prepare a packet to be sent (a lst of memory chunks)
	state->chunk_index = 0;
	state->packetchunks[state->chunk_index  ].bytes = 1;
	state->packetchunks[state->chunk_index++].chunk = &frame_packet_type;
	if (state->exif > 0) { // insert Exif
		D3(fprintf(debug_file, "_7_"));
		state->packetchunks[state->chunk_index  ].bytes = 2;
		state->packetchunks[state->chunk_index++].chunk = state->jpegHeader[port];
		state->packetchunks[state->chunk_index  ].bytes = state->exifSize[port];
		state->packetchunks[state->chunk_index++].chunk = state->ed[port];
		state->packetchunks[state->chunk_index  ].bytes = state->head_size[port] - 2;
		state->packetchunks[state->chunk_index++].chunk = &(state->jpegHeader[port][2]);
	} else {
		D3(fprintf(debug_file, "_8_"));
		state->packetchunks[state->chunk_index  ].bytes = state->head_size[port];
		state->packetchunks[state->chunk_index++].chunk = state->jpegHeader[port];
	}
	D3(fprintf(debug_file, "_9_"));

/* JPEG image data may be split in two segments (rolled over buffer end) - process both variants */
	if ((state->cirbuf_rp[port] + state->jpeg_len) > state->circ_buff_size[port]) { // two segments
/* copy from the beginning of the frame to the end of the buffer */
		D3(fprintf(debug_file, "_10_"));
		state->packetchunks[state->chunk_index  ].bytes = state->circ_buff_size[port] - state->cirbuf_rp[port];
		state->packetchunks[state->chunk_index++].chunk = (unsigned char*)&ccam_dma_buf[state->port_num][state->cirbuf_rp[port] >> 2];
/* copy from the beginning of the buffer to the end of the frame */
		state->packetchunks[state->chunk_index  ].bytes = state->jpeg_len - (state->circ_buff_size[port] - state->cirbuf_rp[port]);
		state->packetchunks[state->chunk_index++].chunk = (unsigned char*)&ccam_dma_buf[state->port_num][0];
	} else { // single segment
		D3(fprintf(debug_file, "_11_"));

/* copy from the beginning of the frame to the end of the frame (no buffer rollovers) */
		state->packetchunks[state->chunk_index  ].bytes = state->jpeg_len;
		state->packetchunks[state->chunk_index++].chunk = (unsigned char*)&ccam_dma_buf[state->port_num][state->cirbuf_rp[port] >> 2];
	}
	D3(fprintf(debug_file, "_12_"));
	state->packetchunks[state->chunk_index  ].bytes = 2;
	state->packetchunks[state->chunk_index++].chunk = (unsigned char*)trailer;

	switch (state->format) {
	case CAMOGM_FORMAT_NONE: rslt = 0; break;
	case CAMOGM_FORMAT_OGM:  rslt = camogm_frame_ogm(state); break;
	case CAMOGM_FORMAT_JPEG: rslt = camogm_frame_jpeg(state); break;
	case CAMOGM_FORMAT_MOV:  rslt = camogm_frame_mov(state); break;
	default: rslt = 0; // do nothing
	}
	if (rslt) {
		D3(fprintf(debug_file, "sendImageFrame:12: camogm_frame_***() returned %d\n", rslt));
		return rslt;
	}
	if (state->kml_used) rslt = camogm_frame_kml(state);  // will turn on state->kml_used if it can
	if (rslt) return rslt;

	D3(fprintf(debug_file, "_14_"));
// advance frame pointer
	state->frameno++;
	state->cirbuf_rp[port] = lseek(state->fd_circ[port], LSEEK_CIRC_NEXT, SEEK_END);
// optionally save it to global read pointer (i.e. for debugging with imgsrv "/pointers")
	if (state->save_gp) lseek(state->fd_circ[port], LSEEK_CIRC_SETP, SEEK_END);
	D3(fprintf(debug_file, "_15_\n"));
	if (state->frames_skip > 0) {
		state->frames_skip_left[port] = state->frames_skip;
	} else if (state->frames_skip < 0) {
		state->frames_skip_left[port] += -(state->frames_skip);
	}
	return 0;
}

/**
 * @brief Stop current recording. If recording was not started, this function has no effect. This function
 * calls @e camogm_end_* for the current file format.
 * @param[in]   state   a pointer to a structure containing current state
 * @return      0 if the operation was stopped successfully and negative error code otherwise
 */
int camogm_stop(camogm_state *state)
{
	int rslt = 0;

	if (state->prog_state != STATE_RUNNING) {
		if (state->prog_state != STATE_STARTING) {
			D2(fprintf(debug_file, "Recording was not running, nothing to stop\n"));
		} else {
			pthread_mutex_lock(&state->mutex);
			state->prog_state = STATE_STOPPED;
			pthread_mutex_unlock(&state->mutex);
			D1(fprintf(debug_file, "Dropping attempt to start\n"));
		}
		return 0;
	}
	D1(fprintf(debug_file, "Ending recording\n"));
	if (state->kml_used) camogm_end_kml(state);
	switch (state->format) {
	case CAMOGM_FORMAT_NONE: rslt = 0; break;
	case CAMOGM_FORMAT_OGM:  rslt = camogm_end_ogm(state); break;
	case CAMOGM_FORMAT_JPEG: rslt = camogm_end_jpeg(state); break;
	case CAMOGM_FORMAT_MOV:  rslt = camogm_end_mov(state); break;
//    default: return 0; // do nothing
	}
// now close video file (if it is open)
	if (state->vf) fclose(state->vf);
	state->vf = NULL;
	if (rslt) return rslt;
	state->last = 1;
	pthread_mutex_lock(&state->mutex);
	state->prog_state = STATE_STOPPED;
	pthread_mutex_unlock(&state->mutex);
	return 0;
}

/**
 * @brief Free all file format handlers that were used. @e camogm_free_* for
 * each format used will be called.
 * @param[in]   state   a pointer to a structure containing current state
 * @return      None
 */
void camogm_free(camogm_state *state)
{
	for (int f = 0; f < 31; f++) {
		if (state->formats & ( 1 << (state->format))) {
			switch (f) {
			case CAMOGM_FORMAT_NONE: break;
			case CAMOGM_FORMAT_OGM:  camogm_free_ogm(); break;
			case CAMOGM_FORMAT_JPEG: camogm_free_jpeg(); break;
			case CAMOGM_FORMAT_MOV:  camogm_free_mov(); break;
			}
		}
	}
	state->formats = 0;
}

/**
 * @brief Reset read pointer and overruns counter for each sensor port
 * @param[in]   state   a pointer to a structure containing current state
 * @return      None
 */
void camogm_reset(camogm_state *state)
{
	FOR_EACH_PORT(int, chn) {
		state->cirbuf_rp[chn] = -1;
		state->buf_overruns[chn] = -1; // first overrun will not count
	}
}

/** @brief kml parameter setter */
void  camogm_kml_set_enable(camogm_state *state, int d)
{
	state->kml_enable = d;
}
/** @brief kml parameter setter */
void  camogm_kml_set_horHalfFov(camogm_state *state, double dd)
{
	state->kml_horHalfFov = dd;
}
/** @brief kml parameter setter */
void  camogm_kml_set_vertHalfFov(camogm_state *state, double dd)
{
	state->kml_vertHalfFov = dd;
}
/** @brief kml parameter setter */
void  camogm_kml_set_height_mode(camogm_state *state, int d)
{
	state->kml_height_mode = d;
}
/** @brief kml parameter setter */
void  camogm_kml_set_height(camogm_state *state, double dd)
{
	state->kml_height = dd;
}
/** @brief kml parameter setter */
void  camogm_kml_set_period(camogm_state *state, int d)
{
	state->kml_period = d;
	state->kml_last_ts = 0;
	state->kml_last_uts = 0;
}
/** @brief kml parameter setter */
void  camogm_kml_set_near(camogm_state *state, double dd)   // distance to PhotoOverlay
{
	state->kml_near = dd;
}


void  camogm_set_segment_duration(camogm_state *state, int sd)
{
	state->segment_duration = sd;
}
void  camogm_set_segment_length(camogm_state *state, int sl)
{
	state->segment_length =   sl;
}
void  camogm_set_save_gp(camogm_state *state, int d)
{
	state->save_gp =   d;
}
void  camogm_set_exif(camogm_state *state, int d)
{
	state->exif =   d;
}

void  camogm_set_greedy(camogm_state *state, int d)
{
	state->greedy =   d ? 1 : 0;
}
void  camogm_set_ignore_fps(camogm_state *state, int d)
{
	state->ignore_fps =   d ? 1 : 0;
}

/**
 * @brief Set file name prefix or raw device file name.
 * @param[in]   state   a pointer to a structure containing current state
 * @param[in]   p       a pointer to the prefix string. The string is prepended to
 * the file name as is.
 * @param[in]   type    the type of prefix, can be one of #path_type
 */
void  camogm_set_prefix(camogm_state *state, const char * p, path_type type)
{
	if (type == FILE_PATH) {
		strncpy(state->path_prefix, p, sizeof(state->path_prefix) - 1);
		state->path_prefix[sizeof(state->path_prefix) - 1] = '\0';
	} else if (type == RAW_PATH && (strncmp(p, "/dev/", 5) == 0)) {
		strncpy(state->rawdev.rawdev_path, p, sizeof(state->rawdev.rawdev_path) - 1);
		state->rawdev.rawdev_path[sizeof(state->rawdev.rawdev_path) - 1] = '\0';
	}
}

/**
 * @brief Get disk size, first LBA, last LBA and save these parameters in state structure.
 * @param[in]   state   a pointer to a structure containing current state
 * @return      None
 */
void get_disk_info(camogm_state *state)
{
	struct range rng = {0};

	state->rawdev.end_pos = get_disk_size(state->rawdev.rawdev_path);
	if (state->rawdev.end_pos == 0) {
		state->rawdev_op = 0;
		state->rawdev.end_pos = state->rawdev.start_pos;
		state->rawdev.rawdev_path[0] = '\0';
		D0(fprintf(debug_file, "ERROR: unable to initiate raw device operation\n"));
	} else {
		D0(fprintf(debug_file, "WARNING: raw device write initiated\n"));
		state->rawdev_op = 1;
	}

	if (get_disk_range(state->rawdev.rawdev_path, &rng) == 0) {
		set_disk_range(&rng);
	} else {
		D0(fprintf(debug_file, "ERROR: unable to get disk size and starting sector\n"));
	}
}

/** @brief Set timescale @e d, default = 1,000,000 */
void  camogm_set_timescale(camogm_state *state, double d)
{
	state->set_timescale =  d;
	if (state->prog_state == STATE_STOPPED) {
		state->timescale = state->set_timescale;
	}
}

/** @brief Set the number of frames @e d to be skipped during recording. This values is common for each sensor port */
void  camogm_set_frames_skip(camogm_state *state, int d)
{
	state->set_frames_skip =  d;
	if (state->prog_state == STATE_STOPPED) {
		state->frames_skip =      state->set_frames_skip;
		state->frames_skip_left[state->port_num] = 0;
	}
}

/** @brief Set file format @e d the images will be recorded to. This function calls corresponding @e camogm_init_*
 * function for the format selected.*/
void  camogm_set_format(camogm_state *state, int d)
{
	int rslt = 0;

	state->set_format =  d;
	if (state->prog_state == STATE_STOPPED) {
		state->format =  state->set_format;
		switch (state->format) {
		case CAMOGM_FORMAT_NONE: rslt = 0; break;
		case CAMOGM_FORMAT_OGM:  rslt = camogm_init_ogm(); break;
		case CAMOGM_FORMAT_JPEG: rslt = camogm_init_jpeg(state); break;
		case CAMOGM_FORMAT_MOV:  rslt = camogm_init_mov(); break;
		}
		if (rslt) {
			D0(fprintf(debug_file, "%s:%d: Error setting format to=%d\n", __FILE__, __LINE__, state->format));
		}
		state->formats |= 1 << (state->format);
	}
}

/** @brief Set max number of frames @e d */
void  camogm_set_max_frames(camogm_state *state, int d)
{
	state->set_max_frames =  d;
	if (state->prog_state == STATE_STOPPED)
		state->max_frames = d;
}

/** @brief Set the number of frames @e d recorded per chunk */
void  camogm_set_frames_per_chunk(camogm_state *state, int d)
{
	state->set_frames_per_chunk =  d;
	if (state->prog_state == STATE_STOPPED)
		state->frames_per_chunk = d;
}

/** @brief Set the time stamp @e d when recording should be started */
void  camogm_set_start_after_timestamp(camogm_state *state, double d)
{
	state->start_after_timestamp =  d;
}

/**
 * @brief Print current status either as plain text or in xml format
 * @param[in]   state   to a structure containing current state
 * @param[in]   fn      a pointer to a file name which will be used for output. Can be NULL or 'stdout' for
 * output to stdout, 'stderr' for output to stderr and a file name for output to a file
 * @param[in]   xml     flag indicating that the output should be in xml format
 * @note access to state->rawdev.curr_pos_r is not locked in reading thread
 * @return      None
 */
void  camogm_status(camogm_state *state, char * fn, int xml)
{
	int64_t _len = 0;
	int _dur = 0, _udur = 0, _dur_raw, _udur_raw;
	FILE* f;
	char *_state, *_output_format, *_using_exif, *_using_global_pointer, *_compressor_state[SENSOR_PORTS];
	int _b_free[SENSOR_PORTS], _b_used[SENSOR_PORTS], _b_size[SENSOR_PORTS];
	int _frames_remain[SENSOR_PORTS] = {0};
	int _sec_remain[SENSOR_PORTS] = {0};
	int _frames_skip = 0;
	int _sec_skip = 0;
	char *_kml_enable, *_kml_used, *_kml_height_mode;
	unsigned int _percent_done;
	off_t save_p;

	_kml_enable =      state->kml_enable ? "yes" : "no";
	_kml_used =        state->kml_used ? "yes" : "no";
	_kml_height_mode = state->kml_height_mode ? "GPS altitude" : "map ground level"; // 1 - actual, 0 - ground

	for (int chn = 0; chn < SENSOR_PORTS; chn++) {
		_b_size[chn] = getGPValue(chn, G_FRAME_SIZE);
		save_p = lseek(state->fd_circ[chn], 0, SEEK_CUR);
		_b_free[chn] = lseek(state->fd_circ[chn], LSEEK_CIRC_FREE, SEEK_END);
		lseek(state->fd_circ[chn], save_p, SEEK_SET);
		_b_used[chn] = lseek(state->fd_circ[chn], LSEEK_CIRC_USED, SEEK_END);
		lseek(state->fd_circ[chn], save_p, SEEK_SET);
		_compressor_state[chn] = (getGPValue(chn, P_COMPRESSOR_RUN) == 2) ? "running" : "stopped";
		if (state->frames_skip > 0)
			_frames_remain[chn] = state->frames_skip_left[chn];
		else if (state->frames_skip < 0)
			_sec_remain[chn] = (state->frames_skip_left[chn] - state->this_frame_params[chn].timestamp_sec);

		_dur_raw = state->this_frame_params[chn].timestamp_sec - state->frame_params[chn].timestamp_sec;
		_udur_raw = state->this_frame_params[chn].timestamp_usec - state->frame_params[chn].timestamp_usec;
		if (_udur_raw < 0) {
			_dur_raw -= 1;
			_udur_raw += 1000000;
		} else if (_udur_raw >= 1000000) {
			_dur_raw += 1;
			_udur_raw -= 1000000;
		}
		_dur += _dur_raw;
		_udur += _udur_raw;
		if (_udur > 1000000) {
			_dur += 1;
			_udur -= 1000000;
		}
	}
	if ( state->frames_skip > 0 ) {
		_frames_skip = state->frames_skip;
	} else if ( state->frames_skip < 0 ) {
		_sec_skip = -(state->frames_skip);
	}

	if (!fn) f = stdout;
	else if (strcmp(fn, "stdout") == 0) f = stdout;
	else if (strcmp(fn, "stderr") == 0) f = stderr;
	else {
		if (!((f = fopen(fn, "w")))) {
			D0(fprintf(debug_file, "Error opening %s\n", fn));
			return;
		}
	}
	if (state->rawdev_op)
		_len = state->rawdev.total_rec_len;
	else if (state->vf) _len = (int64_t)ftell(state->vf);                            // for ogm
	else if ((state->ivf) >= 0) _len = (int64_t)lseek(state->ivf, 0, SEEK_CUR);      //for mov
	switch (state->prog_state) {
	case STATE_STOPPED:
		_state = "stopped";
		break;
	case STATE_RUNNING:
		_state = "running";
		break;
	case STATE_STARTING:
		_state = "starting";
		break;
	case STATE_READING:
		_state = "reading";
		break;
	default:
		_state = "stopped";
	}
	_output_format = state->format ? ((state->format == CAMOGM_FORMAT_OGM) ? "ogm" :
					  ((state->format == CAMOGM_FORMAT_JPEG) ? "jpeg" :
					   ((state->format == CAMOGM_FORMAT_MOV) ? "mov" :
					       "other"))) : "none";
	_using_exif =    state->exif ? "yes" : "no";
	_using_global_pointer = state->save_gp ? "yes" : "no";
	if (state->rawdev.curr_pos_r != 0 && state->rawdev.curr_pos_r > state->rawdev.start_pos)
		_percent_done = 100 * state->rawdev.curr_pos_r / (state->rawdev.end_pos - state->rawdev.start_pos);
	else
		_percent_done = 0;

	if (xml) {
		fprintf(f, "<?xml version=\"1.0\"?>\n" \
			"<camogm_state>\n" \
			"  <state>\"%s\"</state>\n" \
			"  <file_name>\"%s\"</file_name>\n" \
			"  <frame_number>%d</frame_number>\n" \
			"  <start_after_timestamp>%f</start_after_timestamp>\n"	\
			"  <file_duration>%d.%06d</file_duration>\n" \
			"  <file_length>%" PRId64 "</file_length>\n" \
			"  <frames_skip_left>%d</frames_skip_left>\n" \
			"  <seconds_skip_left>%d</seconds_skip_left>\n"	\
			"  <frame_width>%d</frame_width>\n" \
			"  <frame_height>%d</frame_height>\n" \
			"  <format>\"%s\"</format>\n" \
			"  <exif>\"%s\"</exif>\n" \
			"  <prefix>\"%s\"</prefix>\n" \
			"  <max_duration>%d</max_duration>\n" \
			"  <max_length>%d</max_length>\n" \
			"  <max_frames>%d</max_frames>\n" \
			"  <timescale>%f</timescale>\n"	\
			"  <frames_per_chunk>%d</frames_per_chunk>\n" \
			"  <last_error_code>%d</last_error_code>\n" \
			"  <debug_output>\"%s\"</debug_output>\n" \
			"  <debug_level>%d</debug_level>\n" \
			"  <use_global_rp>\"%s\"</use_global_rp>\n" \
			"  <kml_enable>\"%s\"</kml_enable>\n" \
			"  <kml_used>\"%s\"</kml_used>\n" \
			"  <kml_path>\"%s\"</kml_path>\n" \
			"  <kml_horHalfFov>\"%f\"</kml_horHalfFov>\n" \
			"  <kml_vertHalfFov>\"%f\"</kml_vertHalfFov>\n"	\
			"  <kml_near>\"%f\"</kml_near>\n" \
			"  <kml_height_mode>\"%s\"</kml_height_mode>\n"	\
			"  <kml_height>\"%f\"</kml_height>\n" \
			"  <kml_period>%d</kml_period>\n" \
			"  <kml_last_ts>%d.%06d</kml_last_ts>\n" \
			"  <greedy>\"%s\"</greedy>\n" \
			"  <ignore_fps>\"%s\"</ignore_fps>\n" \
			"  <raw_device_path>\"%s\"</raw_device_path>\n" \
			"  <raw_device_overruns>%d</raw_device_overruns>\n" \
			"  <raw_device_pos_write>0x%llx</raw_device_pos_write>\n" \
			"  <raw_device_pos_read>0x%llx (%d%% done)</raw_device_pos_read>\n",
			_state,  state->path, state->frameno, state->start_after_timestamp, _dur, _udur, _len, \
			_frames_skip, _sec_skip, \
			state->width, state->height, _output_format, _using_exif, \
			state->path_prefix, state->segment_duration, state->segment_length, state->max_frames, state->timescale, \
			state->frames_per_chunk,  state->last_error_code, \
			state->debug_name, debug_level, _using_global_pointer, \
			_kml_enable, _kml_used, state->kml_path, state->kml_horHalfFov, state->kml_vertHalfFov, state->kml_near, \
			_kml_height_mode, state->kml_height, state->kml_period, state->kml_last_ts, state->kml_last_uts, \
			state->greedy ? "yes" : "no", state->ignore_fps ? "yes" : "no", state->rawdev.rawdev_path,
			state->rawdev.overrun, state->rawdev.curr_pos_w, state->rawdev.curr_pos_r, _percent_done);

		FOR_EACH_PORT(int, chn) {
			char *_active = is_chn_active(state, chn) ? "yes" : "no";
			fprintf(f,
			"\t<sensor_port_%d>\n" \
			"\t\t<channel_active>\"%s\"</channel_active>\n" \
			"\t\t<compressor_state>\"%s\"</compressor_state>\n" \
			"\t\t<frame_size>%d</frame_size>\n" \
			"\t\t<frames_skip>%d</frames_skip>\n" \
			"\t\t<seconds_skip>%d</seconds_skip>\n" \
			"\t\t<buffer_overruns>%d</buffer_overruns>\n" \
			"\t\t<buffer_minimal>%d</buffer_minimal>\n" \
			"\t\t<frame_period>%d</frame_period>\n" \
			"\t\t<buffer_free>%d</buffer_free>\n" \
			"\t\t<buffer_used>%d</buffer_used>\n" \
			"\t\t<circbuf_rp>%d</circbuf_rp>\n" \
			"\t</sensor_port_%d>\n",
				chn,
				_active,
				_compressor_state[chn],
				_b_size[chn],
				_frames_remain[chn],
				_sec_remain[chn],
				state->buf_overruns[chn],
				state->buf_min[chn],
				state->frame_period[chn],
				_b_free[chn],
				_b_used[chn],
				state->cirbuf_rp[chn],
				chn
				);
		}
		fprintf(f, "</camogm_state>\n");
	} else {
		fprintf(f, "state              \t%s\n",        _state);
		fprintf(f, "file               \t%s\n",        state->path);
		fprintf(f, "frame              \t%d\n",        state->frameno);
		fprintf(f, "start_after_timestamp \t%f\n",     state->start_after_timestamp);
		fprintf(f, "file duration      \t%d.%06d sec\n", _dur, _udur);
		fprintf(f, "file length        \t%" PRId64 " B\n", _len);
		fprintf(f, "width              \t%d (0x%x)\n", state->width, state->width);
		fprintf(f, "height             \t%d (0x%x)\n", state->height, state->height);
		fprintf(f, "\n");
		fprintf(f, "output format      \t%s\n",        _output_format);
		fprintf(f, "using exif         \t%s\n",        _using_exif);
		fprintf(f, "path prefix        \t%s\n",        state->path_prefix);
		fprintf(f, "raw device path    \t%s\n",        state->rawdev.rawdev_path);
		fprintf(f, "raw device overruns\t%d\n",        state->rawdev.overrun);
		fprintf(f, "raw write position \t0x%llx\n",    state->rawdev.curr_pos_w);
		fprintf(f, "raw read position  \t0x%llx\n",    state->rawdev.curr_pos_r);
		fprintf(f, "   percent done    \t%d%%\n",      _percent_done);
		fprintf(f, "max file duration  \t%d sec\n",    state->segment_duration);
		fprintf(f, "max file length    \t%d B\n",      state->segment_length);
		fprintf(f, "max frames         \t%d\n",        state->max_frames);
		fprintf(f, "timescale          \t%f\n",        state->timescale);
		fprintf(f, "frames per chunk   \t%d\n",        state->frames_per_chunk);
		fprintf(f, "greedy             \t%s\n",        state->greedy ? "yes" : "no");
		fprintf(f, "ignore fps         \t%s\n",        state->ignore_fps ? "yes" : "no");
		fprintf(f, "\n");
		fprintf(f, "last error code    \t%d\n",        state->last_error_code);
		fprintf(f, "\n");
		fprintf(f, "debug output to    \t%s\n",        state->debug_name);
		fprintf(f, "debug level        \t%d\n",        debug_level);
		fprintf(f, "use global pointer \t%s\n",        _using_global_pointer);
		fprintf(f, "\n\n");
		fprintf(f, "kml_enable         \t%s\n",        _kml_enable);
		fprintf(f, "kml_used           \t%s\n",        _kml_used);
		fprintf(f, "kml_path           \t%s\n",        state->kml_path);
		fprintf(f, "kml_horHalfFov     \t%f degrees\n", state->kml_horHalfFov);
		fprintf(f, "kml_vertHalfFov    \t%f degrees\n", state->kml_vertHalfFov);
		fprintf(f, "kml_near           \t%f m\n",      state->kml_near);
		fprintf(f, "kml height mode    \t%s\n",        _kml_height_mode);
		fprintf(f, "kml_height (extra) \t%f m\n",      state->kml_height);
		fprintf(f, "kml_period         \t%d\n",        state->kml_period);
		fprintf(f, "kml_last_ts        \t%d.%06d\n",   state->kml_last_ts, state->kml_last_uts);
		fprintf(f, "\n");
		FOR_EACH_PORT(int, chn) {
			char *_active = is_chn_active(state, chn) ? "yes" : "no";
			fprintf(f, "===== Sensor port %d status =====\n", chn);
			fprintf(f, "enabled            \t%s\n",    _active);
			fprintf(f, "compressor state   \t%s\n",    _compressor_state[chn]);
			fprintf(f, "frame size         \t%d\n",    _b_size[chn]);
			if (_frames_skip > 0)
				fprintf(f, "frames to skip \t%d (left %d)\n", _frames_skip, _frames_remain[chn]);
			if (_sec_skip < 0 )
				fprintf(f, "timelapse period \t%d sec (remaining %d sec)\n", _sec_skip, _sec_remain[chn]);
			fprintf(f, "buffer overruns    \t%d\n",    state->buf_overruns[chn]);
			fprintf(f, "buffer minimal     \t%d\n",    state->buf_min[chn]);
			fprintf(f, "frame period       \t%d (0x%x)\n", state->frame_period[chn], state->frame_period[chn]);
			fprintf(f, "buffer free        \t%d\n",    _b_free[chn]);
			fprintf(f, "buffer used        \t%d\n",    _b_used[chn]);
			fprintf(f, "circbuf_rp         \t%d (0x%x)\n", state->cirbuf_rp[chn], state->cirbuf_rp[chn]);
			fprintf(f, "\n");
		}
	}
	if ((f != stdout) && (f != stderr)) fclose(f);
	FOR_EACH_PORT(int, chn) {if (state->buf_overruns[chn] >= 0) state->buf_overruns[chn] = 0;}  // resets overruns after reading status , so "overruns" means since last reading status
	state->last_error_code = 0;                             // Reset error
	FOR_EACH_PORT(int, chn) {state->buf_min[chn] = _b_free[chn];}
}

/**
 * @brief Read a single command from pipe
 * @param[in]   npipe   pointer to command pipe
 * @return Pointer to null terminated string if a command is available or NULL otherwise
 */
char * getLineFromPipe(FILE* npipe)
{
	int fl;
	char * nlp;
	static char cmdbuf[1024];
	static int cmdbufp = 0; // current input pointer in the command buffer (read from pipe)
	static int cmdstrt = 0; // start of the next partial command

//remove used string if any
	if (cmdstrt > 0) {
//moving overlapping strings
		memmove(cmdbuf, &cmdbuf[cmdstrt], sizeof(cmdbuf) - cmdstrt);
		cmdbufp -= cmdstrt;
		cmdstrt = 0;
	}
// is there any complete string in a buffer?
	if (!cmdbufp) cmdbuf[cmdbufp] = 0;  //null-terminate first access (probably not needed for the static buffer
	nlp = strpbrk(cmdbuf, ";\n");
	if (!nlp) { //no complete string, try to read more
		fl = fread(&cmdbuf[cmdbufp], 1, sizeof(cmdbuf) - cmdbufp - 1, npipe);
		cmdbuf[cmdbufp + fl] = 0;
// is there any complete string in a buffer after reading?
		nlp = strpbrk(&cmdbuf[cmdbufp], ";\n"); // there were no new lines before cmdbufp
		cmdbufp += fl;                          //advance pointer after pipe read
	}
	if (nlp) {
		nlp[0] = 0;
		cmdstrt = nlp - cmdbuf + 1;
		for (fl = 0; cmdbuf[fl] && strchr(" \t", cmdbuf[fl]); fl++) ;
		return &cmdbuf[fl];
	} else {
		return NULL;
	}
}

/**
 * @brief Read and execute commands sent over command pipe
 * @param state   pointer to a structure containing current state
 * @param npipe   pointer to command pipe
 * @return        0 if pipe was empty, positive value corresponding to a command processed or
 * -1 in case of an error
 */
int parse_cmd(camogm_state *state, FILE* npipe)
{
	char * cmd;
	char * args;
	char * argse;
	int d;
	double dd;

//skip empty commands
	while (((cmd = getLineFromPipe(npipe))) && !cmd[0]) ;
	if (!cmd) return 0;  // nothing in the pipe
	D2(fprintf(debug_file, "Got command: '%s'\n", cmd));

// Acknowledge received command by copying frame number to per-daemon parameter
//	GLOBALPARS(state->port_num, G_DAEMON_ERR + lastDaemonBit[state->port_num]) = GLOBALPARS(state->port_num, G_THIS_FRAME);
	setGValue(state->port_num, G_DAEMON_ERR + lastDaemonBit[state->port_num], getGPValue(state->port_num, G_THIS_FRAME));
	args = strpbrk(cmd, "= \t");
// is it just a single word command or does it have parameters?
	if (args) {
		args[0] = 0;
		args++;
		while (strchr("= \t", args[0])) args++;
		if (args[0]) {
// ltrim (args)
			for (argse = strchr(args, '\0') - 1; strchr("= \t", argse[0]); argse--) argse[0] = '\0';
		}
		if (!args[0]) args = NULL;
	}
// now cmd is trimmed, arg is NULL or a pointer to trimmed command arguments
	if (strcmp(cmd, "start") == 0) {
		check_compressors(state);
		get_disk_info(state);
		camogm_start(state);
		return 1;
	} else if (strcmp(cmd, "reset") == 0) { // will reset pointer to the last acquired frame (if any)
		camogm_reset(state);
		return 2;
	} else if (strcmp(cmd, "stop") == 0) {
		camogm_stop(state);
		return 3;
	} else if (strcmp(cmd, "exit") == 0) {
		camogm_stop(state);
		camogm_free(state);
		clean_up(state);
		exit(0);
	} else if (strcmp(cmd, "duration") == 0) {
		if (!(args) || (((d = strtol(args, NULL, 10))) <= 0)) d = DEFAULT_DURATION;
		camogm_set_segment_duration(state, d);
		return 4;
	} else if (strcmp(cmd, "length") == 0) {
		if (!(args) || (((d = strtol(args, NULL, 10))) <= 0)) d = DEFAULT_LENGTH;
		camogm_set_segment_length(state, d);
		return 5;
	} else if (strcmp(cmd, "prefix") == 0) {
		if (args) camogm_set_prefix(state, args, FILE_PATH);
		return 6;
	} else if (strcmp(cmd, "status") == 0) {
		camogm_status(state, args, 0);
		return 7;
	} else if (strcmp(cmd, "xstatus") == 0) {
		camogm_status(state, args, 1);
		return 7;
	} else if (strcmp(cmd, "save_gp") == 0) {
		if ((args) && (((d = strtol(args, NULL, 10))) >= 0)) camogm_set_save_gp(state, d);
		return 8;
	} else if (strcmp(cmd, "exif") == 0) {
		if ((args) && (((d = strtol(args, NULL, 10))) >= 0)) camogm_set_exif(state, d);
		return 8;
	} else if (strcmp(cmd, "debug") == 0) {
		camogm_debug(state, args);
		return 9;
	} else if (strcmp(cmd, "timescale") == 0) {
		dd = strtod(args, NULL);
		camogm_set_timescale(state, dd ? dd : 1.0);
		return 10;
//TODO: fix period calculation/check for frame skipping (just disable in frame skip mode?)
//TODO: add time period (system clock), not just frame skipping


	} else if (strcmp(cmd, "frameskip") == 0) {
		d = strtol(args, NULL, 10);
		camogm_set_frames_skip(state, d);
		return 11;
	} else if (strcmp(cmd, "timelapse") == 0) { // period (in seconds) between stored frames
		d = strtol(args, NULL, 10);
		camogm_set_frames_skip(state, -d);
		return 11;
	} else if (strcmp(cmd, "format") == 0) {
		if (args) {
			if (strcmp(args,  "none") == 0) camogm_set_format(state, 0);
			else if ((strcmp(args, "ogm" ) == 0) || (strcmp(args, "ogg") == 0)) camogm_set_format(state, CAMOGM_FORMAT_OGM);
			else if ((strcmp(args, "jpeg") == 0) || (strcmp(args, "jpg") == 0)) camogm_set_format(state, CAMOGM_FORMAT_JPEG);
			else if (strcmp(args,  "mov" ) == 0) camogm_set_format(state, CAMOGM_FORMAT_MOV);
		}
		return 12;
	} else if (strcmp(cmd, "debuglev") == 0) {
		d = strtol(args, NULL, 10);
		camogm_debug_level(d ? d : 0);
		return 13;
	} else if (strcmp(cmd, "kml") == 0) {
		if ((args) && (((d = strtol(args, NULL, 10))) >= 0)) camogm_kml_set_enable(state, d);
		return 14;
	} else if (strcmp(cmd, "kml_hhf") == 0) {
		dd = strtod(args, NULL);
		camogm_kml_set_horHalfFov(state, dd);
		return 15;
	} else if (strcmp(cmd, "kml_vhf") == 0) {
		dd = strtod(args, NULL);
		camogm_kml_set_vertHalfFov(state, dd);
		return 16;
	} else if (strcmp(cmd, "kml_near") == 0) {
		dd = strtod(args, NULL);
		camogm_kml_set_near(state, dd);
		return 17;
	} else if (strcmp(cmd, "kml_alt") == 0) {
		if (args) {
			if (strcmp(args, "gps"   ) == 0) camogm_kml_set_height_mode(state, 1);
			else if (strcmp(args, "ground") == 0) camogm_kml_set_height_mode(state, 0);
		}
		return 18;
	} else if (strcmp(cmd, "kml_height") == 0) {
		dd = strtod(args, NULL);
		camogm_kml_set_height(state, dd);
		return 19;
	} else if (strcmp(cmd, "kml_period") == 0) {
		d = strtol(args, NULL, 10);
		camogm_kml_set_period(state, d ? d : 1);
		return 20;
	} else if (strcmp(cmd, "frames_per_chunk") == 0) {
		d = strtol(args, NULL, 10);
		camogm_set_frames_per_chunk(state, d);
		return 21;
	} else if (strcmp(cmd, "max_frames") == 0) {
		d = strtol(args, NULL, 10);
		camogm_set_max_frames(state, d);
		return 22;
	} else if (strcmp(cmd, "start_after_timestamp") == 0) {
		dd = strtod(args, NULL);
		camogm_set_start_after_timestamp(state, dd);
		return 23;
	} else if (strcmp(cmd, "greedy") == 0) {
		dd = strtod(args, NULL);
		camogm_set_greedy(state, dd);
		return 24;
	} else if (strcmp(cmd, "ignore_fps") == 0) {
		dd = strtod(args, NULL);
		camogm_set_ignore_fps(state, dd);
		return 25;
	} else if (strcmp(cmd, "port_enable") == 0) {
		d = strtol(args, NULL, 10);
		set_chn_state(state, d, 1);
		return 26;
	} else if (strcmp(cmd, "port_disable") == 0) {
		d = strtol(args, NULL, 10);
		set_chn_state(state, d, 0);
		return 27;
	} else if (strcmp(cmd, "rawdev_path") == 0) {
		if (args) {
			camogm_set_prefix(state, args, RAW_PATH);
		} else {
			state->rawdev_op = 0;
			state->rawdev.rawdev_path[0] = '\0';
		}
		return 28;
	} else if (strcmp(cmd, "reader_stop") == 0) {
		if (state->prog_state == STATE_READING &&
				state->rawdev.thread_state == STATE_RUNNING) {
			state->rawdev.thread_state = STATE_CANCEL;
		} else {
			D0(fprintf(debug_file, "Reading thread is not running, nothing to stop\n"));
		}
		return 29;
	}

	return -1;
}

/**
 * @brief This function closes open files, terminates reading thread and deletes allocated memory.
 * @param[in]   state   pointer to #camogm_state structure for a particular sensor channel
 * return       None
 */
void clean_up(camogm_state *state)
{
	for (int port = 0; port < SENSOR_PORTS; port++) {
		if (is_fd_valid(state->fd_exif[port]))
			close(state->fd_exif[port]);
		if (is_fd_valid(state->fd_head[port]))
			close(state->fd_head[port]);
		if (is_fd_valid(state->fd_circ[port]))
			close(state->fd_circ[port]);
		if (is_fd_valid(state->fd_fparmsall[port]))
			close(state->fd_fparmsall[port]);
	}

	if (state->rawdev.thread_state != STATE_STOPPED) {
		pthread_cancel(state->rawdev.tid);
	}
}

/**
 * @brief Main processing loop
 *
 * If recording is on, this function will check for new commands in command pipe after each frame. If recording is
 * turn off, it will poll command pipe each #COMMAND_LOOP_DELAY microseconds and then sleep.
 * @param[in]   state   #camogm_state structure associated with a single port
 * @return      normally this function loops indefinitely processing commands but will return negative exit code in case
 * of error and @e EXIT_SUCCESS it eventually terminates in normal way.
 */
int listener_loop(camogm_state *state)
{
	FILE *cmd_file;
	int rslt, ret, cmd, f_ok;
	int fp0, fp1;
	int process = 1;
	int curr_port = 0;
	const char *pipe_name = state->pipe_name;

	// create a named pipe
	// always delete the pipe if it existed, start a fresh one
	f_ok = access(pipe_name, F_OK);
	ret = unlink(pipe_name);
	if (ret && f_ok == 0) {
		D1(fprintf(debug_file, "Unlink %s returned %d, errno=%d \n", pipe_name, ret, errno));
	}
	ret = mkfifo(pipe_name, 0777); //EEXIST
	// now should not exist
	if (ret) {
		if (errno == EEXIST) {
			D1(fprintf(debug_file, "Named pipe %s already exists, will use it.\n", pipe_name));
		} else {
			D0(fprintf(debug_file, "Can not create a named pipe %s, errno=%d \n", pipe_name, errno));
			clean_up(state);
			return -4;
		}
	}

	// now open the pipe - will block until something will be written (or just open for writing,
	// reads themselves will not block)
	if (!((cmd_file = fopen(pipe_name, "r")))) {
		D0(fprintf(debug_file, "Can not open command file %s\n", pipe_name));
		clean_up(state);
		return -5;
	}
	D0(fprintf(debug_file, "Pipe %s open for reading\n", pipe_name)); // to make sure something is sent out

	// enter main processing loop
	while (process) {
		curr_port = select_port(state);
		state->port_num = curr_port;
		// look at command queue first
		cmd = parse_cmd(state, cmd_file);
		if (cmd) {
			if (cmd < 0) D0(fprintf(debug_file, "Unrecognized command\n"));
		} else if (state->prog_state == STATE_RUNNING) { // no commands in queue, started
			switch ((rslt = -sendImageFrame(state))) {
			case 0:
				break;                      // frame sent OK, nothing to do (TODO: check file length/duration)
			case CAMOGM_FRAME_NOT_READY:    // just wait for the frame to appear at the current pointer
				// we'll wait for a frame, not to waste resources. But if the compressor is stopped this program will not respond to any commands
				// TODO - add another wait with (short) timeout?
				fp0 = lseek(state->fd_circ[curr_port], 0, SEEK_CUR);
				if (fp0 < 0) {
					D0(fprintf(debug_file, "%s:line %d got broken frame (%d) before waiting for ready\n", __FILE__, __LINE__, fp0));
					rslt = CAMOGM_FRAME_BROKEN;
				} else {
					fp1 = lseek(state->fd_circ[curr_port], LSEEK_CIRC_WAIT, SEEK_END);
					if (fp1 < 0) {
						D0(fprintf(debug_file, "%s:line %d got broken frame (%d) while waiting for ready. Before that fp0=0x%x\n", __FILE__, __LINE__, fp1, fp0));
						rslt = CAMOGM_FRAME_BROKEN;
					} else {
						break;
					}
				}
				// no break
			case  CAMOGM_FRAME_CHANGED:     // frame parameters have changed
			case  CAMOGM_FRAME_NEXTFILE:    // next file needed (need to switch to a new file (time/size exceeded limit)
			case  CAMOGM_FRAME_INVALID:     // invalid frame pointer
			case  CAMOGM_FRAME_BROKEN:      // frame broken (buffer overrun)
				// restart the file
				D3(fprintf(debug_file,"%s:line %d - sendImageFrame() returned -%d\n", __FILE__, __LINE__, rslt));
				camogm_stop(state);
				camogm_start(state);
				break;
			case  CAMOGM_FRAME_FILE_ERR:    // error with file I/O
			case  CAMOGM_FRAME_OTHER:       // other errors
				D0(fprintf(debug_file, "%s:line %d - error=%d\n", __FILE__, __LINE__, rslt));
				break;
			default:
				D0(fprintf(debug_file, "%s:line %d - should not get here (rslt=%d)\n", __FILE__, __LINE__, rslt));
				clean_up(state);
				exit(-1);
			} // switch sendImageFrame()

			if ((rslt != 0) && (rslt != CAMOGM_FRAME_NOT_READY) && (rslt != CAMOGM_FRAME_CHANGED)) state->last_error_code = rslt;
		} else if (state->prog_state == STATE_STARTING) { // no commands in queue,starting (but not started yet)

			// retry starting
			switch ((rslt = -camogm_start(state))) {
			case 0:
				break;                      // file started OK, nothing to do
			case CAMOGM_TOO_EARLY:
				lseek(state->fd_circ[curr_port], LSEEK_CIRC_TOWP, SEEK_END);       // set pointer to the frame to wait for
				lseek(state->fd_circ[curr_port], LSEEK_CIRC_WAIT, SEEK_END);       // It already passed CAMOGM_FRAME_NOT_READY, so compressor may be running already
				break;                                                  // no need to wait extra
			case CAMOGM_FRAME_NOT_READY:                                // just wait for the frame to appear at the current pointer
			// we'll wait for a frame, not to waste resources. But if the compressor is stopped this program will not respond to any commands
			// TODO - add another wait with (short) timeout?
			case  CAMOGM_FRAME_CHANGED:     // frame parameters have changed
			case  CAMOGM_FRAME_NEXTFILE:
			case  CAMOGM_FRAME_INVALID:     // invalid frame pointer
			case  CAMOGM_FRAME_BROKEN:      // frame broken (buffer overrun)
				usleep(COMMAND_LOOP_DELAY); // it should be not too long so empty buffer will not be overrun
				break;
			case  CAMOGM_FRAME_FILE_ERR:    // error with file I/O
			case  CAMOGM_FRAME_OTHER:       // other errors
				D0(fprintf(debug_file, "%s:line %d - error=%d\n", __FILE__, __LINE__, rslt));
				break;
			default:
				D0(fprintf(debug_file, "%s:line %d - should not get here (rslt=%d)\n", __FILE__, __LINE__, rslt));
				clean_up(state);
				exit(-1);
			} // switch camogm_start()
			if ((rslt != 0) && (rslt != CAMOGM_TOO_EARLY) && (rslt != CAMOGM_FRAME_NOT_READY) && (rslt != CAMOGM_FRAME_CHANGED) ) state->last_error_code = rslt;
		} else if (state->prog_state == STATE_READING) {
			usleep(COMMAND_LOOP_DELAY);
		} else {                            // not running, not starting
			state->rawdev.thread_state = STATE_RUNNING;
			usleep(COMMAND_LOOP_DELAY);     // make it longer but interruptible by signals?
		}
	} // while (process)

	// normally, we should not be here
	clean_up(state);
	return EXIT_SUCCESS;
}

/**
 * @brief Return total disk size in bytes
 *
 * This function reads disk size using ioctl call and returns it in bytes.
 * @param   name   pointer to disk name string
 * @return  disk size in bytes if it was read correctly and 0 otherwise
 */
static uint64_t get_disk_size(const char *name)
{
	int fd;
	uint64_t dev_sz;

	if ((fd = open(name, O_RDONLY)) < 0) {
		perror(__func__);
		return 0;
	}
	if (ioctl(fd, BLKGETSIZE64, &dev_sz) < 0) {
		perror(__func__);
		return 0;
	}
	close(fd);

	return dev_sz;
}

/**
 * @brief Make sysfs path from disk name in accordance with the type provided.
 * The 'start' file does not exist for full disk path (/dev/sda) and this function
 * returns empty string in @e sys_name and non-zero return value.
 * @param[in]   dev_name   pointer to a string containing device name
 * @param[out]  sys_name   pointer to a string where sysfs path will be stored
 * @param[in]   str_sz     the size of @e dname and @e sname strings including the
 * terminating null byte
 * @param[in]   type    the type of sysfs path required. Can be one of #sysfs_path_type
 * @return      The number of characters in resulting path, including the terminating
 * null byte, or a negative value in case of an error.
 */
static int get_sysfs_name(const char *dev_name, char *sys_name, size_t str_sz, int type)
{
	int ret = -1;
	const char prefix[] = "/sys/block/";
	char size_name[] = "size";
	char start_name[] = "start";
	char device_name[ELPHEL_PATH_MAX] = {0};
	char disk_name[ELPHEL_PATH_MAX] = {0};
	char part_name[ELPHEL_PATH_MAX] = {0};
	char *postfix = NULL;
	char *ptr = NULL;
	size_t dname_sz = strlen(dev_name) - 1;

	strncpy(device_name, dev_name, ELPHEL_PATH_MAX);

	// strip trailing slash
	if (device_name[dname_sz] == '/') {
		device_name[dname_sz] = '\0';
		dname_sz--;
	}

	// get partition and disk names
	ptr = strrchr(device_name, '/');
	if (ptr == NULL) {
		D0(fprintf(debug_file, "%s: the path specified is invalid\n", __func__));
		return ret;
	}
	strcpy(part_name, ptr + 1);
	strcpy(disk_name, ptr + 1);
	if (strlen(disk_name) > 0)
		disk_name[strlen(disk_name) - 1] = '\0';

	if (type == TYPE_SIZE)
		postfix = size_name;
	else if (type == TYPE_START)
		postfix = start_name;

	if (isdigit(device_name[dname_sz])) {
		// we've got partition
		ret = snprintf(sys_name, str_sz, "%s%s/%s/%s", prefix, disk_name, part_name, postfix);
	} else {
		// we've got entire disk
		ret = snprintf(sys_name, str_sz, "%s%s/%s", prefix, part_name, postfix);
		if (type == TYPE_START)
			// the 'start' does not exist for full disk, notify caller
			// that we've got this situation here
			sys_name[0] = '\0';
	}

	return ret;
}

/**
 * @brief Get disk start and end sectors. If the name specified is the name of a disk instead of
 * the name of a partition on that disk (i.e /dev/sda instead /dev/sda2), then the full size of the
 * disk is returned. This function is used with #set_disk_range to allocate raw device buffer on a disk.
 * @param[in]   name   pointer to disk name string
 * @param[out]  rng    pointer to the structure containing disk size
 * @return      0 if disk size was retrieved successfully and -1 in case of an error
 */
static int get_disk_range(const char *name, struct range *rng)
{
	int ret = 0;
	int fd;
	uint64_t val;
	char data[SMALL_BUFF_LEN] = {0};
	char sysfs_name[ELPHEL_PATH_MAX] = {0};

	// read start LBA
	if (get_sysfs_name(name, sysfs_name, ELPHEL_PATH_MAX, TYPE_START) > 0) {
		if (strlen(sysfs_name) > 0) {
			fd = open(sysfs_name, O_RDONLY);
			if (fd >= 0) {
				read(fd, data, SMALL_BUFF_LEN);
				if ((val = strtoull(data, NULL, 10)) != 0)
					rng->from = val;
				else
					ret = -1;
				close(fd);
			}
		} else {
			rng->from = 0;
		}
	}

	// read disk size in LBA
	if (get_sysfs_name(name, sysfs_name, ELPHEL_PATH_MAX, TYPE_SIZE) > 0) {
		fd = open(sysfs_name, O_RDONLY);
		if (fd >= 0) {
			read(fd, data, SMALL_BUFF_LEN);
			if ((val = strtoull(data, NULL, 10)) != 0)
				rng->to = rng->from + val;
			else
				ret = -1;
			close(fd);
		}
	}

	// sanity check
	if (rng->from >= rng->to)
		ret = -1;

	return ret;
}

/**
 * @brief Pass raw device buffer start and end LBAs to driver via its files in
 * sysfs. This function is used with #get_disk_range to allocate raw device buffer on a disk.
 * @param[in]   name   path to sysfs disk driver entry
 * @param[in]   rng    pointer to structure containing disk size
 * @return      0 if the values from @e rng were set successfully and -1 in case of an error
 */
static int set_disk_range(const struct range *rng)
{
	int fd;
	int ret = 0;
	char buff[SMALL_BUFF_LEN] = {0};
	int len;

	fd = open(SYSFS_AHCI_LBA_START, O_WRONLY);
	if (fd < 0)
		return -1;
	len = snprintf(buff, SMALL_BUFF_LEN, "%llu", rng->from);
	write(fd, buff, len + 1);
	close(fd);

	fd = open(SYSFS_AHCI_LBA_END, O_WRONLY);
	if (fd < 0)
		return -1;
	len = snprintf(buff, SMALL_BUFF_LEN, "%llu", rng->to);
	write(fd, buff, len + 1);
	close(fd);

	return ret;
}

/**
 * @brief Select a sensor channel with minimum free space left in the buffer. The channel will
 * be selected from the list of active channels.
 * @param[in]   state   a pointer to a structure containing current state
 * @return      The number of a channel with minimum free space left. This function
 * will return 0 in case all channels are disabled.
 */
unsigned int select_port(camogm_state *state)
{
	unsigned int chn = 0;
	off_t free_sz;
	off_t file_pos;
	off_t min_sz = -1;

	// define first active channel in case not all of them are active
	for (int i = 0; i < SENSOR_PORTS; i++) {
		if (is_chn_active(state, i)) {
			chn = i;
			break;
		}
	}

	if (state->prog_state == STATE_STARTING || state->prog_state == STATE_RUNNING)
		D6(fprintf(debug_file, "Selecting sensor port, buffer free size: "));
	for (int i = 0; i < SENSOR_PORTS; i++) {
		if (is_chn_active(state, i)) {
			file_pos = lseek(state->fd_circ[i], 0, SEEK_CUR);
			if (file_pos != -1) {
				free_sz = lseek(state->fd_circ[i], LSEEK_CIRC_FREE, SEEK_END);
				lseek(state->fd_circ[i], file_pos, SEEK_SET);
				if (state->prog_state == STATE_STARTING || state->prog_state == STATE_RUNNING)
					D6(fprintf(debug_file, "port %i = %i, ", i, free_sz));
				if ((free_sz < min_sz && free_sz >= 0) || min_sz == -1) {
					min_sz = free_sz;
					chn = i;
				}
			}
		} else {
			if (state->prog_state == STATE_STARTING || state->prog_state == STATE_RUNNING)
				D6(fprintf(debug_file, "port %i is inactive, ", i));
		}
	}
	if (state->prog_state == STATE_STARTING || state->prog_state == STATE_RUNNING)
		D6(fprintf(debug_file, "selected port: %i\n", chn));

	return chn;
}

/**
 * @brief Check if channel is enabled or disabled
 * @param[in]   s      a pointer to a structure containing current state
 * @param[in]  port   a port which should be checked
 * @return     0 if the channel is disabled and 1 otherwise
 */
inline int is_chn_active(camogm_state *s, unsigned int port)
{
	return (s->active_chn >> port) & 0x1;
}

/**
 * @brief Change the state of a given sensor channel.
 * @param[in,out] s      a pointer to a structure containing current state
 * @param[in]     port   port which state should be changed
 * @param[in]     new_state   new state of the channel
 * @return        None
 */
inline void set_chn_state(camogm_state *s, unsigned int port, unsigned int new_state)
{
	if (port >= 0 && port < SENSOR_PORTS) {
		if (new_state)
			s->active_chn |= 1 << port;
		else
			s->active_chn &= ~(1 << port);
	}
}

/**
 * @brief Open files provided by circbuf driver.
 * @param[in,out] state   a pointer to a structure containing current state
 * @return        0 on success or negative error code
 */
int open_files(camogm_state *state)
{
	int ret = 0;

	for (int port = 0; port < SENSOR_PORTS; port++) {
		// open Exif header file
		state->fd_exif[port] = open(exifFileNames[port], O_RDONLY);
		if (state->fd_exif[port] < 0) { // check control OK
			D0(fprintf(debug_file, "Error opening %s\n", exifFileNames[port]));
			clean_up(state);
			return -1;
		}

		// open JPEG header file
		state->fd_head[port] = open(headFileNames[port], O_RDWR);
		if (state->fd_head[port] < 0) { // check control OK
			D0(fprintf(debug_file, "Error opening %s\n", headFileNames[port]));
			clean_up(state);
			return -1;
		}
		state->head_size[port] = lseek(state->fd_head[port], 0, SEEK_END);
		if (state->head_size[port] > JPEG_HEADER_MAXSIZE) {
			D0(fprintf(debug_file, "%s:%d: Too big JPEG header (%d > %d)", __FILE__, __LINE__, state->head_size[port], JPEG_HEADER_MAXSIZE ));
			clean_up(state);
			return -2;
		}

		// open circbuf and mmap it (once at startup)
		state->fd_circ[port] = open(circbufFileNames[port], O_RDWR);
		if (state->fd_circ < 0) { // check control OK
			D0(fprintf(debug_file, "Error opening %s\n", circbufFileNames[port]));
			clean_up(state);
			return -2;
		}
		// find total buffer length (it is in defines, actually in c313a.h
		state->circ_buff_size[port] = lseek(state->fd_circ[port], 0, SEEK_END);
		ccam_dma_buf[port] = (unsigned long*)mmap(0, state->circ_buff_size[port], PROT_READ, MAP_SHARED, state->fd_circ[port], 0);
		if ((int)ccam_dma_buf[port] == -1) {
			D0(fprintf(debug_file, "Error in mmap of %s\n", circbufFileNames[port]));
			clean_up(state);
			return -3;
		}

		// now open/mmap file to read sensor/compressor parameters (currently - just free memory in circbuf and compressor state)
		state->fd_fparmsall[port] = open(ctlFileNames[port], O_RDWR);
		if (state->fd_fparmsall[port] < 0) { // check control OK
			D0(fprintf(debug_file, "%s:%d:%s: Error opening %s\n", __FILE__, __LINE__, __FUNCTION__, ctlFileNames[port]));
			clean_up(state);
			return -2;
		}

		// now try to mmap
		// PROT_WRITE - only to write acknowledge
		frameParsAll[port] = (struct framepars_all_t*)mmap(0, sizeof(struct framepars_all_t), PROT_READ | PROT_WRITE, MAP_SHARED, state->fd_fparmsall[port], 0);
		if ((int)frameParsAll[port] == -1) {
			D0(fprintf(debug_file, "%s:%d:%s: Error in mmap in %s\n", __FILE__, __LINE__, __FUNCTION__, ctlFileNames[port]));
			clean_up(state);
			return -3;
		}
		framePars[port] = frameParsAll[port]->framePars;
		aglobalPars[port] = frameParsAll[port]->globalPars;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	const char usage[] =   "This program allows recording of the video/images acquired by Elphel camera to the storage media.\n" \
			     "It is designed to run in the background and accept commands through a named pipe or a socket.\n\n" \
			     "Usage:\n\n" \
			     "%s -n <named_pipe_name> -p <port_number> [-s state_file_name]\n\n"	\
			     "i.e.:\n\n" \
			     "%s -n /var/state/camogm_cmd -p 1234 -s /mnt/sda1/write_pos\n\n" \
			     "When the program is running you may send commands by writing strings to the command file\n" \
			     "(/var/state/camogm_cmd in the example above) or to the socket. The complete list of available commands is available\n" \
			     "on Elphel Wiki (http://wiki.elphel.com/index.php?title=Camogm), here is the example of usage\n" \
			     "from the shell prompt in the camera:\n\n"	\
			     "echo \"status; exif=1; format=jpeg;status=/var/tmp/camogm.status\" > /var/state/camogm_cmd\n\n" \
			     "That will print status information on the standard output (may not be visible if the program was not\n" \
			     "started from the same session), set exif mode on (each frame will have the full Exif header including\n" \
			     "a precise time stamp), set output format to a series of individual JPEG files, and then send status\n" \
			     "information to a file /var/tmp/camogm.status in the camera file system.\n\n" \
			     "This program does not control the process of acquisition of the video/images to the camera internal\n" \
			     "buffer, it only retrieves that data from the buffer (waiting when needed), packages it to selected\n" \
			     "format and stores the result files.\n\n";
	int ret;
	int opt;
	uint16_t port_num = 0;
	size_t str_len;
	char pipe_name_str[ELPHEL_PATH_MAX] = {0};
	char state_name_str[ELPHEL_PATH_MAX] = {0};

	if ((argc < 5) || (argv[1][1] == '-')) {
		printf(usage, argv[0], argv[0]);
		return EXIT_SUCCESS;
	}
	while ((opt = getopt(argc, argv, "n:p:s:h")) != -1) {
		switch (opt) {
		case 'n':
			strncpy(pipe_name_str, (const char *)optarg, ELPHEL_PATH_MAX - 1);
			break;
		case 'p':
			port_num = (uint16_t)atoi((const char *)optarg);
			break;
		case 'h':
			printf(usage, argv[0], argv[0]);
			return EXIT_SUCCESS;
		case 's':
			strncpy(state_name_str, (const char *)optarg, ELPHEL_PATH_MAX - 1);
			break;
		}
	}

	camogm_init(&sstate, pipe_name_str, port_num);
	if (pthread_mutex_init(&sstate.mutex, NULL) != 0) {
		perror("Unable to initialize mutex\n");
		return EXIT_FAILURE;
	}
	ret = open_files(&sstate);
	if (ret < 0)
		return ret;
	check_compressors(&sstate);
	if (pthread_create(&sstate.rawdev.tid, NULL, reader, &sstate) != 0 ||
			pthread_detach(sstate.rawdev.tid) != 0) {
		D0(fprintf(debug_file, "%s:line %d: Can not start reading thread in detached state\n", __FILE__, __LINE__));
		clean_up(&sstate);
		return EXIT_FAILURE;
	}
	sstate.rawdev.thread_state = STATE_RUNNING;
	str_len = strlen(state_name_str);
	if (str_len > 0) {
		strncpy(&sstate.rawdev.state_path, state_name_str, str_len + 1);
	}

	ret = listener_loop(&sstate);

	return ret;
}

/**
 * @brief Read either G_* parameter (these are 'regular' values defined by number) or P_* parameter
 * (it can be read for up to 6 frames ahead, but current interface only allows to read last/current value)
 * @param[in]   GPNumber   parameter number (as defined in c313a.h), G_* parameters have numbers above FRAMEPAR_GLOBALS, P_* - below)
 * @param[in]   port       sensor port number for which a parameter is needed
 * @return      parameter value
 */
unsigned long getGPValue(unsigned int port, unsigned long GPNumber)
{
	return (GPNumber >= FRAMEPAR_GLOBALS) ?
	       GLOBALPARS(port, GPNumber) :
	       framePars[port][GLOBALPARS(port, G_THIS_FRAME) & PARS_FRAMES_MASK].pars[GPNumber];
}

/**
 * @brief Set value of the specified global (G_*) parameter
 * @param[in]   port      sensor port number
 * @param[in]   GNumber   parameter number (as defined in c313a.h)
 * @param[in]   value     value to set
 * @return      None
 */
void setGValue(unsigned int port, unsigned long GNumber, unsigned long value)
{
	GLOBALPARS(port, GNumber) = value;
}

/**
 * @brief Check if this application is enabled (by appropriate bit in P_DAEMON_EN), if not -
 * and wait until enabled (return false when enabled)
 * @param[in]   port        sensor port number
 * @param[in]   daemonBit   bit number to accept control in P_DAEMON_EN parameter
 * @return      (after possible waiting) true if there was no waiting, false if there was waiting
 */
int waitDaemonEnabled(unsigned int port, int daemonBit)   // <0 - use default
{
	camogm_state *state = &sstate;
	if ((daemonBit >= 0) && (daemonBit < 32)) lastDaemonBit[port] = daemonBit;
	unsigned long this_frame = GLOBALPARS(port, G_THIS_FRAME);
	// No semaphors, so it is possible to miss event and wait until the streamer will be re-enabled before sending message,
	// but it seems not so terrible
	lseek(state->fd_circ[port], LSEEK_DAEMON_CIRCBUF + lastDaemonBit[port], SEEK_END);
	if (this_frame == GLOBALPARS(port, G_THIS_FRAME)) return 1;
	return 0;
}

/**
 * @brief Check if this application is enabled (by appropriate bit in P_DAEMON_EN)
 * @param[in]   port        sensor port number
 * @param[in]   daemonBit   bit number to accept control in P_DAEMON_EN parameter
 * @return      (after possible waiting) true if there was no waiting, false if there was waiting
 */
int isDaemonEnabled(unsigned int port, int daemonBit)   // <0 - use default
{
	if ((daemonBit >= 0) && (daemonBit < 32)) lastDaemonBit[port] = daemonBit;
	return ((framePars[port][GLOBALPARS(port, G_THIS_FRAME) & PARS_FRAMES_MASK].pars[P_DAEMON_EN] & (1 << lastDaemonBit[port])) != 0);
}

/**
 * @brief Check if file descriptor is valid. This function is used to check if file
 * is opened before closing it.
 * @param[in]   fd   file descriptor to check
 * @return      1 if descriptor is valid and 0 otherwise
 */
inline int is_fd_valid(int fd)
{
	return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}
