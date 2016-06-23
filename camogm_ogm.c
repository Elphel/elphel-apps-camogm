/*!***************************************************************************
   *! FILE NAME  : camogm_ogm.c
   *! DESCRIPTION: Provides writing to ogm files for camogm
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
   *!  $Log: camogm_ogm.c,v $
   *!  Revision 1.2  2009/02/25 17:50:02  spectr_rain
   *!  removed deprecated dependency
   *!
   *!  Revision 1.1.1.1  2008/11/27 20:04:01  elphel
   *!
   *!
   *!  Revision 1.2  2007/11/19 03:23:21  elphel
   *!  7.1.5.5 Added support for *.mov files in camogm.
   *!
   *!  Revision 1.1  2007/11/16 08:49:58  elphel
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
#include <sys/mman.h>   /* mmap */
#include <sys/ioctl.h>

//#include <c313a.h>
#include <asm/byteorder.h>

//#include <ogg/ogg.h>    // has to be before ogmstreams.h
//#include "ogmstreams.h" // move it to <>?

#include "camogm_ogm.h"

//! may add something - called first time format is changed to this one (only once) recording is stopped
int camogm_init_ogm(void)
{
	return 0;
}
void camogm_free_ogm(void)
{
}

int camogm_start_ogm(camogm_state *state)
{
	char vendor[] = "ElphelOgm v 0.1";
	int pos;
	stream_header sh;
	char hdbuf[sizeof(sh) + 1];
	ogg_packet ogg_header;

	sprintf(state->path, "%s%010ld_%06ld.ogm", state->path_prefix, state->frame_params[state->port_num].timestamp_sec, state->frame_params[state->port_num].timestamp_usec);
	if (!((state->vf = fopen(state->path, "w+"))) ) {
		D0(fprintf(debug_file, "Error opening %s for writing\n", state->path));
		return -CAMOGM_FRAME_FILE_ERR;
	}
	ogg_stream_init(&(state->os), state->serialno);
	state->packetno = 0;
	memset(&sh, 0, sizeof(stream_header));
	memcpy(sh.streamtype, "video", 5);
	memcpy(sh.subtype, "MJPG", 4);
	put_uint32(&sh.size, sizeof(sh));
//   put_uint64(&sh.time_unit, (ogg_int64_t) 10*state->frame_period);
	put_uint64(&sh.time_unit, state->time_unit);
//   put_uint64(&sh.samples_per_unit, 1);
	put_uint64(&sh.samples_per_unit, (ogg_int64_t)state->timescale);
	put_uint32(&sh.default_len, 1);
	put_uint32(&sh.buffersize, state->width * state->height);
//   put_uint16(&sh.bits_per_sample, 24); //?
	put_uint16(&sh.bits_per_sample, 0); //?
	put_uint32(&sh.sh.video.width, state->width);
	put_uint32(&sh.sh.video.height, state->height);
	memcpy(&hdbuf[1], &sh, sizeof(sh));
	hdbuf[0] = 1;
//! put it into Ogg stream
	ogg_header.packet = hdbuf;
	ogg_header.bytes = sizeof(sh) + 1;
	ogg_header.b_o_s = 1;
	ogg_header.e_o_s = 0;
	ogg_header.packetno = state->packetno++;;
	ogg_header.granulepos = 0;
	ogg_stream_packetin(&(state->os), &ogg_header);

//   while(ogg_stream_pageout(&(state->os), &(state->og))) {
	while (ogg_stream_flush(&(state->os), &(state->og))) {
		int i, j;
		if ((((i = fwrite(state->og.header, 1, state->og.header_len, state->vf))) != state->og.header_len) ||
		    (state->og.body_len && (((i = fwrite(state->og.body, 1, state->og.body_len, state->vf))) != state->og.body_len))) {
			j = errno;
			D2(fprintf(debug_file, "\n%d %ld %ld\n", i, state->og.header_len, state->og.body_len));
			return -CAMOGM_FRAME_FILE_ERR;
		}
	}

//!create comment
//!use fixed minimal one - hdbuf will be enough for that
	memset(hdbuf, 0, sizeof(hdbuf));
	hdbuf[0] = PACKET_TYPE_COMMENT;
	memcpy(&hdbuf[1], "vorbis", 6);
	pos = 7;
	put_uint32(&hdbuf[pos], strlen(vendor));
	pos += 4;
	strcpy(&hdbuf[pos], vendor);
	pos += strlen(vendor);
	put_uint32(&hdbuf[pos], 0);
	pos += 4;
	hdbuf[pos++] = 1;
//! put it into Ogg stream
	ogg_header.packet = (unsigned char *)hdbuf;
	ogg_header.bytes = pos;
	ogg_header.b_o_s = 0;
	ogg_header.e_o_s = 0;
	ogg_header.packetno = state->packetno++;;
	ogg_header.granulepos = 0;
	ogg_stream_packetin(&(state->os), &ogg_header);
/*
   while(ogg_stream_pageout(&(state->os), &(state->og))) {
      int i, j;
      if ((((i = fwrite(state->og.header, 1, state->og.header_len, state->vf))) != state->og.header_len) ||
           (state->og.body_len && (((i = fwrite(state->og.body, 1, state->og.body_len, state->vf))) != state->og.body_len))) {
          j=errno;
          D(fprintf(debug_file,"\n%d %ld %ld\n",i,state->og.header_len,state->og.body_len));
          return -CAMOGM_FRAME_FILE_ERR;
      }
   }
 */
//! calculate initial absolute granulepos (from 1970), then increment with each frame. Later try calculating granulepos of each frame
//! from the absolute time (actual timestamp)
	state->granulepos = (ogg_int64_t)( (((double)state->frame_params[state->port_num].timestamp_usec) +
					    (((double)1000000) * ((double)state->frame_params[state->port_num].timestamp_sec))) *
					   ((double)10) /
					   ((double)state->time_unit) *
					   ((double)state->timescale));
//   state->frame_period=(state->this_frame_params.timestamp_usec - state->frame_params.timestamp_usec)+
//               1000000*(state->this_frame_params.timestamp_sec  - state->frame_params.timestamp_sec);
//  put_uint64(&sh.time_unit, (ogg_int64_t)((double)10000000.0 / (double)fps));

//!Temporarily setting granulepos to 0 (suspect they do not process correctly 64 bits)
	state->granulepos = 0;

//! Here - Ogg stream started, both header and comment  packets are sent out, next should be just data packets
	return 0;
}

