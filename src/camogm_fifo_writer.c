/** @brief This define is needed to use lseek64 and should be set before includes */
#define _LARGEFILE64_SOURCE
/** Needed for O_DIRECT */
#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <stdbool.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include <ctype.h>
#include <sys/uio.h>

int main(int argc, char *argv[]){

	FILE *cmd_file;
	const char pipe_name[] = "/tmp/fifo_test";
	const char message[]   = "test";
	int ret;

	printf("This is writer\n");

	cmd_file = fopen(pipe_name, "w");

	while(true){
		ret = fwrite(message,sizeof(char),sizeof(message),cmd_file);
		printf("Wrote %d bytes, fwrite returned %d\n",sizeof(message),ret);
	}

	return 0;
}
