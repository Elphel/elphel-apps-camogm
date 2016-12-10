/** @brief This define is needed to use lseek64 and should be set before includes */
#define _LARGEFILE64_SOURCE

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
	unsigned long int block_size;
	unsigned long int block_count;
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
	ccam_dma_buf[port] = (unsigned long*)mmap(0, state->circ_buff_size[port], PROT_READ | PROT_WRITE, MAP_SHARED, state->fd_circ[port], 0);
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
		if (write(state->rawdev.sysfs_fd, &fdata, sizeof(struct frame_data)) < 0) {
			D0(fprintf(debug_file, "Can not pass IO vector to driver: %s\n", strerror(errno)));
			return -CAMOGM_FRAME_FILE_ERR;
		}
	}

	return 0;
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
unsigned int select_port(camogm_state *state)
{
	// do not process commands
	return port;
}

int parse_cmd(camogm_state *state, FILE* npipe)
{
	return 0;
}

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

int listener_loop(camogm_state *state, struct dd_params *dd_params)
{
	FILE *cmd_file;
	int rslt, cmd;
	int process = 1;
	int curr_port = 0;
	int retry_cntr = 0;
	int ret = 0;

	// enter main processing loop
	while (process) {
		curr_port = select_port(state);
		state->port_num = curr_port;
		// look at command queue first
		cmd = parse_cmd(state, cmd_file);
		if (cmd) {
			if (cmd < 0) D0(fprintf(debug_file, "Unrecognized command\n"));
		} else if (state->prog_state == STATE_RUNNING) { // no commands in queue, started
			if (dd_params->block_count != 0) {
				switch ((rslt = -sendImageFrame(state))) {
				case 0:
					// file sent OK
					dd_params->block_count--;
					break;
				case CAMOGM_FRAME_FILE_ERR:
					// we need to wait as the driver queue is full
					usleep(COMMAND_LOOP_DELAY);
					break;
				default:
					D0(fprintf(debug_file, "%s:line %d - should not get here (rslt=%d)\n", __FILE__, __LINE__, rslt));
					clean_up(state);
					exit(-1);
				} // switch sendImageFrame()
				D0(fprintf(debug_file, "Number of counts left: %lu\n", dd_params->block_count));
			} else {
				process = 0;
			}
		} else {
			usleep(COMMAND_LOOP_DELAY);     // make it longer but interruptible by signals?
		}
	} // while (process)

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
}

int main(int argc, char *argv[])
{
	const char usage[] =   "This program is intended for disk write tests\n" \
			     "Usage:\n\n" \
			     "%s -d <path_to_disk> [-s state_file_name -b block_size -c count]\n\n" \
			     "i.e. write one sector:\n\n" \
			     "%s -d /dev/sda2 -b 512 -c 1\n\n";
	int ret;
	int opt;
	uint16_t port_num = 0;
	size_t str_len;
	camogm_state sstate;
	char pipe_name_str[ELPHEL_PATH_MAX] = {0};
	char state_name_str[ELPHEL_PATH_MAX] = {0};
	char disk_path[ELPHEL_PATH_MAX] = {0};
	struct dd_params dd_params = {0};

	if ((argc < 2) || (argv[1][1] == '-')) {
		printf(usage, argv[0], argv[0]);
		return EXIT_SUCCESS;
	}
	while ((opt = getopt(argc, argv, "b:c:d:n:p:s:h")) != -1) {
		switch (opt) {
		case 'd':
			strncpy(disk_path, (const char *)optarg, ELPHEL_PATH_MAX - 1);
			break;
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
		case 'b':
			dd_params.block_size = strtoul((const char *)optarg, (char **)NULL, 10);
			if (errno != 0) {
				printf("error parsing block size value: %s\n", strerror(errno));
				return EXIT_FAILURE;
			}
			printf("block size is set to %lu\n", dd_params.block_size);
			break;
		case 'c':
			dd_params.block_count = strtoul((const char *)optarg, (char **)NULL, 10);
			if (errno != 0) {
				printf("error parsing block size value: %s\n", strerror(errno));
				return EXIT_FAILURE;
			}
			printf("block count is set to %lu\n", dd_params.block_count);
			break;
		}
	}

	camogm_init(&sstate, pipe_name_str, port_num);
	ret = open_files(&sstate);
	if (ret < 0)
		return ret;
	str_len = strlen(state_name_str);
	if (str_len > 0) {
		strncpy(sstate.rawdev.state_path, state_name_str, str_len + 1);
	}
	camogm_set_prefix(&sstate, disk_path);
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

	return ret;
}
