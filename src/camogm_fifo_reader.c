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
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include <ctype.h>
#include <sys/uio.h>

#include <poll.h>

// set to 1 second
#define CAMOGM_TIMEOUT 1000

int main(int argc, char *argv[]){

	int f_ok;
	int ret;
	int fer, feo;
	long int ftl;
	int i=0;
	const char pipe_name[] = "/tmp/fifo_test";
	int flags;

	char cmdbuf[1024];
	int fl;

	int somecounter = 0;
	int writecounter = 0;

	FILE *cmd_file;

	struct pollfd pfd;
	pfd.events = POLLIN;

	printf("This is reader. It creates FIFO: %s\n",pipe_name);

	// always delete pipe if it exists
	f_ok = access(pipe_name, F_OK);
	ret = unlink(pipe_name);

	if (ret && f_ok == 0) {
		printf("Some error\n");
	}

	ret = mkfifo(pipe_name, 0666); //EEXIST
	if (ret) {
		if (errno==EEXIST){
			printf("Pipe exists\n");
		}
	}

	cmd_file = open(pipe_name, O_RDWR|O_NONBLOCK);
	pfd.fd = cmd_file;
	fcntl(cmd_file, F_SETFL, 0);  /* disable O_NONBLOCK */

	printf("Pipe is now open for reading\n");

	while(true){

		ret = poll(&pfd, 1, CAMOGM_TIMEOUT);  /* poll to avoid reading EOF */
		somecounter +=1;

		if (ret==0) {
			printf("TIMEOUT %d\n",somecounter);
		}

		if (pfd.revents & POLLIN){

			printf("PostPoll %d %d, revents = %d,  errno = %d\n",somecounter,ret,pfd.revents,errno);

			fl = read(cmd_file, cmdbuf, sizeof(cmdbuf));
			//clearerr(cmd_file);

			if (fl>0) {
				writecounter +=1;
			}

			if (fl<0) {
				printf("Error?\n");
				break;
			}


			//fl = read(cmd_file,cmdbuf,sizeof(cmdbuf));
			if ((fl>10)||(somecounter>10000000)) {
				break;
			}
		}
	}

	printf("EXIT! errno=%d writes=%d wdc=%d\n",errno,writecounter,somecounter);

	return 0;
}
