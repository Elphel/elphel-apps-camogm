/** @file ogmstreams.h */
#ifndef __OGGSTREAMS_H
#define __OGGSTREAMS_H

/**
 * Taken from http://tobias.everwicked.com/packfmt.htm
 *
 *
 *   First packet (header)
 *   ---------------------
 *
 *   pos    | content                 | description
 *   -------|-------------------------|----------------------------------
 *   0x0000 | 0x01                    | indicates 'header packet'
 *   0x0001 | stream_header           | the size is indicated in the
 *   &nbsp; | &nbsp;                  | size member
 *
 *
 *   Second packet (comment)
 *   -----------------------
 *
 *   pos    | content                 | description
 *   -------|-------------------------|----------------------------------
 *   0x0000 | 0x03                    | indicates 'comment packet'
 *   0x0001 | data                    | see vorbis doc on www.xiph.org
 *
 *
 *   Data packets
 *   ------------
 *
 *   pos      | content                 | description
 *   ---------|-------------------------|----------------------------------
 *   0x0000   | Bit0  0                 | indicates data packet
 *   &nbsp;   | Bit1  Bit 2 of lenbytes | &nbsp;
 *   &nbsp;   | Bit2  unused            | &nbsp;
 *   &nbsp;   | Bit3  keyframe          | &nbsp;
 *   &nbsp;   | Bit4  unused            | &nbsp;
 *   &nbsp;   | Bit5  unused            | &nbsp;
 *   &nbsp;   | Bit6  Bit 0 of lenbytes | &nbsp;
 *   &nbsp;   | Bit7  Bit 1 of lenbytes | &nbsp;
 *   0x0001   | LowByte                 | Length of this packet in samples
 *   &nbsp;   | ...                     | (frames for video, samples for
 *   &nbsp;   | HighByte                | audio, 1ms units for text)
 *   0x0001+  | data                    | packet contents
 *   lenbytes | &nbsp;                  | &nbsp;
 *
 */

/** OggDS headers */
/** Header for the new header format */
typedef struct stream_header_video {
	ogg_int32_t width;
	ogg_int32_t height;
} stream_header_video;

typedef struct stream_header_audio {
	ogg_int16_t channels;
	ogg_int16_t blockalign;
	ogg_int32_t avgbytespersec;
} stream_header_audio;

typedef struct stream_header {
	char streamtype[8];
	char subtype[4];

	ogg_int32_t size;               // size of the structure

	ogg_int64_t time_unit;          // in reference time
	ogg_int64_t samples_per_unit;
	ogg_int32_t default_len;        // in media time

	ogg_int32_t buffersize;
//	ogg_int16_t     bits_per_sample;
	ogg_int32_t bits_per_sample;

	union {
		// Video specific
		stream_header_video video;
		// Audio specific
		stream_header_audio audio;
	} sh;

//	ogg_int16_t     padding;
	ogg_int32_t padding;

} stream_header;

typedef struct old_stream_header {
	char streamtype[8];
	char subtype[4];

	ogg_int32_t size;               // size of the structure

	ogg_int64_t time_unit;          // in reference time
	ogg_int64_t samples_per_unit;
	ogg_int32_t default_len;        // in media time

	ogg_int32_t buffersize;
	ogg_int16_t bits_per_sample;

	ogg_int16_t padding;

	union {
		// Video specific
		stream_header_video video;
		// Audio specific
		stream_header_audio audio;
	} sh;

} old_stream_header;

/// Some defines from OggDS
#define PACKET_TYPE_HEADER       0x01
#define PACKET_TYPE_COMMENT      0x03
#define PACKET_TYPE_BITS         0x07
#define PACKET_LEN_BITS01        0xc0
#define PACKET_LEN_BITS2         0x02
#define PACKET_IS_SYNCPOINT      0x08

#endif /* __OGGSTREAMS_H */
