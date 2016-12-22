/** @brief This define is needed to use lseek64 and should be set before includes */
#define _LARGEFILE64_SOURCE

#include <stdbool.h>
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
#include <signal.h>
#include <elphel/ahci_cmd.h>

#include "camogm.h"
#include "camogm_read.h"

#define DEFAULT_DEBUG_LVL         6
#define DEFAULT_EXIF              0
#define COMMAND_LOOP_DELAY        500000
#define MMAP_BUFF_SIZE            0x4000000
#define SENSOR_PORTS              4
#define SMALL_BUFF_LEN            32
/** State file record format. It includes device path in /dev, starting, current and ending LBAs */
#define STATE_FILE_FORMAT         "%s\t%llu\t%llu\t%llu\n"
/** Block size in bytes and double words */
#undef BLOCK_SIZE
#define BLOCK_SIZE                512
#define BLOCK_SIZE_DW             128
/** Maximum number of consequent sectors with errors before counter is resynchronized */
#define MAX_ERR_LBA               3


// redefine debug macros define in camogm.h
#undef D
#undef D0
#undef D1
#undef D2
#undef D3
#undef D4
#undef D5
#undef D6
#define D(x) { if (debug_file && debug_level) { x; fflush(debug_file); } }
#define D0(x) { if (debug_file) { x; fflush(debug_file); } }
#define D1(x) { if (debug_file && (debug_level > 0)) { x; fflush(debug_file); } }
#define D2(x) { if (debug_file && (debug_level > 1)) { x; fflush(debug_file); } }
#define D3(x) { if (debug_file && (debug_level > 2)) { x; fflush(debug_file); } }
#define D4(x) { if (debug_file && (debug_level > 3)) { x; fflush(debug_file); } }
#define D5(x) { if (debug_file && (debug_level > 4)) { x; fflush(debug_file); } }
#define D6(x) { if (debug_file && (debug_level > 5)) { x; fflush(debug_file); } }

struct dd_params {
	uint64_t pos_start;
	unsigned long block_size;
	unsigned long block_count;
	unsigned long block_count_init;
	time_t start_time;
};

/** Statistics to be written to file */
struct stat_data {
	unsigned long irq_delay;      // IRQ delay, in ms
	unsigned long mb_written;     // MB written since last delay
	float time_diff;              // time since last delay
};

enum sysfs_path_type {
	TYPE_START,
	TYPE_SIZE
};

const int port = 0;               // use this to simplify code copying from camogm.c
const char *circbufFileNames[] = {DEV393_PATH(DEV393_CIRCBUF0), DEV393_PATH(DEV393_CIRCBUF1),
                                  DEV393_PATH(DEV393_CIRCBUF2), DEV393_PATH(DEV393_CIRCBUF3)
};
unsigned long *ccam_dma_buf[SENSOR_PORTS];
int debug_level;
FILE* debug_file;
static volatile bool keep_running = true;

int open_files(camogm_state *state);
void clean_up(camogm_state *state);
inline int is_fd_valid(int fd);
void camogm_init(camogm_state *state, char *pipe_name, uint16_t port_num);
int listener_loop(camogm_state *state, struct dd_params *dd_params);
unsigned int select_port(camogm_state *state);
void camogm_reset(camogm_state *state);
int camogm_debug_level(int d);
int sendImageFrame(camogm_state *state);
void  camogm_set_prefix(camogm_state *state, const char * p);
int start_recording(camogm_state *state);
int end_recording(camogm_state *state);
void prep_data(camogm_state *state, struct dd_params *dd_params);

static uint64_t get_disk_size(const char *name);
static int get_disk_range(const char *name, struct range *rng);
static int set_disk_range(const struct range *rng);
static int get_sysfs_name(const char *dev_name, char *sys_name, size_t str_sz, int type);
static int find_state(FILE *f, uint64_t *pos, const rawdev_buffer *rawdev);
static int open_state_file(const rawdev_buffer *rawdev);
static int save_state_file(const rawdev_buffer *rawdev);
static int get_disk_range_from_driver(struct range *range);
static void read_stat(unsigned long data_cntr, const char *stat_file);
static void start_test(camogm_state *state, const unsigned long max_cntr, const struct range *range);
static void save_stat(const char *stat_file, struct stat_data *data);
static void int_handler(int data);

/** Process SIGINT signal (Ctrl-C pressed) */
static void int_handler(int data)
{
	keep_running = false;
}

