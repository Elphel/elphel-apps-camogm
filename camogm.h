#define CAMOGM_FRAME_NOT_READY 1 // frame pointer valid, but not yet acquired
#define CAMOGM_FRAME_INVALID   2 // invalid frame pointer
#define CAMOGM_FRAME_CHANGED   3 // frame parameters have changed
#define CAMOGM_FRAME_NEXTFILE  4 // need to switch to a new file segment
#define CAMOGM_FRAME_BROKEN    5 // frame broken (buffer overrun)
#define CAMOGM_FRAME_FILE_ERR  6 // error with file I/O
#define CAMOGM_FRAME_MALLOC    7 // can not allocate memory
#define CAMOGM_TOO_EARLY       8 // too early to start, waiting for particular timestamp
#define CAMOGM_FRAME_OTHER     9 // other errors

#define CAMOGM_FORMAT_NONE     0 // no video output
#define CAMOGM_FORMAT_OGM      1 // output as Ogg Media file
#define CAMOGM_FORMAT_JPEG     2 // output as individual JPEG files
#define CAMOGM_FORMAT_MOV      3 // output as Apple Quicktime

#define D(x) {if (debug_file && debug_level){x;fflush(debug_file);}}
#define D0(x) {if (debug_file){x;fflush(debug_file);}}
#define D1(x) {if (debug_file && (debug_level > 0)){x;fflush(debug_file);}}
#define D2(x) {if (debug_file && (debug_level > 1)){x;fflush(debug_file);}}
#define D3(x) {if (debug_file && (debug_level > 2)){x;fflush(debug_file);}}
#define D4(x) {if (debug_file && (debug_level > 3)){x;fflush(debug_file);}}
#define D5(x) {if (debug_file && (debug_level > 4)){x;fflush(debug_file);}}
#define D6(x) {if (debug_file && (debug_level > 5)){x;fflush(debug_file);}}

//#define DD(x)
#define DD(x)  {if (debug_file){fprintf(debug_file,"%s:%d:",__FILE__,__LINE__);x;fflush(debug_file);}}
// HEADER_SIZE is defined to be larger than actual header (later - with EXIF)  to use compile-time buffer
#define JPEG_HEADER_MAXSIZE    0x300 // will not change

//#include "camogm_exif.h"
#include <asm/elphel/exifa.h>


