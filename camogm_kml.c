/*!***************************************************************************
   *! FILE NAME  : camogm_kml.c
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
   *!  $Log: camogm_kml.c,v $
   *!  Revision 1.3  2011/01/03 22:00:29  elphel
   *!  fixed tilt generation
   *!
   *!  Revision 1.2  2009/02/25 17:50:02  spectr_rain
   *!  removed deprecated dependency
   *!
   *!  Revision 1.1.1.1  2008/11/27 20:04:01  elphel
   *!
   *!
   *!  Revision 1.2  2008/04/13 21:05:20  elphel
   *!  Fixing KML generation
   *!
   *!  Revision 1.1  2008/04/11 23:06:52  elphel
   *!  files to handle KML generation
   *!
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

#include <c313a.h>
#include <asm/byteorder.h>


#include <ogg/ogg.h>    // has to be before ogmstreams.h
#include "ogmstreams.h" // move it to <>?

#include "camogm_kml.h"

const char ExifDirFileName[] = "/dev/exif_metadir";

//! may add something - called first time format is changed to this one (only once) recording is stopped
int camogm_init_kml(void)
{
	return 0;
}
void camogm_free_kml(void)
{
}
/*
   int camogm_start_mov(void) {

   //! allocate memory for the frame index table
   if (!((state->frame_lengths=malloc(4*state->max_frames)))) return -CAMOGM_FRAME_MALLOC ;
   //! open file for writing
   sprintf(state->path,"%s%010ld_%06ld.mov",state->path_prefix,state->frame_params.timestamp_sec,state->frame_params.timestamp_usec);
   if (((state->ivf=open(state->path,O_RDWR | O_CREAT, 0777)))<0){
     D0(fprintf (debug_file, "Error opening %s for writing, returned %d, errno=%d\n", state->path,state->ivf,errno));
     return -CAMOGM_FRAME_FILE_ERR;
   }
   //!skip header (plus extra)
   //! Quicktime (and else?) - frame data start (0xff 0xd8...)
   state->frame_data_start=QUICKTIME_MIN_HEADER+16+ 4*(state->max_frames)+ ( 4*(state->max_frames))/(state->frames_per_chunk); // 8 bytes for "skip" tag
   lseek(state->ivf, state->frame_data_start, SEEK_SET);
   return 0;
   }

 */
