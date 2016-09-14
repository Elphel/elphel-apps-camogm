/** @file camogm.h
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

#ifndef _CAMOGM_H
#define _CAMOGM_H

#include <pthread.h>
#include <stdbool.h>
#include <ogg/ogg.h>
#include "ogmstreams.h"
#include <elphel/exifa.h>
#include <elphel/c313a.h>
#include <elphel/x393_devices.h>


#define CAMOGM_FRAME_NOT_READY    1        ///< frame pointer valid, but not yet acquired
#define CAMOGM_FRAME_INVALID      2        ///< invalid frame pointer
#define CAMOGM_FRAME_CHANGED      3        ///< frame parameters have changed
#define CAMOGM_FRAME_NEXTFILE     4        ///< need to switch to a new file segment
#define CAMOGM_FRAME_BROKEN       5        ///< frame broken (buffer overrun)
#define CAMOGM_FRAME_FILE_ERR     6        ///< error with file I/O
#define CAMOGM_FRAME_MALLOC       7        ///< can not allocate memory
#define CAMOGM_TOO_EARLY          8        ///< too early to start, waiting for particular timestamp
#define CAMOGM_FRAME_OTHER        9        ///< other errors
#define CAMOGM_NO_SPACE           10       ///< no free space left on current file system

#define CAMOGM_FORMAT_NONE        0        ///< no video output
#define CAMOGM_FORMAT_OGM         1        ///< output as Ogg Media file
#define CAMOGM_FORMAT_JPEG        2        ///< output as individual JPEG files
#define CAMOGM_FORMAT_MOV         3        ///< output as Apple Quicktime

#define D(x) { if (debug_file && debug_level) { x; fflush(debug_file); } }
#define D0(x) { if (debug_file) { pthread_mutex_lock(&print_mutex); x; fflush(debug_file); pthread_mutex_unlock(&print_mutex); } }
#define D1(x) { if (debug_file && (debug_level > 0)) { pthread_mutex_lock(&print_mutex); x; fflush(debug_file); pthread_mutex_unlock(&print_mutex); } }
#define D2(x) { if (debug_file && (debug_level > 1)) { pthread_mutex_lock(&print_mutex); x; fflush(debug_file); pthread_mutex_unlock(&print_mutex); } }
#define D3(x) { if (debug_file && (debug_level > 2)) { pthread_mutex_lock(&print_mutex); x; fflush(debug_file); pthread_mutex_unlock(&print_mutex); } }
#define D4(x) { if (debug_file && (debug_level > 3)) { pthread_mutex_lock(&print_mutex); x; fflush(debug_file); pthread_mutex_unlock(&print_mutex); } }
#define D5(x) { if (debug_file && (debug_level > 4)) { pthread_mutex_lock(&print_mutex); x; fflush(debug_file); pthread_mutex_unlock(&print_mutex); } }
#define D6(x) { if (debug_file && (debug_level > 5)) { pthread_mutex_lock(&print_mutex); x; fflush(debug_file); pthread_mutex_unlock(&print_mutex); } }

//#define DD(x)
#define DD(x)  { if (debug_file) { fprintf(debug_file, "%s:%d:", __FILE__, __LINE__); x; fflush(debug_file); } }

/** @brief HEADER_SIZE is defined to be larger than actual header (with EXIF) to use compile-time buffer */
#define JPEG_HEADER_MAXSIZE       0x300
/** @brief Offset from the beginning of raw device buffer. Must be aligned to physical sector size */
#define RAWDEV_START_OFFSET       1024
/** @brief Maximum length of file or raw device path */
#define ELPHEL_PATH_MAX           300
#define MMAP_CHUNK_SIZE           10485760
/** @brief Time interval (in microseconds) for processing commands */
#define COMMAND_LOOP_DELAY        500000

/**
 * @enum state_flags
 * @brief Program state flags
 */