/*
#define  Exif_Photo_DateTimeOriginal      0x19003
#define  Exif_GPSInfo_GPSLatitudeRef      0x20001
#define  Exif_GPSInfo_GPSLatitude         0x20002
#define  Exif_GPSInfo_GPSLongitudeRef     0x20003
#define  Exif_GPSInfo_GPSLongitude        0x20004
#define  Exif_GPSInfo_GPSAltitudeRef      0x20005
#define  Exif_GPSInfo_GPSAltitude         0x20006
#define  Exif_GPSInfo_GPSTimeStamp        0x20007
#define  Exif_GPSInfo_GPSDateStamp        0x2001D
#define  Exif_GPSInfo_CompassDirectionRef 0x20010
#define  Exif_GPSInfo_CompassDirection    0x20011
#define  Exif_GPSInfo_CompassPitchRef     0x20013
#define  Exif_GPSInfo_CompassPitch        0x20014
#define  Exif_GPSInfo_CompassRollRef      0x20015
#define  Exif_GPSInfo_CompassRoll         0x20016
*/
/*
/// Exif data (variable, stored with each frame) used for KML (not only)
#define  Exif_Image_ImageDescription_Index      0x00
#define  Exif_Photo_DateTimeOriginal_Index      0x01
#define  Exif_Photo_SubSecTimeOriginal_Index    0x02
#define  Exif_GPSInfo_GPSLatitudeRef_Index      0x03
#define  Exif_GPSInfo_GPSLatitude_Index         0x04
#define  Exif_GPSInfo_GPSLongitudeRef_Index     0x05
#define  Exif_GPSInfo_GPSLongitude_Index        0x06
#define  Exif_GPSInfo_GPSAltitudeRef_Index      0x07
#define  Exif_GPSInfo_GPSAltitude_Index         0x08
#define  Exif_GPSInfo_GPSTimeStamp_Index        0x09
#define  Exif_GPSInfo_GPSDateStamp_Index        0x0a
#define  Exif_GPSInfo_CompassDirectionRef_Index 0x0b
#define  Exif_GPSInfo_CompassDirection_Index    0x0c
#define  Exif_GPSInfo_CompassPitchRef_Index     0x0d
#define  Exif_GPSInfo_CompassPitch_Index        0x0e
#define  Exif_GPSInfo_CompassRollRef_Index      0x0f
#define  Exif_GPSInfo_CompassRoll_Index         0x10
#define  ExifKmlNumber                          0x11
*/
typedef struct {
  int                   segment_duration;
  int                   segment_length;
  int                   greedy;
  int                   ignore_fps;
  int                   save_gp; //if non zero, current circbuf pointer will be saved to global pointer, so imgsrv can report /pointers
  char                  path_prefix[256];
  char                  path[300];
  int                   cirbuf_rp; //!-1 - invalid
  int                   fd_circ;   //! file descriptor for circbuf
  int                   fd_head;   //! file descriptor for JPEG header
//  int                   fd_sens;   //! file descriptor for sensor/compressor parameters
  int                   fd_fparmsall;   //! file descriptor for sensor/compressor parameters
  int                   fd_exif;   //! file descriptor for Exif data
  int                   head_size; //! JPEG header size
  char                  jpegHeader [JPEG_HEADER_MAXSIZE];
  int                   metadata_start;
  struct interframe_params_t frame_params;
  struct interframe_params_t this_frame_params;
  int                   jpeg_len;
  int                   frame_period ; //!in microseconds (1/10 of what is needed for the Ogm header)
  int                   width;
  int                   height;
  int                   starting;
  int                   running;
  int                   last_error_code;
  ogg_stream_state      os;
  ogg_page              og;
  ogg_packet            op;
  elph_ogg_packet       eop;
  int                   serialno;
  ogg_int64_t           packetno;
  ogg_int64_t           granulepos;
  FILE*                 vf; //! video file (ogm, fopen)
  int                   ivf; //! video file (jpeg, mov - open)
  int                   last; //last packet in a file

  int                   exif; // 1 - calculate and include Exif headers in each frame
//  exif_pointers_t       ep;
//  int                   exifValid;
  int                   exifSize; //signed
  unsigned char         ed[MAX_EXIF_SIZE];

  int                   circ_buff_size;
  int                   senspars_size;
  char                  debug_name[256];
//  FILE*             debug_file;
  int                   set_samples_per_unit;
  double                timescale; //! current timescale, default 1.0
  double                set_timescale;
  double                start_after_timestamp; /// delay recording start to after frame timestamp
  int                   max_frames;
  int                   set_max_frames;
  int                   frames_per_chunk;
  int                   set_frames_per_chunk; // quicktime -  index for fast forward?
  int                   frameno;
  int*                  frame_lengths;
  off_t                 frame_data_start; //! Quicktime (and else?) - frame data start (0xff 0xd8...) 
  ogg_int64_t           time_unit;
  int                   formats;          //! bitmask of used (initialized) formats
  int                   format;           //! output file format
  int                   set_format;       //! output format to set (will be updated after stop)
  elph_packet_chunk     packetchunks[7];
  int                   chunk_index;
  int                   buf_overruns;
  int                   buf_min;
  int                   set_frames_skip;  //! will be copied to frames_skip if stopped or at start
  int                   frames_skip;      //! number of frames to skip after the one recorded (for time lapse)
                                          //! if negetive - -(interval between frames in seconds)
  int                   frames_skip_left; //! number of frames left to skip before the next one to be processed
                                          //! if (frames_skip <0) - next timestamp to save an image
//kml stuff
  int                   kml_enable;       //! enable KML file generation
  int                   kml_used;         //! KML file generation used (change only when stopped)
  char                  kml_path[300];    //! full path for KML file (if any)
  FILE*                 kml_file;         //! stream to write kml file
  double                kml_horHalfFov;   //! half horizontal Fov (degrees)
  double                kml_vertHalfFov;  //! half vertical Fov (degrees)
  double                kml_near;         //! Use in KML "near" parameter (<=0 - don't use it)
  int                   kml_height_mode;  //! 1 - actual, 0 - ground
  double                kml_height;       //! extra height to add
  int                   kml_period;       //! generate PhotoOverlay for each kml_period seconds;
  int                   kml_last_ts;      //! last generated kml file timestamp
  int                   kml_last_uts;     //! last generated kml file timestamp, microseconds
  struct exif_dir_table_t kml_exif[ExifKmlNumber] ;  //! store locations of the fields needed for KML generations in the Exif block
   

} camogm_state;
extern int   debug_level;
extern FILE* debug_file;
extern camogm_state * state;
void put_uint16(void *buf, u_int16_t val);
void put_uint32(void *buf, u_int32_t val);
void put_uint64(void *buf, u_int64_t val);
unsigned long getGPValue(unsigned long GPNumber);
void setGValue(unsigned long  GNumber,   unsigned long value);
int  waitDaemonEnabled(int daemonBit); // <0 - use default
int  isDaemonEnabled(int daemonBit); // <0 - use default