int camogm_start_kml(camogm_state *state)
{
//  struct exif_dir_table_t kml_exif[ExifKmlNumber] ;  //! store locations of the fields needed for KML generations in the Exif block
///exif_metadir
///    state->kml_exif[i]
/// Re-read directory table and rebuild state->kml_exif when starting each file
	struct exif_dir_table_t dir_table_entry;
	int fd_ExifDir;
	int indx;

	for (indx = 0; indx < ExifKmlNumber; indx++) state->kml_exif[indx].ltag = 0;
//! open Exif header directory file
	fd_ExifDir = open(ExifDirFileName, O_RDONLY);
	if (fd_ExifDir < 0) { // check control OK
		D0(fprintf(debug_file, "Error opening %s\n", ExifDirFileName));
		return -CAMOGM_FRAME_FILE_ERR;
	}
	while (read(fd_ExifDir, &dir_table_entry, sizeof(dir_table_entry)) > 0) {
		switch (dir_table_entry.ltag) {
		case Exif_Photo_DateTimeOriginal:      indx = Exif_Photo_DateTimeOriginal_Index; break;
		case Exif_GPSInfo_GPSLatitudeRef:      indx = Exif_GPSInfo_GPSLatitudeRef_Index; break;
		case Exif_GPSInfo_GPSLatitude:         indx = Exif_GPSInfo_GPSLatitude_Index; break;
		case Exif_GPSInfo_GPSLongitudeRef:     indx = Exif_GPSInfo_GPSLongitudeRef_Index; break;
		case Exif_GPSInfo_GPSLongitude:        indx = Exif_GPSInfo_GPSLongitude_Index; break;
		case Exif_GPSInfo_GPSAltitudeRef:      indx = Exif_GPSInfo_GPSAltitudeRef_Index; break;
		case Exif_GPSInfo_GPSAltitude:         indx = Exif_GPSInfo_GPSAltitude_Index; break;
		case Exif_GPSInfo_GPSTimeStamp:        indx = Exif_GPSInfo_GPSTimeStamp_Index; break;
		case Exif_GPSInfo_GPSDateStamp:        indx = Exif_GPSInfo_GPSDateStamp_Index; break;
		case Exif_GPSInfo_CompassDirectionRef: indx = Exif_GPSInfo_CompassDirectionRef_Index; break;
		case Exif_GPSInfo_CompassDirection:    indx = Exif_GPSInfo_CompassDirection_Index; break;
		case Exif_GPSInfo_CompassPitchRef:     indx = Exif_GPSInfo_CompassPitchRef_Index; break;
		case Exif_GPSInfo_CompassPitch:        indx = Exif_GPSInfo_CompassPitch_Index; break;
		case Exif_GPSInfo_CompassRollRef:      indx = Exif_GPSInfo_CompassRollRef_Index; break;
		case Exif_GPSInfo_CompassRoll:         indx = Exif_GPSInfo_CompassRoll_Index; break;
		default: indx = -1;
		}
		if (indx >= 0) {
			memcpy(&(state->kml_exif[indx]), &dir_table_entry, sizeof(dir_table_entry));
			D2(fprintf(debug_file, "indx=%02d, ltag=0x%05x, len=0x%03x, src=0x%03x, dst=0x%03x\n", indx, \
				   (int)dir_table_entry.ltag, \
				   (int)dir_table_entry.len, \
				   (int)dir_table_entry.src, \
				   (int)dir_table_entry.dst));
		}
	}
	close(fd_ExifDir);
	sprintf(state->kml_path, "%s%010ld_%06ld.kml", state->path_prefix, state->this_frame_params.timestamp_sec, state->this_frame_params.timestamp_usec);
	if (!((state->kml_file = fopen(state->kml_path, "w+"))) ) {
		D0(fprintf(debug_file, "Error opening %s for writing\n", state->kml_path));
		return -CAMOGM_FRAME_FILE_ERR;
	}
/// write start of the KML file
	fprintf(state->kml_file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"	\
		"<kml xmlns=\"http://earth.google.com/kml/2.2\">\n");
	fprintf(state->kml_file, "<Document>\n");
	state->kml_used = 1;
	return 0;
}

