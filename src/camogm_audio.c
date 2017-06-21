/**
 * @file camogm_audio.c
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

#include <stdbool.h>
#include <assert.h>

#include "camogm.h"
#include "camogm_audio.h"
#include "thelper.h"

// for debug only
#include <math.h>

enum {
	AUDIO_NONE,
	AUDIO_PROCESS,
	AUDIO_FINISH,
	AUDIO_LAST_CHUNK
};

static void audio_deinit(struct audio *audio);
static bool skip_audio(struct audio *audio, snd_pcm_uframes_t frames);
static long frames_to_bytes(const struct audio *audio, long frames);
static void record_buffer(struct audio *audio, int opt);
static void recover_stream(struct audio *audio, snd_pcm_sframes_t err, snd_pcm_uframes_t xrun);
static void dummy_read(struct audio *audio);
static void write_silence(struct audio *audio);
static int realloc_buffers(struct context_audio *ctx);

/**
 * Initialize HW part of audio interface.
 * Asserts:
 *  audio->get_fpga_time pointer to callback function is not set
 * @param   audio   pointer to a structure containing audio parameters and buffers
 * @param   restart flag indicating that full restart is requested
 * @return  None
 */
void audio_init_hw(struct audio *audio, bool restart)
{
	// apply settings in case they have changed
	audio->audio_enable = audio->set_audio_enable;
	audio->audio_rate = audio->set_audio_rate;
	audio->audio_channels = audio->set_audio_channels;
	audio->audio_volume = audio->set_audio_volume;

	if(audio->audio_enable == 0) {
		return;
	}
	// set up audio device
	if (restart) {
		int err = 0;
		bool init_ok = false;
		unsigned int t = audio->audio_rate;
		unsigned int period_time = SAMPLE_TIME * 1000;
		unsigned int buffer_time = BUFFER_TIME * 1000;
		snd_pcm_hw_params_t *hw_params;                         // allocated on stack, do not free
		snd_pcm_sw_params_t *sw_params;                         // allocated on stack, do not free
		snd_pcm_status_t *status;                               // allocated on stack, do not free
		snd_timestamp_t audio_ts;

		audio->ctx_a.audio_format = SND_PCM_FORMAT_S16_LE;
		// 'while' loop here just to break initialization sequence after an error
		while (true) {
			if ((err = snd_pcm_open(&audio->ctx_a.capture_hnd, audio->dev_name, SND_PCM_STREAM_CAPTURE, 0)) < 0)
				break;
			snd_pcm_hw_params_alloca(&hw_params);
			if ((err = snd_pcm_hw_params_any(audio->ctx_a.capture_hnd, hw_params)) < 0)
				break;
			if ((err = snd_pcm_hw_params_set_access(audio->ctx_a.capture_hnd, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
				break;
			if ((err = snd_pcm_hw_params_set_format(audio->ctx_a.capture_hnd, hw_params, audio->ctx_a.audio_format)) < 0)
				break;

			if ((err = snd_pcm_hw_params_set_rate_near(audio->ctx_a.capture_hnd, hw_params, &t, 0)) < 0)
				break;
			if (audio->audio_rate != t)
				D1(fprintf(debug_file, "Requested audio sampling rate is not supported, set %u Hz\n", t));
			audio->audio_rate = t;

			if ((err = snd_pcm_hw_params_set_channels(audio->ctx_a.capture_hnd, hw_params, audio->audio_channels)) < 0)
				break;
			if ((err = snd_pcm_hw_params_set_period_time_near(audio->ctx_a.capture_hnd, hw_params, &period_time, 0)) < 0)
				break;
			if ((err = snd_pcm_hw_params_set_buffer_time_near(audio->ctx_a.capture_hnd, hw_params, &buffer_time, 0)) < 0)
				break;
			if ((err = snd_pcm_hw_params(audio->ctx_a.capture_hnd, hw_params)) < 0)
				break;

			snd_pcm_sw_params_alloca(&sw_params);
			if ((err = snd_pcm_sw_params_current(audio->ctx_a.capture_hnd, sw_params)) < 0)
				break;
			if ((err = snd_pcm_sw_params_set_tstamp_mode(audio->ctx_a.capture_hnd, sw_params, SND_PCM_TSTAMP_ENABLE)) < 0)
				break;
			if ((err = snd_pcm_sw_params_set_tstamp_type(audio->ctx_a.capture_hnd, sw_params, SND_PCM_TSTAMP_TYPE_GETTIMEOFDAY)) < 0)
				break;
			if ((err = snd_pcm_sw_params(audio->ctx_a.capture_hnd, sw_params)) < 0)
				break;

			init_ok = true;
			break;
		}
		if (init_ok) {
			snd_pcm_prepare(audio->ctx_a.capture_hnd);
			snd_pcm_reset(audio->ctx_a.capture_hnd);
			audio_set_volume(audio->audio_volume);
			dummy_read(audio);
			snd_pcm_status_alloca(&status);
			snd_pcm_status(audio->ctx_a.capture_hnd, status);
			snd_pcm_status_get_tstamp(status, &audio_ts);

			audio->begin_of_stream_with_audio = 1;
			audio->audio_trigger = 1;
			audio->audio_skip_samples = 0;

			struct timeval fpga_tv, sys_tv;
			assert(audio->get_fpga_time);
			audio->get_fpga_time((const struct audio *)audio, &fpga_tv);
			gettimeofday(&sys_tv, NULL);

			struct timeval d;                                       // system and FPGA time difference
			timersub(&sys_tv, &fpga_tv, &d);
			struct timeval tv;
			tv.tv_sec = audio_ts.tv_sec;
			tv.tv_usec = audio_ts.tv_usec;
			timersub(&tv, &d, &audio->ts_audio);

			D4(fprintf(debug_file, "audio_init OK, system time = %ld:%06ld, FPGA time = %ld:%06ld, audio start time = %ld:%06ld, audio_ts = %ld:%06ld\n",
					sys_tv.tv_sec, sys_tv.tv_usec, fpga_tv.tv_sec, fpga_tv.tv_usec, audio->ts_audio.tv_sec, audio->ts_audio.tv_usec,
					audio_ts.tv_sec, audio_ts.tv_usec));
		} else {
			audio->set_audio_enable = 0;
			audio->audio_enable = 0;
			D0(fprintf(debug_file, "Error: audio init failed and audio capture is disabled; ALSA error message: %s\n", snd_strerror(err)));
		}
	}
}

/**
 * Initialize SW part of audio interface. This function assumes that HW part has already been initialized and
 * all values required to calculate audio buffer size are properly set. Audio buffer is allocated here.
 * Asserts:
 *  audio buffer is not initialized when full restart is requested
 * @param   audio   pointer to a structure containing audio parameters and buffers
 * @param   restart flag indicating that full restart is requested
 * @param   frames  number of frames in video chunk; audio chunk is recorder after video chunk thus we need
 * this value to calculate buffer size
 * @return  None
 */
void audio_init_sw(struct audio *audio, bool restart, int frames)
{
	audio->audio_frameno = 0;
	audio->audio_samples = 0;
	audio->ctx_a.rem_samples = 0;
	audio->ctx_a.time_last.tv_sec = 0;
	audio->ctx_a.time_last.tv_usec = 0;
	audio->ctx_a.sample_time = SAMPLE_TIME;

	if (audio->audio_enable == 0)
		return;

	if (restart) {
		size_t buff_size;
		size_t max_buff_frames, def_buff_frames;                // maximum and default buffer sizes, in frames
		float v_chunk_time;                                     // duration of one video chunk, in seconds

		assert(audio->ctx_a.sbuffer == NULL);
		assert(audio->ctx_a.xrun_buffer == NULL);

		audio->ctx_a.sbuffer_pos = 0;

		/* decide if camogm can sleep using lseek into video buffer: if audio HW buffer size is less than
		 * video frame period then we need to process audio stream between video frames and must use shorter
		 * sleep periods to prevent buffer overflow, and we are good if audio HW buffer is bigger than
		 * video frame period.
		 */
		if (audio->frame_period_us < (BUFFER_TIME * 1000)) {
			audio->sleep_period_us = 0;
		} else {
			audio->sleep_period_us = (BUFFER_TIME * 1000) / 2;
		}

		/* maximum buffer size takes into account video frame period and the number of video frames in one chunk;
		 * if calculated maximum buffer size is less than BUFFER_TIME then BUFFER_TIME is used as audio buffer size,
		 */
		v_chunk_time = (float)(audio->frame_period_us * frames) / 1000000.0f;
		max_buff_frames = v_chunk_time * audio->audio_rate;
		max_buff_frames -= max_buff_frames % 2;
		def_buff_frames = audio->audio_rate * audio->ctx_a.sample_time;
		def_buff_frames /= 1000;
		def_buff_frames -= def_buff_frames % 2;
		if (max_buff_frames > def_buff_frames) {
			audio->ctx_a.sbuffer_len = max_buff_frames;
			// round buffer size up to the nearest multiple of SAMPLE_TIME to simplify things
			audio->ctx_a.sbuffer_len += (max_buff_frames % def_buff_frames);
		} else {
			audio->ctx_a.sbuffer_len = def_buff_frames;
		}
		audio->ctx_a.read_frames = def_buff_frames;
		buff_size = audio->ctx_a.sbuffer_len * audio->audio_channels * (snd_pcm_format_physical_width(audio->ctx_a.audio_format) / 8);
		audio->ctx_a.sbuffer = malloc(buff_size);
		audio->ctx_a.xrun_buffer = malloc(buff_size);
		if (audio->ctx_a.sbuffer == NULL || audio->ctx_a.xrun_buffer == NULL) {
			audio->set_audio_enable = 0;
			audio->audio_enable = 0;
			snd_pcm_close(audio->ctx_a.capture_hnd);
			D0(fprintf(debug_file, "error: can not allocate %u bytes for audio buffer: %s. Audio disabled\n", buff_size, strerror(errno)));
		}
		D6(fprintf(debug_file, "allocated audio buffer for %ld frames, read granularity is %ld frames\n",
				audio->ctx_a.sbuffer_len, audio->ctx_a.read_frames));
	}
}

/**
 * Process audio stream.
 * Asserts:
 *  audio->write_samples pointer to a function is not set
 *  number of audio frames remaining for recording is negative value
 * @param   audio   pointer to a structure containing audio parameters and buffers
 * @return  None
 */
void audio_process(struct audio *audio)
{
	snd_pcm_sframes_t slen;
	snd_timestamp_t ts;
	snd_pcm_status_t *status;                                   // allocated on stack, do not free

	if (audio->audio_enable == 0)
		return;

	assert(audio->write_samples);

	// first of all, save all we have in the buffer by the moment
	if (audio->save_data && audio->ctx_a.sbuffer_pos > audio->ctx_a.sample_time)
		record_buffer(audio, AUDIO_PROCESS);

	snd_pcm_status_alloca(&status);
	for (;;) {
		long avail = 0;
		int to_push_flag = AUDIO_NONE;

		snd_pcm_status(audio->ctx_a.capture_hnd, status);
		snd_pcm_status_get_tstamp(status, &ts);
		avail = snd_pcm_status_get_avail(status);

		D6(fprintf(debug_file, "\navailable audio frames: %ld, audio timestamp: %ld:%06ld\n", avail, ts.tv_sec, ts.tv_usec));
		assert(audio->ctx_a.rem_samples >= 0);

		snd_pcm_uframes_t to_read = audio->ctx_a.read_frames;   // length in audio frames
		if (audio->ctx_a.xrun_append > 0) {
			// finish xrun recovery process and fill the buffer with new frames untill it is full
			to_read = audio->ctx_a.xrun_append;

			// === debug code ===
			fprintf(debug_file, "append %ld audio frames\n", to_read);
			// === end of debug ===
		}
		if (avail >= to_read && audio->ctx_a.rem_samples == 0) {
			if (skip_audio(audio, avail))
				continue;
			to_push_flag = AUDIO_PROCESS;
			audio->ctx_a.xrun_append = 0;
		}
		if (audio->ctx_a.rem_samples > 0) {
			if (audio->ctx_a.rem_samples > audio->ctx_a.read_frames) {
				if (avail >= audio->ctx_a.read_frames) {
					to_read = audio->ctx_a.read_frames;
					audio->ctx_a.rem_samples -= audio->ctx_a.read_frames;
					to_push_flag = AUDIO_FINISH;
				}
			} else {
				if (avail >= audio->ctx_a.rem_samples) {
					to_read = audio->ctx_a.rem_samples;
					audio->ctx_a.rem_samples = 0;
					to_push_flag = AUDIO_LAST_CHUNK;
				}
			}
		}

		if (to_push_flag) {
			if ((to_read + audio->ctx_a.sbuffer_pos) > audio->ctx_a.sbuffer_len) {
				/* looks like we spent too much time somewhere and now driver has
				 * more audio frames than we can store in buffer, but overrun has not occured.
				 * We can not record all these frames as it is not proper time yet, but we can increase
				 * buffer size and continue with a bigger buffer.
				 */
				int err_code = realloc_buffers(&audio->ctx_a);
				if (err_code < 0) {
					D0(fprintf(debug_file, "error (%d), could not reallocate audio buffer\n", err_code));
					audio->set_audio_enable = 0;
					audio_deinit(audio);
				}
			}

			char *buff_ptr = audio->ctx_a.sbuffer + frames_to_bytes(audio, audio->ctx_a.sbuffer_pos);
			slen = snd_pcm_readi(audio->ctx_a.capture_hnd, buff_ptr, to_read);
			if (slen > 0) {
				audio->ctx_a.sbuffer_pos += slen;
				if (audio->save_data || (to_push_flag == AUDIO_FINISH) || (to_push_flag == AUDIO_LAST_CHUNK)) {
					record_buffer(audio, to_push_flag);
				}
			} else {
				recover_stream(audio, slen, avail);
			}
		} else {
			// no audio frames for processing, return
			break;
		}
	}
	audio->save_data = false;
}

/**
 * Finalize audio stream and stop hardware.
 * Asserts:
 *  audio->write_samples pointer to a function is not set
 * @param   audio   pointer to a structure containing audio parameters and buffers
 * @param   reset   flag indicating that HW should be reset as well
 * @return  None
 */
void audio_finish(struct audio *audio, bool reset)
{
	long to_finish_us;
	float period_us;
	struct timeval m_end, m_len, av_diff, frame_period;

	D6(fprintf(debug_file, "movie start at: %ld:%06ld\n", audio->ctx_a.time_start.tv_sec, audio->ctx_a.time_start.tv_usec));
	m_end = audio->ts_video;
	D6(fprintf(debug_file, "movie end at: %ld:%06ld\n", m_end.tv_sec, m_end.tv_usec));

	timersub(&m_end, &audio->ctx_a.time_start, &m_len);
	D6(fprintf(debug_file, "movie length: %ld:%06ld\n", m_len.tv_sec, m_len.tv_usec));
	audio->ctx_a.time_start = m_end;

	assert(audio->get_fpga_time);

	// first of all, save all we have in the buffer by the moment
	if (audio->ctx_a.sbuffer_pos > audio->ctx_a.sample_time)
		record_buffer(audio, AUDIO_PROCESS);

	// calculate how many samples we need to save now for the end
	struct timeval fpga_tv, sys_tv;
	audio->get_fpga_time((const struct audio *)audio, &fpga_tv);
	gettimeofday(&sys_tv, NULL);

	D6(fprintf(debug_file, "_________________ END ____________________\n"););
	D6(fprintf(debug_file, "       sys time == %ld:%06ld\n", sys_tv.tv_sec, sys_tv.tv_usec));
	D6(fprintf(debug_file, "      FPGA time == %ld:%06ld\n", fpga_tv.tv_sec, fpga_tv.tv_usec));
	D6(fprintf(debug_file, "AUDIO  sys time == %ld:%06ld\n", audio->ctx_a.time_last.tv_sec, audio->ctx_a.time_last.tv_usec););

	frame_period = us_to_time(audio->frame_period_us);
	if (timercmp(&m_len, &audio->ctx_a.time_last, >)) {
		timersub(&m_len, &audio->ctx_a.time_last, &av_diff);
		fprintf(debug_file, "av_diff: %ld:%06ld\n", av_diff.tv_sec, av_diff.tv_usec);
		timeradd(&av_diff, &frame_period, &av_diff);                // plus duration of the last video frame
		to_finish_us = time_to_us(&av_diff);
		D6(fprintf(debug_file, "... and now we need to save audio for this time: %ld:%06ld - i.e. %06ld usecs\n", av_diff.tv_sec, av_diff.tv_usec, to_finish_us));
	} else {
		// audio is ahead of video, we do not need to save any additional audio frames
		timersub(&audio->ctx_a.time_last, &m_len, &av_diff);
		D6(fprintf(debug_file, "audio/video difference: -%ld:%06ld\n", av_diff.tv_sec, av_diff.tv_usec));
		to_finish_us = 0;
	}
	period_us = (1.0 / audio->audio_rate) * 1000000;
	if (to_finish_us > period_us) {
		double s = audio->audio_rate;
		s /= 1000.0;
		s *= to_finish_us;
		s /= 1000.0;
		audio->ctx_a.rem_samples = (long) s;
		do {
			fprintf(debug_file, "process remaining %ld samples\n", audio->ctx_a.rem_samples);
			audio_process(audio);
			fprintf(debug_file, "rem_samples = %ld\n", audio->ctx_a.rem_samples);
			if (audio->ctx_a.rem_samples > 0)
	//			sched_yield();
				// TODO: calculate sleep time base on the number of samples required
				usleep(100000);
		} while (audio->ctx_a.rem_samples > 0);
	}

	if (reset)
		audio_deinit(audio);
}

void audio_set_volume(int nvolume)
{
	snd_mixer_t *mixer;
	snd_mixer_elem_t *elem;
	snd_mixer_selem_id_t *sid;                                  // allocated on stack, do not free

	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_open(&mixer, 0);
	snd_mixer_attach(mixer, "default");
	snd_mixer_selem_register(mixer, NULL, NULL);
	snd_mixer_load(mixer);

	for (elem = snd_mixer_first_elem(mixer); elem; elem = snd_mixer_elem_next(elem)) {
		snd_mixer_selem_get_id(elem, sid);
		if (!snd_mixer_selem_is_active(elem))
			continue;
		// set volume at percents for capture elements
		snd_mixer_elem_t *selem = snd_mixer_find_selem(mixer, sid);
		if (selem == NULL) {
			break;
		}
		long volume_min = 0;
		long volume_max = 0;
		if (snd_mixer_selem_get_capture_volume_range(selem, &volume_min, &volume_max) == 0) {
			// set volume only for capture
			if (nvolume > DEFAULT_AUDIO_VOLUME)
				nvolume = DEFAULT_AUDIO_VOLUME;
			if (nvolume < 0)
				nvolume = 0;
			long long vol_new = volume_max;
			vol_new *= nvolume;
			vol_new /= DEFAULT_AUDIO_VOLUME;
			long vol = 0;
			snd_mixer_selem_get_capture_volume(selem, SND_MIXER_SCHN_FRONT_LEFT, &vol);
			snd_mixer_selem_set_capture_volume_all(selem, vol_new);
			D6(fprintf(debug_file, "element %s - OLD min vol == %ld; max vol == %ld; volume == %ld\n",
					snd_mixer_selem_id_get_name(sid), volume_min, volume_max, vol));
			D6(snd_mixer_selem_get_capture_volume(selem, SND_MIXER_SCHN_FRONT_LEFT, &vol));
			D6(fprintf(debug_file, "element %s - NEW min vol == %ld; max vol == %ld; volume == %ld\n",
					snd_mixer_selem_id_get_name(sid), volume_min, volume_max, vol));
		}
	}
	snd_mixer_close(mixer);
}

static void audio_deinit(struct audio *audio)
{
	struct timeval tv;

	audio->audio_enable = 0;
	snd_pcm_drop(audio->ctx_a.capture_hnd);
	snd_pcm_close(audio->ctx_a.capture_hnd);
	free(audio->ctx_a.sbuffer);
	audio->ctx_a.sbuffer = NULL;
	audio->ctx_a.sbuffer_pos = 0;
	free(audio->ctx_a.xrun_buffer);
	audio->ctx_a.xrun_buffer = NULL;
	audio->ctx_a.xrun_pos = 0;

	gettimeofday(&tv, NULL);
	D4(fprintf(debug_file, "audio deinitialized at %ld:%06ld\n", tv.tv_sec, tv.tv_usec));
}

/**
 * Skip some audio frames in the beginning of recording to synchronize audio and video streams.
 * @param   audio   pointer to a structure containing audio parameters and buffers
 * @param   frames  number of frames available
 * @return  True if frames were skipped and False otherwise
 */
static bool skip_audio(struct audio *audio, snd_pcm_uframes_t frames)
{
	bool ret_val = false;
	snd_pcm_uframes_t skip;

	if (audio->audio_skip_samples != 0) {
		D5(fprintf(debug_file, "skip_samples = %lu, available samples = %ld\n", audio->audio_skip_samples, frames));
		if (audio->audio_skip_samples >= frames) {
			audio->audio_skip_samples -= frames;
			skip = frames;
		} else {
			skip = audio->audio_skip_samples;
			audio->audio_skip_samples = 0;
		}
		snd_pcm_forward(audio->ctx_a.capture_hnd, skip);
		ret_val = true;
	}

	return ret_val;
}

/**
 * Convert the number of audio frames given to the number of bytes.
 * @param   audio   pointer to a structure containing audio parameters and buffers
 * @return  number of bytes in audio frames
 */
static long frames_to_bytes(const struct audio *audio, long frames)
{
	return frames * audio->audio_channels * (snd_pcm_format_physical_width(audio->ctx_a.audio_format) / 8);
}

/**
 * Record data from audio buffer to storage media. Audio frames are recorded in SAMPLE_TIME chunks and this function
 * assumes that the buffer contains at least this amount of data.
 * @param   audio   pointer to a structure containing audio parameters and buffers
 */
static void record_buffer(struct audio *audio, int opt)
{
	void *_buf;
	long _buf_len;
	long frames;
	long rem_frames;

	/* check if xrun has occurred and write audio frames that were saved before xrun,
	 * then add silence equal in time to lost frames
	 */
	 if(audio->ctx_a.lost_frames > 0) {
		 _buf = audio->ctx_a.xrun_buffer;
		 _buf_len = frames_to_bytes(audio, audio->ctx_a.xrun_pos);
		 frames = audio->ctx_a.xrun_pos;
		 audio->write_samples(audio, _buf, _buf_len, frames);
		 audio->ctx_a.xrun_pos = 0;
		 D6(fprintf(debug_file, "record %ld audio frames which were saved before xrun\n", frames));

		 write_silence(audio);
	 }

	_buf = audio->ctx_a.sbuffer;
	rem_frames = audio->ctx_a.sbuffer_pos;
	while (rem_frames >= audio->ctx_a.read_frames || opt == AUDIO_LAST_CHUNK) {
		frames = audio->ctx_a.read_frames;
		if (opt == AUDIO_LAST_CHUNK)
			frames = rem_frames;
		_buf_len = frames_to_bytes(audio, frames);
		audio->write_samples(audio, _buf, _buf_len, frames);
		_buf += _buf_len;
		rem_frames -= frames;

		float tr = 1.0 / audio->audio_rate;
		float l = tr * audio->audio_samples;
		unsigned long s = (unsigned long) l;
		l -= s;
		l *= 1000000;
		unsigned long us = (unsigned long) l;
		audio->ctx_a.time_last.tv_sec = s;
		audio->ctx_a.time_last.tv_usec = us;
		D6(fprintf(debug_file, "sound time %lu:%06lu, recorded frames: %ld, frames: %ld, remaining frames: %ld\n",
				s, us, audio->audio_samples, frames, rem_frames));
		opt = AUDIO_NONE;
	}
	if (rem_frames > 0) {
		// move remaining data to the beginning of the buffer and record it on next iteration
		_buf_len = frames_to_bytes(audio, rem_frames);
		memcpy(audio->ctx_a.sbuffer, _buf, _buf_len);
		D6(fprintf(debug_file, "copy remaining %ld bytes to the beginning of audio buffer\n", _buf_len));
	}
	audio->ctx_a.sbuffer_pos = rem_frames;
}

/**
 * Try to recover audio stream after buffer overflow.
 * @param   audio   pointer to a structure containing audio parameters and buffers
 * @param   err     error code received after overflow event
 * @param   xrun    the number of audio frames returned by snd_pcm_format_get_avail() after xrun
 */
static void recover_stream(struct audio *audio, snd_pcm_sframes_t err, snd_pcm_uframes_t xrun)
{
	int ret;
	long prepend_frames;

	if (err == -EPIPE || err == -ESTRPIPE) {
		D0(fprintf(debug_file, "snd_pcm_readi returned error: %ld\n", err));
		ret = snd_pcm_recover(audio->ctx_a.capture_hnd, err, 0);
		if (ret != 0) {
			D0(fprintf(debug_file, "error: ALSA could not recover audio stream, error code: %s\n", snd_strerror(err)));
			// TODO: complete restart of audio interface
		} else {
			if (audio->ctx_a.sbuffer_pos > 0) {
				/* buffer contains some data which was saved before xrun,
				 * move the data to a temporary storage in order to free the buffer for current use
				 */
				size_t bytes = frames_to_bytes(audio, audio->ctx_a.sbuffer_pos);
				memcpy(audio->ctx_a.xrun_buffer, audio->ctx_a.sbuffer, bytes);
				audio->ctx_a.xrun_pos = audio->ctx_a.sbuffer_pos;
				audio->ctx_a.sbuffer_pos = 0;
			}

			dummy_read(audio);

			prepend_frames = xrun % audio->ctx_a.read_frames;
			audio->ctx_a.lost_frames = xrun - prepend_frames;
			snd_pcm_format_set_silence(audio->ctx_a.audio_format, audio->ctx_a.sbuffer, prepend_frames * audio->audio_channels);
			audio->ctx_a.sbuffer_pos = prepend_frames;
			audio->ctx_a.xrun_append = audio->ctx_a.read_frames - prepend_frames;
			D0(fprintf(debug_file, "audio error recover complete, trying to restart the stream\n"));

			// === debug code ===
			snd_pcm_status_t *s;
			snd_timestamp_t ts;
			snd_pcm_status_alloca(&s);
			snd_pcm_status(audio->ctx_a.capture_hnd, s);
			snd_pcm_status_get_tstamp(s, &ts);
			fprintf(debug_file, "xrun = %lu, prepend_frames = %ld, lost_frames = %ld, xrun_append = %ld\n",
					xrun, prepend_frames, audio->ctx_a.lost_frames, audio->ctx_a.xrun_append);
			fprintf(debug_file, "audio tstamp: %ld:%06ld\n", ts.tv_sec, ts.tv_usec);
			// === end of debug ===
		}
	}
}

/**
 * For some reason, ALSA reports incorrect number of frames (always 0) when audio stream has just started or
 * been recoverded after xrun. Reading small number of frames seems to restore normal operation.
 * @param   audio   pointer to a structure containing audio parameters and buffers
 */
static void dummy_read(struct audio *audio)
{
	char tmp_buff[32];
	snd_pcm_readi(audio->ctx_a.capture_hnd, tmp_buff, 8);
}

/**
 * Pad audio stream with silence frames instead of lost frames after buffer overflow not lose sync with video.
 * This function reuses xrun_buffer for silence frames and data from this buffer should be
 * recorded by the moment.
 * @param   audio   pointer to a structure containing audio parameters and buffers
 * @return  None
 */
static void write_silence(struct audio *audio)
{
	void *_buf;
	long _buf_len;
	long rem_frames;

	_buf = audio->ctx_a.xrun_buffer;
	_buf_len = frames_to_bytes(audio, audio->ctx_a.read_frames);
	snd_pcm_format_set_silence(audio->ctx_a.audio_format, _buf, audio->ctx_a.read_frames * audio->audio_channels);
	rem_frames = audio->ctx_a.lost_frames;
	while (rem_frames >= audio->ctx_a.read_frames) {
		audio->write_samples(audio, _buf, _buf_len, audio->ctx_a.read_frames);
		rem_frames -= audio->ctx_a.read_frames;
	}
	D6(fprintf(debug_file, "recorded %ld audio frames of silence\n", audio->ctx_a.lost_frames));
	assert(rem_frames == 0);
	audio->ctx_a.lost_frames = 0;
}

/**
 * Allocate new audio buffer with double size of the previous buffer
 * @param   ctx   pointer to current audio context
 * @return  0 in case buffers were reallocated and negative error code otherwise
 */
static int realloc_buffers(struct context_audio *ctx)
{
	int ret_val = 0;
	ssize_t new_size = snd_pcm_frames_to_bytes(ctx->capture_hnd, 2 * ctx->sbuffer_len);

	ctx->sbuffer = realloc(ctx->sbuffer, new_size);
	ctx->xrun_buffer = realloc(ctx->xrun_buffer, new_size);
	if (ctx->sbuffer == NULL || ctx->xrun_buffer == NULL) {
		ret_val = -CAMOGM_FRAME_MALLOC;
	} else {
		ctx->sbuffer_len = 2 * ctx->sbuffer_len;
		D1(fprintf(debug_file, "audio buffer reallocated, new size is %ld frames\n", ctx->sbuffer_len));
	}
	return ret_val;
}
