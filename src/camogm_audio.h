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

#define SAMPLE_RATE               44100
#define SAMPLE_CHANNELS           2
#define SAMPLE_TIME               200
#define DEFAULT_SND_DEVICE        "plughw:0,0"
#define AUDIO_SBUFFER_PREFIX      16
#define AUDIO_CHANNELS_MIN        1
#define AUDIO_CHANNELS_MAX        2
#define AUDIO_RATE_MIN            11025
#define AUDIO_RATE_MAX            44100
#define DEFAULT_AUDIO_VOLUME      0xffff

struct context_audio {
	char *sbuffer;                                              ///< buffer for audio samples
	long sbuffer_len;                                           ///< the length of samples buffer in samples
	long sample_time;                                           ///< duration of one audio sample in ms
	long long audio_count;                                      ///< total number of audio samples

	struct timeval time_start;                                  ///< start time, set only when stream starts and updated with each new file
	struct timeval time_last;                                   ///< time of last audio sample
	long rem_samples;                                           ///< remaining samples

	int begin_of_stream_with_audio;                             ///<
	int audio_trigger;                                          ///< indicates the beginning of audio recording to make some initial set ups
	long long audio_skip_samples;                               ///<

	snd_pcm_t *capture_hnd;                                     ///< ALSA PCM handle
};

struct audio {
	int audio_enable;                                           ///< flag indicating if audio is enabled
	int audio_rate;                                             ///< sample rate
	int audio_channels;                                         ///< number of channels
	int audio_volume;                                           ///< volume set in range [0..0xFFFF]

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
	int frame_period;                                           ///< video frame period, used to calculate time stamps for audio samples

	void (*get_fpga_time)(const struct audio *audio, struct timeval *tv);//< callback function which can get FPGA time
	int (*write_samples)(void *buff, int len, int slen);        ///< callback function which actually write data to file, this must be set
	                                                            ///< in the camogm_init_* function when appropriate format is selected
};

void audio_init(struct audio *audio, bool restart);
void audio_start(struct audio *audio);
void audio_process(struct audio *audio);
void audio_finish(struct audio *audio, bool reset);
void audio_set_volume(int nvolume);

#endif /* _CAMOGM_AUDIO_H */