enum state_flags {
	STATE_STOPPED,
	STATE_STARTING,
	STATE_RUNNING,
	STATE_READING,
	STATE_CANCEL
};

/**
 * @struct rawdev_buffer
 * @brief Holds pointers related to raw device buffer operation
 * @var rawdev_buffer::rawdev_fd
 * File descriptor of open raw device
 * @var rawdev_buffer::rawdev_path
 * A string containing full path to raw device
 * @var rawdev_buffer::overrun
 * The number of times the buffer has overrun during current work session
 * @var rawdev_buffer::start_pos
 * The start position of raw device buffer
 * @var rawdev_buffer::end_pos
 * The end position of raw device buffer
 * @var rawdev_buffer::curr_pos_r
 * Current read position in raw device buffer
 * @var rawdev_buffer::curr_pos_w
 * Current write position in raw device buffer
 * @var rawdev_buffer::mmap_default_size
 * The default size of memory mapped disk region
 * @var rawdev_buffer::mmap_current_size
 * The size of currently memory mapped disk region. Can be less then #mmap_default_size
 * @var rawdev_buffer::mmap_offset
 * Current offset (in bytes) from the beginning of raw device buffer
 * @var rawdev_buffer::file_start
 * Pointer to the beginning of current file. This pointer is set during raw device reading and
 * updated every time new file is found.
 * @var rawdev_buffer::tid
 * The ID of raw device reading thread
 * @var rawdev_buffer::thread_state
 * The state of the reading thread. Used to interrupt current operation
 * @var rawdev_buffer::disk_mmap
 * Pointer to memory mapped buffer region
 */
typedef struct {
	int rawdev_fd;
	char rawdev_path[ELPHEL_PATH_MAX];
	uint32_t overrun;
	uint64_t start_pos;
	uint64_t end_pos;
	volatile uint64_t curr_pos_r;
	uint64_t curr_pos_w;
	uint64_t mmap_default_size;
	uint64_t mmap_current_size;
	uint64_t mmap_offset;
	uint64_t file_start;
	pthread_t tid;
	volatile int thread_state;
	unsigned char *disk_mmap;
	int sysfs_fd;
} rawdev_buffer;

/**
 * @struct camogm_state
 * @brief Holds current state of the running program
 */
