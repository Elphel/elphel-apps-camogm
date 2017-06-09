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

static void audio_deinit(struct audio *audio);
static bool skip_audio(struct audio *audio, snd_pcm_uframes_t frames);

/**
 * Initialize audio interface.
 * Asserts:
 *  audio->get_fpga_time pointer to callback function is not set
 * @param   audio   pointer to a structure containing audio parameters and buffers
 * @param   restart flag indicating that full restart is requested
 * @return  None
 */
void audio_init(struct audio *audio, bool restart)
{
	// apply settings in case they have changed
	audio->audio_enable = audio->set_audio_enable;
	audio->audio_rate = audio->set_audio_rate;
	audio->audio_channels = audio->set_audio_channels;
	audio->audio_volume = audio->set_audio_volume;
	audio->ctx_a.sample_time = SAMPLE_TIME;

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

		audio->audio_format = SND_PCM_FORMAT_S16_LE;
		audio->ctx_a.sbuffer_len = audio->audio_rate * audio->ctx_a.sample_time;
		audio->ctx_a.sbuffer_len /= 1000;
		audio->ctx_a.sbuffer_len -= audio->ctx_a.sbuffer_len % 2;
		// 'while' loop here just to break initialization sequence after an error
		while (true) {
			size_t buff_size = audio->ctx_a.sbuffer_len * audio->audio_channels * (snd_pcm_format_physical_width(audio->audio_format) / 8);
			audio->ctx_a.sbuffer = malloc(buff_size);
			if (audio->ctx_a.sbuffer == NULL) {
				D0(fprintf(debug_file, "error: can not allocate buffer for audio samples: %s\n", strerror(errno)));
				break;
			}
			D6(fprintf(debug_file, "audio sbuffer_len == %ld\n", audio->ctx_a.sbuffer_len));

			if ((err = snd_pcm_open(&audio->ctx_a.capture_hnd, audio->dev_name, SND_PCM_STREAM_CAPTURE, 0)) < 0)
				break;
			snd_pcm_hw_params_alloca(&hw_params);
			if ((err = snd_pcm_hw_params_any(audio->ctx_a.capture_hnd, hw_params)) < 0)
				break;
			if ((err = snd_pcm_hw_params_set_access(audio->ctx_a.capture_hnd, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
				break;
			if ((err = snd_pcm_hw_params_set_format(audio->ctx_a.capture_hnd, hw_params, audio->audio_format)) < 0)
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
			// read some samples to force the driver to start time stamping
			snd_pcm_readi(audio->ctx_a.capture_hnd, audio->ctx_a.sbuffer, 8);
			snd_pcm_status_alloca(&status);
			snd_pcm_status(audio->ctx_a.capture_hnd, status);
			snd_pcm_status_get_tstamp(status, &audio_ts);

			audio->ctx_a.begin_of_stream_with_audio = 1;
			audio->ctx_a.audio_trigger = 1;
			audio->ctx_a.audio_skip_samples = 0;

			struct timeval fpga_tv, sys_tv;
			assert(audio->get_fpga_time);
			audio->get_fpga_time((const struct audio *)audio, &fpga_tv);
			gettimeofday(&sys_tv, NULL);

			struct timeval d;                                       // system and FPGA time difference
			d = time_sub(&sys_tv, &fpga_tv);
			audio->sys_fpga_timediff = d;
			struct timeval tv;
			tv.tv_sec = audio_ts.tv_sec;
			tv.tv_usec = audio_ts.tv_usec;
			audio->ts_audio = time_sub(&tv, &d);
			audio->sf_timediff = tv;

			// === debug code ===
			snd_pcm_uframes_t val;
			snd_pcm_hw_params_get_buffer_size_max(hw_params, &val);
			fprintf(debug_file, "ALSA buffer size: %lu\n", val);
			// === end of debug ===

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

void audio_start(struct audio *audio)
{
	audio->audio_frameno = 0;
	audio->audio_samples = 0;
	audio->ctx_a.rem_samples = 0;
	audio->ctx_a.time_last.tv_sec = 0;
	audio->ctx_a.time_last.tv_usec = 0;
}

/**
 * Process audio stream.
 * Asserts:
 *  audio->write_samples pointer to a function is not set
 *  number of audio frames remaining for recording is positive value
 * @param   audio   pointer to a structure containing audio parameters and buffers
 * @return  None
 */
void audio_process(struct audio *audio)
{
	snd_pcm_sframes_t slen;
	int counter = 0;
	void *_buf;
	long _buf_len;
	struct timeval tv_sys;
	snd_timestamp_t ts;
	snd_pcm_status_t *status;                                   // allocated on stack, do not free

	if (audio->audio_enable == 0)
		return;

	assert(audio->write_samples);

	snd_pcm_status_alloca(&status);
	for (;;) {
		long avail = 0;
		int to_push_flag = 0;

		counter++;
		gettimeofday(&tv_sys, NULL);
		tv_sys.tv_usec += audio->ctx_a.sample_time;
		time_normalize(&tv_sys);
		snd_pcm_status(audio->ctx_a.capture_hnd, status);
		snd_pcm_status_get_tstamp(status, &ts);
		avail = snd_pcm_status_get_avail(status);

		// === debug code ====
		int sbf; // samples before recent frame
		int samples, total;
		struct timeval av_tdiff, ts_corrected;

		total = audio->audio_samples + audio->skip_samples;
		fprintf(debug_file, "recent frame tstamp: %ld:%06ld\n", audio->ts_video.tv_sec, audio->ts_video.tv_usec);
		fprintf(debug_file, "available samples: %ld, recorded samples (+skipped): %ld (%d)\n",
				avail, audio->audio_samples, total);
		ts_corrected = time_sub(&ts, &audio->sys_fpga_timediff);
		fprintf(debug_file, "tstamp: %ld:%06ld, corrected tstamp: %ld:%06ld\n", ts.tv_sec, ts.tv_usec, ts_corrected.tv_sec, ts_corrected.tv_usec);
		av_tdiff = time_sub(&ts_corrected, &audio->ts_video);
		samples = (int)floor(((double)av_tdiff.tv_sec + (double)av_tdiff.tv_usec / 1000000) * audio->audio_rate);
		fprintf(debug_file, "time diff since last frame: %ld:%06ld, # of samples since last frame: %d\n", av_tdiff.tv_sec, av_tdiff.tv_usec, samples);
		if (samples > avail) {
			// some samples have already been recorded
			samples -= avail;
			sbf = audio->audio_samples - samples;
		} else {
			sbf = audio->audio_samples + (avail - samples);
		}
		fprintf(debug_file, "samples before recent frame: %d\n", sbf);

		if (avail == 0) {
			snd_pcm_state_t s = snd_pcm_status_get_state(status);
			fprintf(debug_file, "stream state: %d\n", s);
		}
		audio->avail_samples = avail;
		// === end of debug ===

		assert(audio->ctx_a.rem_samples >= 0);
		snd_pcm_uframes_t to_read = audio->ctx_a.sbuffer_len;   // length in audio frames
		if (avail >= audio->ctx_a.sbuffer_len && audio->ctx_a.rem_samples == 0) {
			if (skip_audio(audio, avail))
				continue;
			to_push_flag = 1;
		}
		if (audio->ctx_a.rem_samples > 0) {
			if (audio->ctx_a.rem_samples > audio->ctx_a.sbuffer_len) {
				if (avail >= audio->ctx_a.sbuffer_len) {
					to_read = audio->ctx_a.sbuffer_len;
					audio->ctx_a.rem_samples -= audio->ctx_a.sbuffer_len;
					to_push_flag = 2;
				}
			} else {
				if (avail >= audio->ctx_a.rem_samples) {
					to_read = audio->ctx_a.rem_samples;
					audio->ctx_a.rem_samples = 0;
					to_push_flag = 2;
				}
			}
		}
		if (to_push_flag) {
			slen = snd_pcm_readi(audio->ctx_a.capture_hnd, audio->ctx_a.sbuffer, to_read);
			if (slen > 0) {
				int flag = 1;
				long offset = 0;
//				// check the length of the movie and sound track, proceed only if audio and video already in sync
//				if (to_push_flag == 1 && audio->ctx_a.begin_of_stream_with_audio) {
//					struct timeval sl = audio->ctx_a.time_last;
//					sl.tv_usec += audio->ctx_a.sample_time;
//					time_normalize(&sl);
//					struct timeval m_end;
//					m_end = audio->ts_video;
//					m_end.tv_usec += audio->frame_period / 2;
//					time_normalize(&m_end);
//					struct timeval m_len;
//					m_len = time_sub(&m_end, &audio->ctx_a.time_start);
//					if (time_comp(&sl, &m_len) > 0) {
//						D4(fprintf(debug_file, "Sound chunk is too early, skip it\n"));
//						break;
//					}
//				}
				// we need to skip some samples in a new session, but if we just switch the frames then
				// we need to split new samples in the buffer into two parts - for the previous file,
				// and the next one...
				// so we can just save in the first file new data, and in the next use "skip_samples" field
				if (audio->ctx_a.audio_skip_samples != 0) {
					D5(fprintf(debug_file, "skip_samples = %lld, available samples = %ld\n", audio->ctx_a.audio_skip_samples, slen));
					if (audio->ctx_a.audio_skip_samples >= slen) {
						audio->ctx_a.audio_skip_samples -= slen;
						flag = 0;
					} else {
						offset = audio->ctx_a.audio_skip_samples;
						audio->ctx_a.audio_skip_samples = 0;
					}
				}
				if (flag) {
					long samples = slen - offset;
					_buf = (void *)audio->ctx_a.sbuffer;
					_buf = (void *)((char *) _buf + offset * audio->audio_channels * (snd_pcm_format_physical_width(audio->audio_format) / 8));
					_buf_len = samples * audio->audio_channels * (snd_pcm_format_physical_width(audio->audio_format) / 8);
					audio->write_samples(audio, _buf, _buf_len, samples);

					float tr = 1.0 / audio->audio_rate;
					float l = tr * audio->audio_samples;
					unsigned long s = (unsigned long) l;
					l -= s;
					l *= 1000000;
					unsigned long us = (unsigned long) l;
					audio->ctx_a.time_last.tv_sec = s;
					audio->ctx_a.time_last.tv_usec = us;
					D6(fprintf(debug_file, "%d: sound time %lu:%06lu, at %ld:%06ld; samples: %ld\n",
							counter, s, us, tv_sys.tv_sec, tv_sys.tv_usec, samples));
				}
			} else {
				if (slen == -EPIPE || slen == -ESTRPIPE) {
					int err;
					fprintf(debug_file, "snd_pcm_readi returned error: %ld\n", (long)slen);
					err = snd_pcm_recover(audio->ctx_a.capture_hnd, slen, 0);
					snd_pcm_reset(audio->ctx_a.capture_hnd);
//					snd_pcm_drain(audio->ctx_a.capture_hnd);
//					err = snd_pcm_prepare(audio->ctx_a.capture_hnd);
					if (err != 0) {
						D0(fprintf(debug_file, "error: ALSA could not recover audio buffer, error code: %s\n", snd_strerror(err)));
						// TODO: restart audio interface
						break;
					} else {
						fprintf(debug_file, "audio error recover complete, trying to restart the stream\n");
//						snd_pcm_drain(audio->ctx_a.capture_hnd);
//						err = snd_pcm_prepare(audio->ctx_a.capture_hnd);
//						fprintf(debug_file, "snd_pcm_prepare returned %d\n", err);
					}
				}
			}
		} else {
			// no audio frames for processing, return
			break;
		}
	}
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
	struct timeval m_end, m_len, av_diff, frame_period;

	D6(fprintf(debug_file, "movie start at: %ld:%06ld\n", audio->ctx_a.time_start.tv_sec, audio->ctx_a.time_start.tv_usec));
	m_end = audio->ts_video;
	D6(fprintf(debug_file, "movie end at: %ld:%06ld\n", m_end.tv_sec, m_end.tv_usec));

	m_len = time_sub(&m_end, &audio->ctx_a.time_start);
	D6(fprintf(debug_file, "movie length: %ld:%06ld\n", m_len.tv_sec, m_len.tv_usec));
	audio->m_len = m_len;
	audio->ctx_a.time_start = m_end;

	assert(audio->get_fpga_time);

	// calculate how many samples we need to save now for the end
	struct timeval fpga_tv, sys_tv, audio_last;
	audio->get_fpga_time((const struct audio *)audio, &fpga_tv);
	gettimeofday(&sys_tv, NULL);

	D6(fprintf(debug_file, "_________________ END ____________________\n"););
	D6(fprintf(debug_file, "       sys time == %ld:%06ld\n", sys_tv.tv_sec, sys_tv.tv_usec));
	D6(fprintf(debug_file, "      FPGA time == %ld:%06ld\n", fpga_tv.tv_sec, fpga_tv.tv_usec));
	D6(fprintf(debug_file, "AUDIO  sys time == %ld:%06ld\n", audio->ctx_a.time_last.tv_sec, audio->ctx_a.time_last.tv_usec););

//	audio_last = audio->ctx_a.time_last;
//	if (m_len.tv_sec > audio_last.tv_sec) {
//		m_len.tv_sec--;
//		m_len.tv_usec += 1000000;
//	}
//	m_len.tv_sec -= audio_last.tv_sec;
//	m_len.tv_usec -= audio_last.tv_usec;
//	time_normalize(&m_len);
//	long to_finish_us = time_to_us(&m_len);
	av_diff = time_sub(&m_len, &audio->ctx_a.time_last);
	frame_period = us_to_time(audio->frame_period);
	av_diff = time_add(&av_diff, &frame_period);                // plus duration of the last video frame
	long to_finish_us = time_to_us(&av_diff);
	float period_us = (1.0 / audio->audio_rate) * 1000000;
//	D6(fprintf(debug_file, "... and now we need to save audio for this time: %ld:%06ld - i.e. %06ld usecs\n", m_len.tv_sec, m_len.tv_usec, to_finish_us));
	D6(fprintf(debug_file, "... and now we need to save audio for this time: %ld:%06ld - i.e. %06ld usecs\n", av_diff.tv_sec, av_diff.tv_usec, to_finish_us));
	if (to_finish_us > period_us) {
		double s = audio->audio_rate;
		s /= 1000.0;
		s *= to_finish_us;
		s /= 1000.0;
		audio->ctx_a.rem_samples = (long) s;
		// from the state->tv_video_start to ctx_a.time_last (with FPGA time recalculation)
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
			if (nvolume > 65535)
				nvolume = 65535;
			if (nvolume < 0)
				nvolume = 0;
			long long vol_new = volume_max;
			vol_new *= nvolume;
			vol_new /= 65535;
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

	if (audio->ctx_a.audio_skip_samples != 0) {
		D5(fprintf(debug_file, "skip_samples = %lld, available samples = %ld\n", audio->ctx_a.audio_skip_samples, frames));
		if (audio->ctx_a.audio_skip_samples >= frames) {
			audio->ctx_a.audio_skip_samples -= frames;
			skip = frames;
		} else {
			skip = audio->ctx_a.audio_skip_samples;
			audio->ctx_a.audio_skip_samples = 0;
		}
		snd_pcm_forward(audio->ctx_a.capture_hnd, skip);
		ret_val = true;
	}

	return ret_val;
}