void camogm_init(camogm_state *state, char *pipe_name, uint16_t port_num)
{
	const char sserial[] = "elp0";
	int * ipser = (int*)sserial;

	memset(state, 0, sizeof(camogm_state));
	camogm_reset(state);                    // sets state->buf_overruns =- 1
	state->serialno = ipser[0];
	debug_file = stderr;
	camogm_debug_level(DEFAULT_DEBUG_LVL);
	strcpy(state->debug_name, "stderr");
	state->exif = DEFAULT_EXIF;
	state->frame_lengths = NULL;

	state->pipe_name = pipe_name;
	state->rawdev.start_pos = RAWDEV_START_OFFSET;
	state->rawdev.end_pos = state->rawdev.start_pos;
	state->rawdev.curr_pos_w = state->rawdev.start_pos;
	state->rawdev.curr_pos_r = state->rawdev.start_pos;
	// set first channel active (port 0 defined as constant)
	state->active_chn = 0x1;
	state->sock_port = port_num;
}

void camogm_reset(camogm_state *state)
{
	for (int chn = 0; chn < SENSOR_PORTS; chn++) {
		state->cirbuf_rp[chn] = -1;
		state->buf_overruns[chn] = -1; // first overrun will not count
	}
}

int camogm_debug_level(int d)
{
	debug_level = d;
	return 0;
}

void clean_up(camogm_state *state)
{
	if (is_fd_valid(state->fd_circ[port])) {
		munmap(ccam_dma_buf[port], state->circ_buff_size[port]);
		close(state->fd_circ[port]);
	}
}