int camogm_frame_kml(camogm_state *state)
{
	char JPEGFileName[300];
	char * filename;
	int fd_JPEG;
	int i, j;
	ssize_t iovlen, l;
	struct iovec chunks_iovec[7];
	char datestr[11];
	double longitude = 0.0, latitude = 0.0,  altitude = 0.0,  heading = 0.0,  tilt = 0.0,  roll = 0.0, pitch = 0.0;
	int hours = 0, minutes = 0;
	double seconds = 0.0;
	int * ip;
	int port = state->port_num;

	if (state->kml_file) { // probably not needed
		i = state->this_frame_params.timestamp_sec - (state->kml_last_ts + state->kml_period);
		if ((i > 1) || ((i == 0) && ( state->this_frame_params.timestamp_usec > state->kml_last_uts  ))) {
//    if (state->this_frame_params.timestamp_sec > (state->kml_last_ts + state->kml_period)) { // this way it is safe to put kml_period=1000, then kml_period=1
			state->kml_last_ts = state->this_frame_params.timestamp_sec;
			state->kml_last_uts = state->this_frame_params.timestamp_usec;
			if (state->format == CAMOGM_FORMAT_JPEG) {
				strcpy(JPEGFileName, state->path);
			} else {
				sprintf(JPEGFileName, "%s%010ld_%06ld.jpeg", state->path_prefix, state->this_frame_params.timestamp_sec, state->this_frame_params.timestamp_usec);
				if (((fd_JPEG = open(JPEGFileName, O_RDWR | O_CREAT, 0777))) >= 0) {
					l = 0;
					for (i = 0; i < (state->chunk_index) - 1; i++) {
						chunks_iovec[i].iov_base = state->packetchunks[i + 1].chunk;
						chunks_iovec[i].iov_len = state->packetchunks[i + 1].bytes;
						l += chunks_iovec[i].iov_len;
					}
					iovlen = writev(fd_JPEG, chunks_iovec, (state->chunk_index) - 1);
					if (iovlen < l) {
						j = errno;
						D0(fprintf(debug_file, "writev error %d (returned %d, expected %d)\n", j, iovlen, l));
						close(fd_JPEG);
						return -CAMOGM_FRAME_FILE_ERR;
					}
					close(fd_JPEG);
				} else {
					D0(fprintf(debug_file, "Error opening %s for writing, returned %d, errno=%d\n", JPEGFileName, fd_JPEG, errno));
					return -CAMOGM_FRAME_FILE_ERR;
				}
			}

/// now we have JPEGFileName written. find realtive (to KML) location:
			filename = strrchr(JPEGFileName, '/');
			filename[0] = '\0';
			filename++;
///generating KML itself
/// Using GPS time - in the same structure
			if (state->kml_exif[Exif_GPSInfo_GPSDateStamp_Index].ltag == Exif_GPSInfo_GPSDateStamp) { // Exif_GPSInfo_GPSDateStamp is present in template
				memcpy(datestr, &(state->ed[port][state->kml_exif[Exif_GPSInfo_GPSDateStamp_Index].dst]), 10);
				datestr[4] = '-'; datestr[7] = '-'; datestr[10] = '\0';
			}
			if (state->kml_exif[Exif_GPSInfo_GPSTimeStamp_Index].ltag == Exif_GPSInfo_GPSTimeStamp) { // Exif_GPSInfo_GPSTimeStamp is present in template
				ip = (int*)&(state->ed[state->kml_exif[Exif_GPSInfo_GPSTimeStamp_Index].dst]);
				hours =   __cpu_to_be32( ip[0]);
				minutes = __cpu_to_be32( ip[2]);
				seconds = (1.0 * (__cpu_to_be32( ip[4]) + 1)) / __cpu_to_be32( ip[5]); /// GPS likes ".999", let's inc by one - anyway will round that out
				D2(fprintf(debug_file, "(when) 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", ip[0], ip[1], ip[2], ip[3], ip[4], ip[5]));
				D2(fprintf(debug_file, "(when) 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", __cpu_to_be32(ip[0]), \
					   __cpu_to_be32(ip[1]), \
					   __cpu_to_be32(ip[2]), \
					   __cpu_to_be32(ip[3]), \
					   __cpu_to_be32(ip[4]), \
					   __cpu_to_be32(ip[5])));
			}
			D1(fprintf(debug_file, "when=%sT%02d:%02d:%05.2fZ\n", datestr, hours, minutes, seconds));

/// knowing format provided from GPS - degrees and minuts only, no seconds:
			if (state->kml_exif[Exif_GPSInfo_GPSLongitude_Index].ltag == Exif_GPSInfo_GPSLongitude) { // Exif_GPSInfo_GPSLongitude is present in template
				ip = (int*)&(state->ed[state->kml_exif[Exif_GPSInfo_GPSLongitude_Index].dst]);
				longitude = __cpu_to_be32( ip[0]) / (1.0 * __cpu_to_be32( ip[1])) + __cpu_to_be32( ip[2]) / (60.0 * __cpu_to_be32( ip[3]));
				if ((state->kml_exif[Exif_GPSInfo_GPSLongitudeRef_Index].ltag == Exif_GPSInfo_GPSLongitudeRef) &&
				    (state->ed[port][state->kml_exif[Exif_GPSInfo_GPSLongitudeRef_Index].dst] != 'E')) longitude = -longitude;
				D2(fprintf(debug_file, "(longitude) 0x%x 0x%x 0x%x 0x%x '%c'\n", ip[0], ip[1], ip[2], ip[3], state->ed[port][state->kml_exif[Exif_GPSInfo_GPSLongitudeRef_Index].dst]));
			}

			if (state->kml_exif[Exif_GPSInfo_GPSLatitude_Index].ltag == Exif_GPSInfo_GPSLatitude) { // Exif_GPSInfo_GPSLatitude is present in template
				ip = (int*)&(state->ed[port][state->kml_exif[Exif_GPSInfo_GPSLatitude_Index].dst]);
				latitude = __cpu_to_be32( ip[0]) / (1.0 * __cpu_to_be32( ip[1])) + __cpu_to_be32( ip[2]) / (60.0 * __cpu_to_be32( ip[3]));
				if ((state->kml_exif[Exif_GPSInfo_GPSLatitudeRef_Index].ltag == Exif_GPSInfo_GPSLatitudeRef) &&
				    (state->ed[port][state->kml_exif[Exif_GPSInfo_GPSLatitudeRef_Index].dst] != 'N')) latitude = -latitude;
				D2(fprintf(debug_file, "(latitude) 0x%x 0x%x 0x%x 0x%x '%c'\n", ip[0], ip[1], ip[2], ip[3], state->ed[port][state->kml_exif[Exif_GPSInfo_GPSLatitudeRef_Index].dst] ? '-' : '+'));
			}
/// altitude - will be modified/replaced later
			if (state->kml_exif[Exif_GPSInfo_GPSAltitude_Index].ltag == Exif_GPSInfo_GPSAltitude) { // Exif_GPSInfo_GPSAltitude is present in template
				ip = (int*)&(state->ed[port][state->kml_exif[Exif_GPSInfo_GPSAltitude_Index].dst]);
				altitude = (1.0 * __cpu_to_be32( ip[0])) / __cpu_to_be32( ip[1]);
				if ((state->kml_exif[Exif_GPSInfo_GPSAltitudeRef_Index].ltag == Exif_GPSInfo_GPSAltitudeRef) &&
				    (state->ed[port][state->kml_exif[Exif_GPSInfo_GPSAltitudeRef_Index].dst] != '\0')) altitude = -altitude;
				D2(fprintf(debug_file, "(altitude) 0x%x 0x%x '%c'\n", ip[0], ip[1], state->ed[port][state->kml_exif[Exif_GPSInfo_GPSAltitudeRef_Index].dst]));
			}

			D1(fprintf(debug_file, "longitude=%f, latitude=%f, altitude=%f\n", longitude, latitude, altitude));

/// Heading - no processing of "True/Magnetic" Exif_GPSInfo_CompassDirectionRef now (always M)
			if (state->kml_exif[Exif_GPSInfo_CompassDirection_Index].ltag == Exif_GPSInfo_CompassDirection) { // Exif_GPSInfo_CompassDirection is present in template
				ip = (int*)&(state->ed[port][state->kml_exif[Exif_GPSInfo_CompassDirection_Index].dst]);
				heading = (1.0 * __cpu_to_be32( ip[0])) / __cpu_to_be32( ip[1]);
				D2(fprintf(debug_file, "(heading) 0x%x 0x%x\n", ip[0], ip[1]));
			}
///Processing 'hacked' pitch and roll (made of Exif destination latitude/longitude)
			if (state->kml_exif[Exif_GPSInfo_CompassRoll_Index].ltag == Exif_GPSInfo_CompassRoll) { // Exif_GPSInfo_CompassRoll is present in template
				ip = (int*)&(state->ed[state->kml_exif[Exif_GPSInfo_CompassRoll_Index].dst]);
				roll = __cpu_to_be32( ip[0]) / (1.0 * __cpu_to_be32( ip[1])) + __cpu_to_be32( ip[2]) / (60.0 * __cpu_to_be32( ip[3]));
				if ((state->kml_exif[Exif_GPSInfo_CompassRollRef_Index].ltag == Exif_GPSInfo_CompassRollRef) &&
				    (state->ed[port][state->kml_exif[Exif_GPSInfo_CompassRollRef_Index].dst] != EXIF_COMPASS_ROLL_ASCII[0])) roll = -roll;
				D2(fprintf(debug_file, "(roll) 0x%x 0x%x '%c'\n", ip[0], ip[1], state->ed[port][state->kml_exif[Exif_GPSInfo_CompassRollRef_Index].dst]));
			}

			if (state->kml_exif[Exif_GPSInfo_CompassPitch_Index].ltag == Exif_GPSInfo_CompassPitch) { // Exif_GPSInfo_CompassPitch is present in template
				ip = (int*)&(state->ed[port][state->kml_exif[Exif_GPSInfo_CompassPitch_Index].dst]);
				pitch = __cpu_to_be32( ip[0]) / (1.0 * __cpu_to_be32( ip[1])) + __cpu_to_be32( ip[2]) / (60.0 * __cpu_to_be32( ip[3]));
				if ((state->kml_exif[Exif_GPSInfo_CompassPitchRef_Index].ltag == Exif_GPSInfo_CompassPitchRef) &&
				    (state->ed[port][state->kml_exif[Exif_GPSInfo_CompassPitchRef_Index].dst] !=  EXIF_COMPASS_PITCH_ASCII[0])) pitch = -pitch;
				D2(fprintf(debug_file, "(pitch) 0x%x 0x%x '%c'\n", ip[0], ip[1], state->ed[port][state->kml_exif[Exif_GPSInfo_CompassPitchRef_Index].dst]));
			}
/// convert from GPS heading, pitch, roll to KML heading, tilt, roll
			tilt = pitch + 90.0;
			if (tilt < 0.0) tilt = 0;
			else if (tilt > 180.0) tilt = 180.0;
			D2(fprintf(debug_file, "heading=%f, roll=%f, pitch=%f, tilt=%f\n", heading, roll, pitch, tilt));

/// modify altitude
			altitude = (state->kml_height_mode ? altitude : 0.0) + state->kml_height;


/// write to KML
			fprintf(state->kml_file, "<PhotoOverlay>\n" \
				"  <shape>rectangle</shape>\n" \
				"  <TimeStamp>\n" \
				"     <when>%sT%02d:%02d:%05.2fZ</when>\n" \
				"  </TimeStamp>\n" \
				"  <Icon>\n" \
				"    <href>%s</href>\n"	\
				"  </Icon>\n" \
				" <Camera>\n" \
				"   <longitude>%f</longitude>\n" \
				"   <latitude>%f</latitude>\n" \
				"   <altitude>%f</altitude>\n" \
				"   <heading>%f</heading>\n" \
				"   <tilt>%f</tilt>\n" \
				"   <roll>%f</roll>\n" \
				"   <altitudeMode>%s</altitudeMode>\n" \
				"  </Camera>\n"	\
				"  <ViewVolume>\n" \
				"    <leftFov>%f</leftFov>\n" \
				"    <rightFov>%f</rightFov>\n"	\
				"    <bottomFov>%f</bottomFov>\n" \
				"    <topFov>%f</topFov>\n" \
				"    <near>%f</near>\n"	\
				"  </ViewVolume>\n" \
				"</PhotoOverlay>\n", \
				datestr, hours, minutes, seconds, \
				filename, longitude, latitude, altitude, heading, tilt, roll, state->kml_height_mode ? "absolute" : "relativeToGround",	\
				-(state->kml_horHalfFov), state->kml_horHalfFov, -(state->kml_vertHalfFov), state->kml_vertHalfFov, state->kml_near);
		}
	}
	return 0;
}


int camogm_end_kml(camogm_state *state)
{

	if (state->kml_file) {
		fprintf(state->kml_file, "</Document>\n");
		fprintf(state->kml_file, "</kml>\n");
		fclose(state->kml_file);
		state->kml_file = NULL;
	}
	return 0;
}