int camogm_frame_ogm(camogm_state *state)
{
	int indx;
	elph_ogg_packet elp_packet;

	elp_packet.bytes = 0;
	for (indx = 0; indx < state->chunk_index; indx++) elp_packet.bytes += state->packetchunks[indx].bytes;
	elp_packet.packet = state->packetchunks;
//D(fprintf (debug_file,"elp_packet.bytes=0x%lx: elp_packet.packet=%p\n",elp_packet.bytes, elp_packet.packet));
/*D(fprintf (debug_file,"0:0x%lx: %p\n" \
                             "1:0x%lx: %p\n" \
                             "2:0x%lx: %p\n" \
                             "3:0x%lx: %p\n" \
                             "4:0x%lx: %p\n" \
                             "5:0x%lx: %p\n" \
                             "6:0x%lx: %p\n", \
                              elp_packet.packet[0].bytes,  elp_packet.packet[0].chunk,
                              elp_packet.packet[1].bytes,  elp_packet.packet[1].chunk,
                              elp_packet.packet[2].bytes,  elp_packet.packet[2].chunk,
                              elp_packet.packet[3].bytes,  elp_packet.packet[3].chunk,
                              elp_packet.packet[4].bytes,  elp_packet.packet[4].chunk,
                              elp_packet.packet[5].bytes,  elp_packet.packet[5].chunk,
                              elp_packet.packet[6].bytes,  elp_packet.packet[6].chunk));
 */
	elp_packet.b_o_s = 0;
	elp_packet.e_o_s = 0;
	elp_packet.packetno = state->packetno++;;
//   tts[0]=state->frame_params.timestamp_usec;
//   tts[1]=state->frame_params.timestamp_sec;
	elp_packet.granulepos = state->granulepos;
//!TODO: If that works, calculate granulepos from timestamp for each frame
	state->granulepos += (ogg_int64_t)state->timescale;

//D3(fprintf (debug_file,"_121_"));
	ogg_stream_packetin_elph(&(state->os), &elp_packet);
//D3(fprintf (debug_file,"_13_"));
	while (ogg_stream_pageout(&(state->os), &(state->og))) {
		int i, j;
		if ((((i = fwrite(state->og.header, 1, state->og.header_len, state->vf))) != state->og.header_len) ||
		    (state->og.body_len && (((i = fwrite(state->og.body, 1, state->og.body_len, state->vf))) != state->og.body_len))) {
			j = errno;
			D0(fprintf(debug_file, "\n%d %ld %ld\n", i, state->og.header_len, state->og.body_len));
			return -CAMOGM_FRAME_FILE_ERR;
		}
	}
	return 0;
}

//!Zero packets are OK, use them to end file with "last" turned on

int camogm_end_ogm(camogm_state *state)
{
//! put zero-packet it into stream
	ogg_packet ogg_header;

	ogg_header.packet = NULL;
	ogg_header.bytes = 0;
	ogg_header.b_o_s = 0;
	ogg_header.e_o_s = 1;
	ogg_header.packetno = state->packetno++;
	ogg_header.granulepos = ++(state->granulepos);
	ogg_stream_packetin(&(state->os), &ogg_header); //!+++++++++++++++++++++++++++++++++++++++++++++++++++++
	while (ogg_stream_flush(&(state->os), &(state->og))) {
		int i, j;
		if ((((i = fwrite(state->og.header, 1, state->og.header_len, state->vf))) != state->og.header_len) ||
		    (state->og.body_len && (((i = fwrite(state->og.body, 1, state->og.body_len, state->vf))) != state->og.body_len))) {
			j = errno;
			D0(fprintf(debug_file, "\n%d %ld %ld\n", i, state->og.header_len, state->og.body_len));
			return -CAMOGM_FRAME_FILE_ERR;
		}
	}
	return 0;
}
