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
//!Not all are needed, just copied from the camogm.c
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
//#include <ctype.h>
//#include <getopt.h>
#include <time.h>
#include <string.h>

#include <netinet/in.h> /*little <-> big endian ?*/
#include <sys/mman.h>		/* mmap */
#include <sys/ioctl.h>

#include <asm/elphel/c313a.h>
#include <asm/byteorder.h>


#include <ogg/ogg.h> // has to be before ogmstreams.h
#include "ogmstreams.h" // move it to <>?

#include "camogm_jpeg.h"
#include "camogm.h"
//! may add something - called first time format is changed to this one (only once) recording is stopped
int camogm_init_jpeg(void) {
  return 0;
}
void camogm_free_jpeg(void) {
}

int camogm_start_jpeg(void) {
//!TODO: make directory if it does not exist (find the last "/" in the state->path
  char * slash;
  int    rslt;
  strcpy (state->path,state->path_prefix); //!make state->path a directory name (will be replaced when the frames will be written)
  slash=strrchr(state->path,'/');
D2(fprintf (debug_file, "camogm_start_jpeg\n"));
  if (slash) {
    D3(fprintf (debug_file, "Full path %s\n", state->path));
    slash[0]='\0'; //! truncate path to the directory name
    D3(fprintf (debug_file, "directory path %s\n", state->path));
    rslt=mkdir(state->path, 0777);
    D3(fprintf (debug_file, "mkdir (%s, 0777) returned %d, errno=%d\n", state->path, rslt, errno));
    if ((rslt<0) && (errno != EEXIST)) { // already exists is OK
       D0(fprintf (debug_file, "Error creating directory %s, errno=%d\n", state->path, errno));
       return -CAMOGM_FRAME_FILE_ERR;
    }
  }
  return 0;
}
int camogm_frame_jpeg(void){
   int i,j;
//   int fd;
   ssize_t iovlen,l;
   struct iovec chunks_iovec[7];
   l=0;
   for (i=0; i< (state->chunk_index)-1; i++) {
      chunks_iovec[i].iov_base=state->packetchunks[i+1].chunk;
      chunks_iovec[i].iov_len= state->packetchunks[i+1].bytes;
      l+=chunks_iovec[i].iov_len;
   }

   sprintf(state->path,"%s%010ld_%06ld.jpeg",state->path_prefix,state->this_frame_params.timestamp_sec,state->this_frame_params.timestamp_usec);
// if ((devfd = open("/dev/fpgaio", O_RDWR))<0)  {printf("error opening /dev/fpgaio\r\n"); return -1;}
//_1__12_Error opening /tmp/z/video1195147018_273452.jpeg for writing

   if (((state->ivf=open(state->path,O_RDWR | O_CREAT, 0777)))<0){
     D0(fprintf (debug_file, "Error opening %s for writing, returned %d, errno=%d\n", state->path,state->ivf,errno));
     return -CAMOGM_FRAME_FILE_ERR;
   }

   iovlen=writev(state->ivf,chunks_iovec, (state->chunk_index)-1);
   if (iovlen < l) {
          j=errno;
          D0(fprintf(debug_file,"writev error %d (returned %d, expected %d)\n",j,iovlen,l));
          close (state->ivf);
          return -CAMOGM_FRAME_FILE_ERR;
   }
   close (state->ivf);
   return 0;
}

int camogm_end_jpeg(void){
  return 0;
}