typedef struct {
	int segment_duration;
	int segment_length;
	int greedy;
	int ignore_fps;
	int save_gp;                                            ///< if non zero, current circbuf pointer will be saved to global pointer, so imgsrv can report /pointers
	char path_prefix[256];                                  ///< file name prefix
	char path[ELPHEL_PATH_MAX];                             ///< full file name
	int cirbuf_rp[SENSOR_PORTS];                            ///< -1 means the pointer is invalid
	int fd_circ[SENSOR_PORTS];                              ///< file descriptor for circbuf
	int fd_head[SENSOR_PORTS];                              ///< file descriptor for JPEG header
	int fd_fparmsall[SENSOR_PORTS];                         ///< file descriptor for sensor/compressor parameters
	int fd_exif[SENSOR_PORTS];                              ///< file descriptor for Exif data
	int head_size[SENSOR_PORTS];                            ///< JPEG header size
	unsigned char jpegHeader[SENSOR_PORTS][JPEG_HEADER_MAXSIZE];
	int metadata_start;
	struct interframe_params_t frame_params[SENSOR_PORTS];
	struct interframe_params_t this_frame_params[SENSOR_PORTS];
	int jpeg_len;
	int frame_period[SENSOR_PORTS];                         ///< in microseconds (1/10 of what is needed for the Ogm header)
	int width;                                              ///< image width
	int height;                                             ///< image height
	volatile int prog_state;                                ///< program state flag, can be one of #state_flags
	pthread_mutex_t mutex;                                  ///< mutex for @e prog_state variable; all modifications to the variable must be using this mutex
	int last_error_code;
	ogg_stream_state os;
	ogg_page og;
	ogg_packet op;
	elph_ogg_packet eop;
	int serialno;
	ogg_int64_t packetno;
	ogg_int64_t granulepos;
	FILE*                 vf;                               ///< video file (ogm, fopen)
	int ivf;                                                ///< video file (jpeg, mov - open)
	int last;                                               ///< last packet in a file

	int exif;                                               ///< flag indicating that Exif headers should be calculated and included in each frame
	int exifSize[SENSOR_PORTS];                             ///< signed
	unsigned char ed[SENSOR_PORTS][MAX_EXIF_SIZE];

	int circ_buff_size[SENSOR_PORTS];
	char debug_name[256];
	double timescale;                                       ///< current timescale, default 1.0
	double set_timescale;
	double start_after_timestamp;                           ///< delay recording start to after frame timestamp
	int max_frames;
	int set_max_frames;
	int frames_per_chunk;
	int set_frames_per_chunk;                               ///< quicktime -  index for fast forward?
	int frameno;
	int *frame_lengths;
	off_t frame_data_start;                                 ///< Quicktime (and else?) - frame data start (0xff 0xd8...)
	ogg_int64_t time_unit;
	int formats;                                            ///< bitmask of used (initialized) formats
	int format;                                             ///< output file format
	int set_format;                                         ///< output format to set (will be updated after stop)
	elph_packet_chunk packetchunks[8];
	int chunk_index;
	int buf_overruns[SENSOR_PORTS];
	int buf_min[SENSOR_PORTS];
	int set_frames_skip;                                    ///< will be copied to frames_skip if stopped or at start
	int frames_skip;                                        ///< number of frames to skip after the one recorded (for time lapse)
	                                                        ///< if negative - -(interval between frames in seconds)
	int frames_skip_left[SENSOR_PORTS];                     ///< number of frames left to skip before the next one to be processed
	                                                        ///< if (frames_skip <0) - next timestamp to save an image
    // kml stuff
	int kml_enable;                                         ///< enable KML file generation
	int kml_used;                                           ///< KML file generation used (change only when stopped)
	char kml_path[300];                                     ///< full path for KML file (if any)
	FILE* kml_file;                                         ///< stream to write kml file
	double kml_horHalfFov;                                  ///< half horizontal Fov (degrees)
	double kml_vertHalfFov;                                 ///< half vertical Fov (degrees)
	double kml_near;                                        ///< Use in KML "near" parameter (<=0 - don't use it)
	int kml_height_mode;                                    ///< 1 - actual, 0 - ground
	double kml_height;                                      ///< extra height to add
	int kml_period;                                         ///< generate PhotoOverlay for each kml_period seconds;
	int kml_last_ts;                                        ///< last generated kml file timestamp
	int kml_last_uts;                                       ///< last generated kml file timestamp, microseconds
	struct exif_dir_table_t kml_exif[ExifKmlNumber];        ///< store locations of the fields needed for KML generations in the Exif block

	unsigned int port_num;                                  ///< sensor port we are currently working with
	char *pipe_name;                                        ///< command pipe name
	int rawdev_op;                                          ///< flag indicating writing to raw device
	rawdev_buffer rawdev;                                   ///< contains pointers to raw device buffer
	unsigned int active_chn;                                ///< bitmask of active sensor ports
	uint16_t sock_port; 									///< command socket port number
} camogm_state;

extern int debug_level;
extern FILE* debug_file;
extern pthread_mutex_t print_mutex;

void put_uint16(void *buf, u_int16_t val);
void put_uint32(void *buf, u_int32_t val);
void put_uint64(void *buf, u_int64_t val);
unsigned long getGPValue(unsigned int port, unsigned long GPNumber);
void setGValue(unsigned int port, unsigned long GNumber, unsigned long value);
int waitDaemonEnabled(unsigned int port, int daemonBit);
int isDaemonEnabled(unsigned int port, int daemonBit);
int is_fd_valid(int fd);

#endif /* _CAMOGM_H */
