/**
 * @file camogm_audio.h
 * @brief Provides audio interface for camogm
 * @copyright Copyright (C) 2017 Elphel Inc.
 * @author AUTHOR <EMAIL>
 *
 * @par License:
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _CAMOGM_AUDIO_H
#define _CAMOGM_AUDIO_H

#include <sys/time.h>
#include <alsa/asoundlib.h>

#define SAMPLE_RATE               44100                         ///< default sampling rate
#define SAMPLE_CHANNELS           2                             ///< default number of audio channels
#define SAMPLE_TIME               200                           ///< restrict ALSA to have this period, in milliseconds
#define BUFFER_TIME               1000                          ///< approximate ALSA buffer duration, in milliseconds
#define DEFAULT_SND_DEVICE        "plughw:0,0"
#define AUDIO_CHANNELS_MIN        1
#define AUDIO_CHANNELS_MAX        2
#define AUDIO_RATE_MIN            11025
#define AUDIO_RATE_MAX            44100
#define DEFAULT_AUDIO_VOLUME      0xffff                        ///< absolute maximum audio volume

/**
 * @brief Audio recording context related to stream management.
 * Members of this structure should not be used outside audio module.
 */
struct context_audio {
	char *sbuffer;                                              ///< buffer for audio samples
	long sbuffer_len;                                           ///< total length of audio buffer, in audio frames
	long sbuffer_pos;                                           ///< pointer to current write position in audio buffer, in frames
	long read_frames;                                           ///< read granularity, in frames
	long sample_time;                                           ///< duration of one chunk of audio data, in ms

	struct timeval time_start;                                  ///< start time, set only when stream starts and updated with each new file
	struct timeval time_last;                                   ///< calculated time of last audio sample (this value is not taken from ALSA)
	long rem_samples;                                           ///< remaining samples

	long lost_frames;                                           ///< the number of frames lost after buffer overrun
	char *xrun_buffer;                                          ///< temporary storage for the data saved in buffer befor xrun
	long xrun_pos;                                              ///< number of samples in xrun buffer
	long xrun_append;                                           ///< save in buffer this number of frames after xrun, all other frames in chunk
	                                                            ///< will be silence

	snd_pcm_format_t audio_format;                              ///< format of audio samples as defined in 'enum snd_pcm_format_t'
	snd_pcm_t *capture_hnd;                                     ///< ALSA PCM handle
};

/**
 * @brief Various parameters related to audio recording.
 */
struct audio {
	int audio_enable;                                           ///< flag indicating that audio is enabled
	int audio_rate;                                             ///< sample rate, in Hz
	int audio_channels;                                         ///< number of channels
	int audio_volume;                                           ///< volume set in range [0..0xFFFF]
	int sync_port;                                              ///< synch audio stream to this sensor port

	int set_audio_enable;                                       ///< temporary storage for new value
	int set_audio_rate;                                         ///< temporary storage for new value
	int set_audio_channels;                                     ///< temporary storage for new value
	int set_audio_volume;                                       ///< temporary storage for new value

	long audio_frameno;                                         ///< frame number counter, used as index in frames_len array
	long audio_samples;                                         ///< samples counter
	unsigned long *frames_len;                                  ///< indexes of audio frames
	int *audio_samples_to_chunk;                                ///< an array of chunks, contains sample count in each chunk

	char *dev_name;                                             ///< the name of audio device to use
	struct context_audio ctx_a;                                 ///< current audio context

	struct timeval ts_audio;                                    ///< time stamp when audio stream started
	struct timeval ts_video;                                    ///< time stamp of each new frame
	struct timeval ts_video_start;                              ///< time stamp of starting video frame
	int frame_period_us;                                        ///< video frame period measured for #sync_port, in microseconds

	unsigned long audio_skip_samples;                           ///< skip this number audio frames to sync to video
	int begin_of_stream_with_audio;                             ///< flag indicating that A/V sync is in progress
	int audio_trigger;                                          ///< indicates the beginning of audio recording to make some initial set ups
	bool save_data;                                             ///< flag indicating that audio data should be recorded, otherwise audio frames should be
	                                                            ///< stored in buffer for delayed recording
	unsigned int sleep_period_us;                               ///< sleep period between frames while processing audio stream, in microseconds

	void (*get_fpga_time)(const struct audio *audio, struct timeval *tv);//< callback function which can get FPGA time
	int (*write_samples)(struct audio *audio, void *buff, long len, long slen); ///< callback function which actually write data to file, this must be set
};

void audio_init_hw(struct audio *audio, bool restart);
void audio_init_sw(struct audio *audio, bool restart, int frames);
void audio_process(struct audio *audio);
void audio_finish(struct audio *audio, bool reset);
void audio_set_volume(int nvolume);
unsigned long audio_get_hw_buffer_max(void);

#endif /* _CAMOGM_AUDIO_H */