inline int is_fd_valid(int fd)
{
	return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

int open_files(camogm_state *state)
{
	int ret = 0;

	// open circbuf and mmap it (once at startup)
	state->fd_circ[port] = open(circbufFileNames[port], O_RDWR);
	if (state->fd_circ < 0) { // check control OK
		D0(fprintf(debug_file, "Error opening %s\n", circbufFileNames[port]));
		clean_up(state);
		return -2;
	}
	state->circ_buff_size[port] = MMAP_BUFF_SIZE;
	// PROT_EXEC flag is set to make __clear_cache() work properly (that was advised on the web)
	ccam_dma_buf[port] = (unsigned long*)mmap(0, state->circ_buff_size[port], PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED, state->fd_circ[port], 0);
	if ((int)ccam_dma_buf[port] == -1) {
		D0(fprintf(debug_file, "Error in mmap of %s\n", circbufFileNames[port]));
		clean_up(state);
		return -3;
	} else {
		D0(fprintf(debug_file, "successfully mmap cirbuf region\n"));
	}

	return ret;
}

int sendImageFrame(camogm_state *state)
{
	int ret = 0;
	struct frame_data fdata = {0};

	if (state->rawdev_op) {
		fdata.sensor_port = port;
		fdata.cirbuf_ptr = state->cirbuf_rp[port];
		fdata.jpeg_len = state->jpeg_len;
		if (state->exif) {
			fdata.meta_index = state->this_frame_params[port].meta_index;
			fdata.cmd |= DRV_CMD_EXIF;
		}
		fdata.cmd |= DRV_CMD_WRITE_TEST;
		ret = write(state->rawdev.sysfs_fd, &fdata, sizeof(struct frame_data));
		if (ret < 0) {
//			D0(fprintf(debug_file, "Can not pass IO vector to driver (driver may be busy): %s\r", strerror(errno)));
			ret = errno;
		} else {
			ret = 0;
		}
	}

	return ret;
}

void  camogm_set_prefix(camogm_state *state, const char * p)
{
	struct range rng = {0};

	strncpy(state->rawdev.rawdev_path, p, sizeof(state->rawdev.rawdev_path) - 1);
	state->rawdev.rawdev_path[sizeof(state->rawdev.rawdev_path) - 1] = '\0';
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

/** Get full disk size in bytes */
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

/** Get starting and endign LBAs of the partition specified as raw device buffer */
static int get_disk_range_from_driver(struct range *range)
{
	FILE *f;

	// get raw device buffer starting postion on disk
	f = fopen(SYSFS_AHCI_LBA_START, "r");
	if (f == NULL) {
		return -1;
	}
	fscanf(f, "%llu\n", &range->from);
	fclose(f);

	// get raw device buffer ending postion on disk
	f = fopen(SYSFS_AHCI_LBA_END, "r");
	if (f == NULL) {
		return -1;
	}
	fscanf(f, "%llu\n", &range->to);
	fclose(f);

	return 0;
}

/** Get write pointer from a file. This functions check not only the name of a partition, but
 * its geometry as well */
static int find_state(FILE *f, uint64_t *pos, const rawdev_buffer *rawdev)
{
	size_t len;
	uint64_t start_pos, curr_pos, end_pos;
	struct range range;
	char buff[ELPHEL_PATH_MAX];
	char dev_name[ELPHEL_PATH_MAX];

	if (f == NULL || pos == NULL)
		return -1;
	if (get_disk_range_from_driver(&range) != 0) {
		return -1;
	}

	// skip first line containing file header
	fgets(buff, ELPHEL_PATH_MAX, f);
	while (fgets(buff, ELPHEL_PATH_MAX, f) != NULL) {
		sscanf(buff, STATE_FILE_FORMAT, dev_name, &start_pos, &curr_pos, &end_pos);
		len = strlen(dev_name);
		if (strncmp(rawdev->rawdev_path, dev_name, len) == 0 &&
				range.from == start_pos &&
				range.to == end_pos) {
			*pos = curr_pos;
			break;
		}
	}

	return 0;
}

/** Read state from file and restore disk write pointer */
static int open_state_file(const rawdev_buffer *rawdev)
{
	int fd, len;
	FILE *f;
	int ret = 0;
	uint64_t curr_pos;
	char buff[SMALL_BUFF_LEN] = {0};

	if (strlen(rawdev->state_path) == 0) {
		return ret;
	}

	f = fopen(rawdev->state_path, "r");
	if (f != NULL) {
		if (find_state(f, &curr_pos, rawdev) != -1) {
			fd = open(SYSFS_AHCI_LBA_CURRENT, O_WRONLY);
			if (fd >= 0) {
				len = snprintf(buff, SMALL_BUFF_LEN, "%llu", curr_pos);
				write(fd, buff, len + 1);
				close(fd);
			} else {
				ret = -1;
			}
		}
		fclose(f);
	} else {
		ret = -1;
	}

	return ret;
}

/** Save current position of the disk write pointer */
static int save_state_file(const rawdev_buffer *rawdev)
{
	int ret = 0;
	FILE *f;
	struct range range;
	uint64_t curr_pos;

	if (strlen(rawdev->state_path) == 0) {
		return ret;
	}
	if (get_disk_range_from_driver(&range) != 0) {
		return -1;
	}

	// get raw device buffer current postion on disk, this position indicates where recording has stopped
	f = fopen(SYSFS_AHCI_LBA_CURRENT, "r");
	if (f == NULL) {
		return -1;
	}
	fscanf(f, "%llu\n", &curr_pos);
	fclose(f);

	// save pointers to a regular file
	f = fopen(rawdev->state_path, "w");
	if (f == NULL) {
		return -1;
	}
	fprintf(f, "Device\t\tStart LBA\tCurrent LBA\tEnd LBA\n");
	fprintf(f, STATE_FILE_FORMAT, rawdev->rawdev_path, range.from, curr_pos, range.to);
	fflush(f);
	fsync(fileno(f));
	fclose(f);

	return ret;
}

/** Read recent statistics from sysfs and print it to debug file (or stdout).
 * This function holds some data in static variables and thus is not reentrant. */
static void read_stat(unsigned long data_cntr, const char *stat_file)
{
	int fd, err;
	const char *stat_delay_name = "stat_irq_delay";
	char file_name[ELPHEL_PATH_MAX];
	char data[SMALL_BUFF_LEN];
	unsigned long val = 0;
	ssize_t len;
	clock_t time, time_diff;
	struct stat_data stat_data = {0};
	static clock_t prev_time;
	static unsigned long prev_cntr;

	len = strlen(SYSFS_AHCI_ENTRY);
	strncpy(file_name, SYSFS_AHCI_ENTRY, ELPHEL_PATH_MAX - 1);
	strncat(&file_name[len], stat_delay_name, ELPHEL_PATH_MAX - len - 1);
	fd = open(file_name, O_RDWR);
	if (fd >= 0) {
		len = read(fd, data, SMALL_BUFF_LEN - 1);
		if (len > 0) {
			val = strtoul(data, NULL, 10);
			// reset this statistics sample
			write(fd, data, len);
		}
		close(fd);
		if (val != 0) {
			time = clock();
			time_diff = time - prev_time;
			prev_time = time;
			stat_data.time_diff = (float)time_diff / CLOCKS_PER_SEC;
			stat_data.irq_delay = val;
			stat_data.mb_written = data_cntr - prev_cntr;
			D0(fprintf(debug_file, "\nIRQ delay: %lu ms, %f s since last delay, %lu MB recorded since last delay\n",
					val, stat_data.time_diff, stat_data.mb_written));
			prev_cntr = data_cntr;
			save_stat(stat_file, &stat_data);
		}
	} else {
		err = errno;
		D0(fprintf(debug_file, "\nerror opening %s: %s\n", file_name, strerror(err)));
	}
}

/** Save recording statistics to file */
static void save_stat(const char *stat_file, struct stat_data *data)
{
	FILE *fd;

	if (strlen(stat_file) != 0) {
		fd = fopen(stat_file, "a");
		if (fd != NULL) {
			fprintf(fd, "IRQ delay: %lu ms; %f s since last delay; %lu MB recorded since last delay\n",
					data->irq_delay, data->time_diff, data->mb_written);
			fclose(fd);
		}
	}
}

unsigned int select_port(camogm_state *state)
{
	// do not process commands
	return port;
}

int parse_cmd(camogm_state *state, FILE* npipe)
{
	return 0;
}

/** Open command file in sysfs */
int start_recording(camogm_state *state)
{
	if (state->rawdev_op) {
		if (open_state_file(&state->rawdev) != 0) {
			D0(fprintf(debug_file, "Could not set write pointer via sysfs, recording will start from the beginning of partition: "
					"%s\n", state->rawdev.rawdev_path));
		}
		state->rawdev.sysfs_fd = open(SYSFS_AHCI_WRITE, O_WRONLY);
		D6(fprintf(debug_file, "Open sysfs file: %s\n", SYSFS_AHCI_WRITE));
		if (state->rawdev.sysfs_fd < 0) {
			D0(fprintf(debug_file, "Error opening sysfs file: %s\n", SYSFS_AHCI_WRITE));
			return -CAMOGM_FRAME_FILE_ERR;
		}
	}

	return 0;
}

/** Close command file in sysfs */
int end_recording(camogm_state *state)
{
	int ret = 0;
	struct frame_data fdata = {0};

	if (state->rawdev_op) {
		fdata.cmd = DRV_CMD_FINISH;
		if (write(state->rawdev.sysfs_fd, &fdata, sizeof(struct frame_data)) < 0) {
			D0(fprintf(debug_file, "Error sending 'finish' command to driver\n"));
		}
		D6(fprintf(debug_file, "Closing sysfs file %s\n", SYSFS_AHCI_WRITE));
		ret = close(state->rawdev.sysfs_fd);
		if (ret == -1)
			D0(fprintf(debug_file, "Error: %s\n", strerror(errno)));

		save_state_file(&state->rawdev);
	}
	return ret;
}

/** Main processing loop in which all write commands are sent to driver. This function, as many other, was copied from camogm and
 * contains some unrelated stuff */
int listener_loop(camogm_state *state, struct dd_params *dd_params)
{
	FILE *cmd_file;
	int rslt, cmd;
	int process = 1;
	int curr_port = 0;
	int ret = 0;
	unsigned long mb_written;
	unsigned int wr_speed = 0;
	time_t now;
	double seconds;

	dd_params->start_time = time(NULL);
	// enter main processing loop
	while (process && keep_running) {
		curr_port = select_port(state);
		state->port_num = curr_port;
		// look at command queue first
		cmd = parse_cmd(state, cmd_file);
		if (cmd) {
			if (cmd < 0) D0(fprintf(debug_file, "Unrecognized command\n"));
		} else if (state->prog_state == STATE_RUNNING) { // no commands in queue, started
			if (dd_params->block_count != 0) {
				switch ((rslt = sendImageFrame(state))) {
				case 0:
					// file sent OK
					dd_params->block_count--;
					break;
				case EAGAIN:
					break;
				case EIO:
					break;
				case EINVAL:
					break;
				default:
					D0(fprintf(debug_file, "%s:line %d - should not get here (rslt=%d)\n", __FILE__, __LINE__, rslt));
					clean_up(state);
					exit(-1);
				} // switch sendImageFrame()
				if (rslt != 0 && rslt != EAGAIN)
					fprintf(debug_file, "\ndriver returned %d\n", rslt);
				mb_written = ((uint64_t)dd_params->block_size * ((uint64_t)dd_params->block_count_init - (uint64_t)dd_params->block_count)) / (uint64_t)1048576;
				now = time(NULL);
				seconds = difftime(now, dd_params->start_time);
				if (seconds > 0)
					wr_speed = mb_written / (unsigned long)seconds;
				D0(fprintf(debug_file, "\r%lu MiB written, number of counts left: %04lu, average speed: %u MB/s",
						mb_written, dd_params->block_count, wr_speed));
				read_stat(mb_written, state->path);
			} else {
				process = 0;
			}
		} else {
			usleep(COMMAND_LOOP_DELAY);     // make it longer but interruptible by signals?
		}
	} // while (process)
	D0(fprintf(debug_file, "\n"));

	return ret;
}

/** Write counter to cirbuf memory area */
void prep_data(camogm_state *state, struct dd_params *dd_params)
{
	state->cirbuf_rp[port] = 0;
	state->jpeg_len = dd_params->block_size;
	for (int i = 0, j = 0; i < dd_params->block_size; i++) {
		ccam_dma_buf[port][i] = j;
		j++;
	}
	__clear_cache((char *)ccam_dma_buf[port], (char *)ccam_dma_buf[port] + dd_params->block_size);
}

/** Start disk reading test. The range of LBAs to be tested is specified in 'range' */
static void start_test(camogm_state *state, const unsigned long max_cntr, const struct range *range)
{
	int fd;
	bool report_lba = true;
	off64_t pos, current_lba, end_pos;
	unsigned int cntr, error_lba_cntr;
	unsigned int data[BLOCK_SIZE_DW] = {0};

	fd = open(state->rawdev.rawdev_path, O_RDONLY);
	if (fd < 0) {
		D0(fprintf(debug_file, "Error opening file %s: %s\n", state->rawdev.rawdev_path, strerror(errno)));
		return;
	}

	pos = 0;
	cntr = 0;
	error_lba_cntr = 0;
	end_pos = (range->to - range->from) * BLOCK_SIZE;
	D6(fprintf(debug_file, "testing disk from LBA %llu to LBA %llu\n", range->from, range->to));
	while (pos < end_pos) {
		current_lba = pos / BLOCK_SIZE + range->from;
		D2(fprintf(debug_file, "\rcurrent LBA: %llu", current_lba));
		lseek64(fd, pos, SEEK_SET);
		read(fd, data, BLOCK_SIZE);
		for (int i = 0; i < BLOCK_SIZE_DW; i++) {
			if (data[i] != cntr &&
					report_lba) {
				D0(fprintf(debug_file, "\ncounter discontinuity detected, LBA: %llu, data: 0x%x, counter: 0x%x\n", current_lba, data[i], cntr));
				report_lba = false;
				error_lba_cntr++;
			}
			cntr++;
			if (cntr >= max_cntr)
				cntr = 0;
		}
		if (report_lba && error_lba_cntr) {
			// current sector has no errors so reset erroneous sectors counter
			error_lba_cntr = 0;
		}
		if (error_lba_cntr >= MAX_ERR_LBA) {
			// several consequent sectors with errors were detected, this can be a result of new record so the counter should be
			// resynchronized
			cntr = data[BLOCK_SIZE_DW - 1] + 1;
			error_lba_cntr = 0;
			D0(fprintf(debug_file, "\nresynchronizing counter at LBA: %llu, new value: 0x%x\n", current_lba, cntr));
		}
		report_lba = true;
		pos += BLOCK_SIZE;
	}
	D0(fprintf(debug_file, "\n"));
	close(fd);
}

int main(int argc, char *argv[])
{
	const char usage[] =   "This program is intended for disk write tests\n" \
			     "Usage:\n\n" \
			     "%s -d <path_to_disk> [-s state_file_name -b block_size -c count -t -f from_lba -e to_lba -w file_name]\n\n" \
			     "i.e. write one sector:\n\n" \
			     "%s -d /dev/sda2 -b 512 -c 1\n\n" \
				 "-d specifies partition or disk name which is used for testing;\n" \
				 "-b sets block size in bytes. The program writes data to disk in blocks of this size;\n" \
				 "-c sets the number of repetitions. Total amount of data recorded to disk is equal to (count * block_size);\n" \
				 "-f and -e parameters define starting and finishing LBAs of the disk during test mode. These parameters are " \
				 "ineffective during recording.\n"
				 "-t parameter sets test mode in which the program reads data from disk and verifies that " \
				 "the counter values are consistent. The LBAs with counter discontinuities are reported. Test starts " \
				 "from the beginning of the disk and continues until the end of disk is reached. '-b' parameter is " \
				 "mandatory in this mode and its value must correspond to that used during recording.\n" \
				 "-w parameter sets the file name were some statistics is saved during recording test.\n" \
				 "-h prints this help message\n";
	int ret = EXIT_SUCCESS;
	int opt;
	bool test_mode = false;
	uint16_t port_num = 0;
	size_t str_len;
	camogm_state sstate;
	char pipe_name_str[ELPHEL_PATH_MAX] = {0};
	char state_name_str[ELPHEL_PATH_MAX] = {0};
	char disk_path[ELPHEL_PATH_MAX] = {0};
	char stat_file[ELPHEL_PATH_MAX] = {0};
	struct dd_params dd_params = {0};
	struct range range;
	struct range disk_range;

	if ((argc < 2) || (argv[1][1] == '-')) {
		printf(usage, argv[0], argv[0]);
		return EXIT_SUCCESS;
	}
	while ((opt = getopt(argc, argv, "b:c:d:n:p:s:htf:e:t:w:")) != -1) {
		switch (opt) {
		case 'd':
			// set path to disk under test
			strncpy(disk_path, (const char *)optarg, ELPHEL_PATH_MAX - 1);
			break;
		case 'n':
			strncpy(pipe_name_str, (const char *)optarg, ELPHEL_PATH_MAX - 1);
			break;
		case 'p':
			port_num = (uint16_t)atoi((const char *)optarg);
			break;
		case 'h':
			// print help message
			printf(usage, argv[0], argv[0]);
			return EXIT_SUCCESS;
		case 's':
			// the name of state file
			strncpy(state_name_str, (const char *)optarg, ELPHEL_PATH_MAX - 1);
			break;
		case 'b':
			// set test block size
			dd_params.block_size = strtoul((const char *)optarg, (char **)NULL, 10);
			if (errno != 0) {
				printf("error parsing block size value: %s\n", strerror(errno));
				return EXIT_FAILURE;
			}
			printf("block size is set to %lu\n", dd_params.block_size);
			break;
		case 'c':
			// set the number of test blocks to write
			dd_params.block_count = strtoul((const char *)optarg, (char **)NULL, 10);
			dd_params.block_count_init = dd_params.block_count;
			if (errno != 0) {
				printf("error parsing block size value: %s\n", strerror(errno));
				return EXIT_FAILURE;
			}
			printf("block count is set to %lu\n", dd_params.block_count);
			break;
		case 't':
			// switch to test mode
			test_mode = true;
			break;
		case 'f':
			// set initial LBA
			range.from = strtoull((const char *)optarg, (char **)NULL, 10);
			break;
		case 'e':
			// set last LBA
			range.to = strtoull((const char *)optarg, (char **)NULL, 10);
			break;
		case 'w':
			// set path file were recording statistics is saved
			strncpy(stat_file, (const char *)optarg, ELPHEL_PATH_MAX - 1);
		}
	}

	signal(SIGINT, int_handler);

	camogm_init(&sstate, pipe_name_str, port_num);
	if (!test_mode) {
		ret = open_files(&sstate);
		if (ret < 0)
			return ret;
		str_len = strlen(state_name_str);
		if (str_len > 0) {
			strncpy(sstate.rawdev.state_path, state_name_str, str_len + 1);
		}
		camogm_set_prefix(&sstate, disk_path);
		str_len = strlen(stat_file);
		if (str_len > 0)
			strncpy(sstate.path, stat_file, ELPHEL_PATH_MAX - 1);
		if (sstate.rawdev_op == 1) {
			prep_data(&sstate, &dd_params);

			ret = start_recording(&sstate);
			if (ret == 0) {
				sstate.prog_state = STATE_RUNNING;
				ret = listener_loop(&sstate, &dd_params);
				end_recording(&sstate);
			}
		} else {
			D0(fprintf(debug_file, "Unable to set %s as a disk path\n", disk_path));
			clean_up(&sstate);
			ret = EXIT_FAILURE;
		}
		clean_up(&sstate);
	} else {
		if (dd_params.block_size != 0) {
			camogm_set_prefix(&sstate, disk_path);
			if (sstate.rawdev_op == 1) {
				get_disk_range(sstate.rawdev.rawdev_path, &disk_range);
				if (range.from == 0)
					range.from = disk_range.from;
				if (range.to == 0)
					range.to = disk_range.to;
				// block_size is in bytes and we need if here in double words
				start_test(&sstate, dd_params.block_size / 4, &range);
			}
		} else {
			D0(fprintf(debug_file, "block size is not specified, restart the program with '-b' option\n"));
		}
	}

	return ret;
}
