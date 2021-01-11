/*
 * $QNXLicenseC:
 * Copyright 2016, QNX Software Systems. All Rights Reserved.
 *
 * You must obtain a written license from and pay applicable license fees to QNX
 * Software Systems before you may reproduce, modify or distribute this software,
 * or any work that includes all or part of this software.   Free development
 * licenses are available for evaluation and non-commercial purposes.  For more
 * information visit http://licensing.qnx.com or email licensing@qnx.com.
 *
 * This file may contain contributions from others.  Please review this entire
 * file for other proprietary rights or license notices, as well as the QNX
 * Development Suite License Guide at http://licensing.qnx.com/license-guide/
 * for other information.
 * $
 */

#include <errno.h>
#include <fcntl.h>
#include <gulliver.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/termio.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>

#include <glib.h>
#include <sys/asoundlib.h>

#include "base/nugu_log.h"
#include "base/nugu_plugin.h"
#include "base/nugu_pcm.h"
#include "base/nugu_prof.h"

#define PLUGIN_DRIVER_NAME "qnx_alsa_pcm"

/* hjkim: patch for vmware. This definition is located in asound_common.h */
#ifndef SND_PCM_EVENT_UNDERRUN
#define SND_PCM_EVENT_UNDERRUN 3
#endif
#ifndef SND_PCM_EVENT_OVERRUN
#define SND_PCM_EVENT_OVERRUN 4
#endif
#ifndef SND_PCM_SFMT_U24_4_LE
#define SND_PCM_SFMT_U24_4_LE 28 /* 24 bits in 4 byte container */
#endif
#ifndef SND_PCM_SFMT_U24_4_BE
#define SND_PCM_SFMT_U24_4_BE 29 /* 24 bits in 4 byte container */
#endif
#ifndef SND_PCM_SFMT_S24_4_LE
#define SND_PCM_SFMT_S24_4_LE 30 /* 24 bits in 4 byte container */
#endif
#ifndef SND_PCM_SFMT_S24_4_BE
#define SND_PCM_SFMT_S24_4_BE 31 /* 24 bits in 4 byte container */
#endif
#ifndef SND_PCM_EVENT_MASK
#define SND_PCM_EVENT_MASK(mask) (1 << mask)
#endif

#define SAMPLE_SIZE 5120
#define SAMPLE_SILENCE (0.0f)

typedef struct _qnx_alsa_handle {
	snd_pcm_t *pcm_handle;
	snd_mixer_t *mixer_handle;
	pthread_t write_thread;
	pthread_t event_thread;
	pthread_mutex_t mutex;
	int card;
	int device;

	NuguPcm *pcm;

	int samplerate;
	int samplebyte;
	int channel;

	int stop;
	int pause;
	int done;

	int is_start;
	int is_first;

	guint timer;

	size_t written;
	useconds_t frag_period_us;
} qnx_alsa_handle;

static NuguPcmDriver *pcm_driver;

#if 1
static gboolean _playerEndOfStream(void *userdata)
{
	qnx_alsa_handle *handle = (qnx_alsa_handle *)userdata;

	nugu_info("%s:%d!!!", __func__, __LINE__);

	if (!handle) {
		nugu_error("wrong parameter");
		return FALSE;
	}

	handle->timer = 0;

	if (handle->pcm)
		nugu_pcm_emit_event(handle->pcm,
				    NUGU_MEDIA_EVENT_END_OF_STREAM);
	else
		nugu_error("wrong parameter");

	return FALSE;
}
static void *_pcm_write_thread(void *data)
{
	qnx_alsa_handle *handle = (qnx_alsa_handle *)data;
	char buf[SAMPLE_SIZE];
	int buf_size;
	int guard_time = 500;
	int written = 0;
	int written2 = 0;
	int is_first = 1;
	sigset_t signals;

	sigfillset(&signals);
	pthread_sigmask(SIG_BLOCK, &signals, NULL);

	while (1) {
		if (handle->stop) {
			usleep(10 * 1000);
			is_first = 1;
			continue;
		}

		if (!handle->is_start || handle->pause || handle->done) {
			usleep(10 * 1000);
			continue;
		}
		/* This is magic code for fixing hang up on vmware */
		if (handle->is_first) {
			nugu_info("%s:%d!!!", __func__, __LINE__);
			handle->is_first = 0;
			continue;
		}

		memset(buf, SAMPLE_SILENCE, SAMPLE_SIZE);
		buf_size = SAMPLE_SIZE;

		// nugu_info("buffer data size check");
		if (nugu_pcm_get_data_size(handle->pcm) > 0) {
			if (is_first) {
				// nugu_info("%s:%d!!!", __func__, __LINE__);
				nugu_prof_mark(
					NUGU_PROF_TYPE_TTS_FIRST_PCM_WRITE);
				is_first = 0;
			}

			buf_size =
				nugu_pcm_get_data(handle->pcm, buf, buf_size);
			handle->written += buf_size;
			printf("bytes written = (%d/%d)\n", buf_size,
			       (int)handle->written);
		} else if (nugu_pcm_receive_is_last_data(handle->pcm) &&
			   !handle->done) {
			nugu_info("done");
			if (handle->timer != 0)
				g_source_remove(handle->timer);

			handle->timer = g_timeout_add(
				guard_time, _playerEndOfStream, handle);
			handle->done = 1;
			continue;
		}

		usleep(handle->frag_period_us);

		written =
			snd_pcm_plugin_write(handle->pcm_handle, buf, buf_size);
		written2 = 0;
		if (written != buf_size) {
			snd_pcm_channel_status_t status;

			nugu_warn("retry to write %d", buf_size - (int)written);
			memset(&status, 0, sizeof(status));
			status.channel = SND_PCM_CHANNEL_PLAYBACK;
			if (snd_pcm_plugin_status(handle->pcm_handle, &status) <
			    0) {
				fprintf(stderr,
					"underrun: playback channel status error\n");
				continue;
			}

			switch (status.status) {
			case SND_PCM_STATUS_UNDERRUN:
				nugu_warn("Audio underrun occured");
			case SND_PCM_STATUS_READY:
				if (snd_pcm_plugin_prepare(
					    handle->pcm_handle,
					    SND_PCM_CHANNEL_PLAYBACK) < 0) {
					fprintf(stderr,
						"underrun: playback channel prepare error\n");
					//cleanup_and_exit(EXIT_FAILURE);
				}
				break;
			case SND_PCM_STATUS_UNSECURE:
				fprintf(stderr, "Channel unsecure\n");
				if (snd_pcm_plugin_prepare(
					    handle->pcm_handle,
					    SND_PCM_CHANNEL_PLAYBACK) < 0) {
					fprintf(stderr,
						"unsecure: playback channel prepare error\n");
					//cleanup_and_exit(EXIT_FAILURE);
				}
				break;
			case SND_PCM_STATUS_ERROR:
				nugu_error("SND_PCM_STATUS_ERROR");
				//cleanup_and_exit(EXIT_FAILURE);
				break;
			case SND_PCM_STATUS_PREEMPTED:
				nugu_error("SND_PCM_STATUS_PREEMPTED");
				//cleanup_and_exit(EXIT_FAILURE);
				break;
			case SND_PCM_STATUS_CHANGE: {
				snd_pcm_channel_params_t pp;
				nugu_warn("Audio device change occured");
				if (snd_pcm_plugin_params(handle->pcm_handle,
							  &pp) < 0) {
					fprintf(stderr,
						"device change: snd_pcm_plugin_params failed, why_failed = %d\n",
						pp.why_failed);
					//cleanup_and_exit(EXIT_FAILURE);
				}
				if (snd_pcm_plugin_prepare(
					    handle->pcm_handle,
					    SND_PCM_CHANNEL_PLAYBACK) < 0) {
					fprintf(stderr,
						"device change: playback channel prepare error\n");
					//cleanup_and_exit(EXIT_FAILURE);
				}
			} break;
			case SND_PCM_STATUS_PAUSED:
				nugu_info("SND_PCM_STATUS_PAUSED");
				/* Fall-Through - no break */
			case SND_PCM_STATUS_SUSPENDED: {
				fd_set wfds;
				int pcm_fd;

				nugu_info("SND_PCM_STATUS_SUSPENDED");

				/* Wait until there is more room in the buffer (unsuspended/resumed) */
				FD_ZERO(&wfds);
				pcm_fd = snd_pcm_file_descriptor(
					handle->pcm_handle,
					SND_PCM_CHANNEL_PLAYBACK);
				FD_SET(pcm_fd, &wfds);

				if (select(pcm_fd + 1, NULL, &wfds, NULL,
					   NULL) == -1)
					perror("select");
				continue; /* Go back to the top of the write loop */
			} break;
			default:
				break;
			}
			nugu_info("retry buf remain data => (%d/%d)", written,
				  buf_size);
			usleep(handle->frag_period_us);
			written2 = snd_pcm_plugin_write(handle->pcm_handle,
							buf + written,
							buf_size - written);
		}

		if ((written + written2) < buf_size)
			nugu_warn("write failed %d bytes",
				  buf_size - (written + written2));
		else
			nugu_info("write done(%d/%d)!!", written + written2,
				  buf_size);
	}

	printf("thread is destroyed\n");

	return NULL;
}
static void *_pcm_event_thread(void *data)
{
	qnx_alsa_handle *handle = (qnx_alsa_handle *)data;

	sigset_t signals;
	fd_set ofds;
	snd_pcm_event_t event;
	int rtn;
	int pcm_fd;

	sigfillset(&signals);
	pthread_sigmask(SIG_BLOCK, &signals, NULL);

	while (1) {
#if 0
		if (handle->stop) {
			usleep(10 * 1000);
			continue;
		}

		if (!handle->is_start || handle->pause) {
			usleep(10 * 1000);
			continue;
		}
#endif
		pcm_fd = snd_pcm_file_descriptor(handle->pcm_handle,
						 SND_PCM_CHANNEL_PLAYBACK);

		FD_ZERO(&ofds);
		FD_SET(pcm_fd, &ofds);

		if (select(pcm_fd + 1, NULL, NULL, &ofds, NULL) == -1)
			perror("select");

		nugu_info("fd:%d - %s:%d!!!", pcm_fd, __func__, __LINE__);

		if ((rtn = snd_pcm_channel_read_event(handle->pcm_handle,
						      SND_PCM_CHANNEL_PLAYBACK,
						      &event)) == EOK) {
			switch (event.type) {
			case SND_PCM_EVENT_AUDIOMGMT_STATUS:
				nugu_info("SND_PCM_EVENT_AUDIOMGMT_STATUS");
				break;
			case SND_PCM_EVENT_AUDIOMGMT_MUTE:
				nugu_info("SND_PCM_EVENT_AUDIOMGMT_MUTE");
				break;
			case SND_PCM_EVENT_OUTPUTCLASS:
				nugu_info("SND_PCM_EVENT_OUTPUTCLASS");
				printf("Output class event received - output class changed from %d to %d\n",
				       event.data.outputclass.old_output_class,
				       event.data.outputclass.new_output_class);
				break;
			case SND_PCM_EVENT_UNDERRUN:
				nugu_info("SND_PCM_EVENT_UNDERRUN");
				break;
			default:
				nugu_info("Unknown PCM event type - %d\n",
					  event.type);
				break;
			}
		} else
			printf("snd_pcm_channel_read_event() failed with %d\n",
			       rtn);
	}
	return NULL;
}
#endif
static int _pcm_create(NuguPcmDriver *driver, NuguPcm *pcm,
		       NuguAudioProperty prop)
{
	qnx_alsa_handle *alsa_handle = g_malloc0(sizeof(qnx_alsa_handle));
	alsa_handle->pcm = pcm;

	nugu_info("%s:%d!!!", __func__, __LINE__);
	pthread_mutex_init(&alsa_handle->mutex, NULL);
	pthread_create(&alsa_handle->write_thread, NULL, _pcm_write_thread,
		       alsa_handle);
	pthread_create(&alsa_handle->event_thread, NULL, _pcm_event_thread,
		       alsa_handle);

	nugu_pcm_set_driver_data(pcm, alsa_handle);

	return 0;
}

static void _pcm_destroy(NuguPcmDriver *driver, NuguPcm *pcm)
{
	qnx_alsa_handle *alsa_handle = nugu_pcm_get_driver_data(pcm);

	nugu_info("%s:%d!!!", __func__, __LINE__);

	g_return_if_fail(pcm != NULL);

	if (alsa_handle == NULL)
		return;

	memset(alsa_handle, 0, sizeof(qnx_alsa_handle));
	g_free(alsa_handle);
	nugu_pcm_set_driver_data(pcm, NULL);
}

static int _pcm_start(NuguPcmDriver *driver, NuguPcm *pcm)
{
	qnx_alsa_handle *alsa_handle = nugu_pcm_get_driver_data(pcm);
	int rtn;

	nugu_info("%s:%d!!!", __func__, __LINE__);

	g_return_val_if_fail(pcm != NULL, -1);

	if (alsa_handle == NULL) {
		nugu_error("internal error");
		return -1;
	}

	if (alsa_handle->is_start) {
		nugu_dbg("already started");
		return 0;
	}

	{
		snd_pcm_filter_t pevent;
		snd_pcm_channel_info_t pi;
		int rtn;

		int fragsize = SAMPLE_SIZE;

		snd_pcm_channel_setup_t setup;
		//useconds_t frag_period_us;
		snd_mixer_group_t group;
		snd_pcm_channel_params_t pp;

		if ((rtn = snd_pcm_open_preferred(
			     &alsa_handle->pcm_handle, &alsa_handle->card,
			     &alsa_handle->device,
			     (SND_PCM_OPEN_PLAYBACK | SND_PCM_OPEN_NONBLOCK))) <
		    0) {
			fprintf(stderr, "snd_pcm_open_preferred failed - %s\n",
				snd_strerror(rtn));
			return -1;
		}

		/* Enable PCM events */
		pevent.enable =
			SND_PCM_EVENT_MASK(SND_PCM_EVENT_AUDIOMGMT_STATUS) |
			SND_PCM_EVENT_MASK(SND_PCM_EVENT_AUDIOMGMT_MUTE) |
			SND_PCM_EVENT_MASK(SND_PCM_EVENT_OUTPUTCLASS) |
			SND_PCM_EVENT_MASK(SND_PCM_EVENT_UNDERRUN);
		snd_pcm_set_filter(alsa_handle->pcm_handle,
				   SND_PCM_CHANNEL_PLAYBACK, &pevent);

		memset(&pi, 0, sizeof(pi));
		pi.channel = SND_PCM_CHANNEL_PLAYBACK;
		if ((rtn = snd_pcm_plugin_info(alsa_handle->pcm_handle, &pi)) <
		    0) {
			fprintf(stderr, "snd_pcm_plugin_info failed: %s\n",
				snd_strerror(rtn));
			//cleanup_and_exit(EXIT_FAILURE);
			return -2;
		}

		memset(&pp, 0, sizeof(pp));

		pp.mode = SND_PCM_MODE_BLOCK;
		pp.channel = SND_PCM_CHANNEL_PLAYBACK;
		pp.start_mode = SND_PCM_START_FULL;
		pp.stop_mode = SND_PCM_STOP_STOP;

		nugu_info("pi.max_fragment_size: %d\n", pi.max_fragment_size);
		pp.buf.block.frag_size = pi.max_fragment_size;
		if (fragsize != -1) {
			pp.buf.block.frag_size = fragsize;
		}
		pp.buf.block.frags_max = -1;
		pp.buf.block.frags_buffered_max = 0;
		pp.buf.block.frags_min = 1;

		pp.format.interleave = 1;
		pp.format.rate = 22050;
		pp.format.voices = 1;
		pp.format.format = SND_PCM_SFMT_S16_LE;
		strcpy(pp.sw_mixer_subchn_name, "Wave playback channel");

		if ((rtn = snd_pcm_plugin_set_src_method(
			     alsa_handle->pcm_handle, 0)) != 0) {
			fprintf(stderr,
				"Failed to apply rate_method 0, using %d\n",
				rtn);
		}

		if ((rtn = snd_pcm_plugin_params(alsa_handle->pcm_handle,
						 &pp)) < 0) {
			fprintf(stderr,
				"snd_pcm_plugin_params failed: %s, why_failed = %d\n",
				snd_strerror(rtn), pp.why_failed);
			//cleanup_and_exit(EXIT_FAILURE);
			return -3;
		}

		memset(&setup, 0, sizeof(setup));
		memset(&group, 0, sizeof(group));
		setup.channel = SND_PCM_CHANNEL_PLAYBACK;
		setup.mixer_gid = &group.gid;
		if ((rtn = snd_pcm_plugin_setup(alsa_handle->pcm_handle,
						&setup)) < 0) {
			fprintf(stderr, "snd_pcm_plugin_setup failed: %s\n",
				snd_strerror(rtn));
			//cleanup_and_exit(EXIT_FAILURE);
			return -4;
		}
		printf("Format %s \n",
		       snd_pcm_get_format_name(setup.format.format));
		printf("Frag Size %d \n", setup.buf.block.frag_size);
		printf("Total Frags %d \n", setup.buf.block.frags);
		printf("Rate %d \n", setup.format.rate);
		printf("Voices %d \n", setup.format.voices);
		alsa_handle->frag_period_us =
			(((int64_t)setup.buf.block.frag_size * 1000000) /
			 (2 * 1 * 22050));
		printf("FragSize: %d\n", setup.buf.block.frag_size);
		printf("Frag Period is %d us\n", alsa_handle->frag_period_us);

		if (group.gid.name[0] == 0) {
			printf("Mixer Pcm Group [%s] Not Set \n",
			       group.gid.name);
		} else {
			printf("Mixer Pcm Group [%s]\n", group.gid.name);
			if ((rtn = snd_mixer_open(&alsa_handle->mixer_handle,
						  alsa_handle->card,
						  setup.mixer_device)) < 0) {
				fprintf(stderr, "snd_mixer_open failed: %s\n",
					snd_strerror(rtn));
				//cleanup_and_exit(EXIT_FAILURE);
				return -5;
			}
		}
	}

	if ((rtn = snd_pcm_plugin_prepare(alsa_handle->pcm_handle,
					  SND_PCM_CHANNEL_PLAYBACK)) < 0) {
		fprintf(stderr, "snd_pcm_plugin_prepare failed: %s\n",
			snd_strerror(rtn));
		return -2;
	}

	nugu_pcm_emit_status(pcm, NUGU_MEDIA_STATUS_READY);

	//pthread_mutex_lock(&alsa_handle->mutex);

	alsa_handle->is_start = 1;
	alsa_handle->is_first = 1;
	alsa_handle->pause = 0;
	alsa_handle->stop = 0;
	alsa_handle->written = 0;
	alsa_handle->done = 0;

	nugu_pcm_set_volume(pcm, nugu_pcm_get_volume(pcm));

	//pthread_mutex_unlock(&alsa_handle->mutex);

	nugu_dbg("start done");
	return 0;
}

static int _pcm_stop(NuguPcmDriver *driver, NuguPcm *pcm)
{
	qnx_alsa_handle *alsa_handle = nugu_pcm_get_driver_data(pcm);

	nugu_info("%s:%d!!!", __func__, __LINE__);

	g_return_val_if_fail(pcm != NULL, -1);

	if (alsa_handle == NULL) {
		nugu_error("internal error");
		return -1;
	}

	if (alsa_handle->is_start == 0) {
		nugu_dbg("already stopped");
		return 0;
	}

	//pthread_mutex_lock(&alsa_handle->mutex);

	alsa_handle->pause = 0;
	alsa_handle->stop = 1;
	alsa_handle->is_start = 0;

	//pthread_mutex_unlock(&alsa_handle->mutex);

	nugu_pcm_emit_status(pcm, NUGU_MEDIA_STATUS_STOPPED);

	nugu_dbg("stop done");

	return 0;
}

static int _pcm_pause(NuguPcmDriver *driver, NuguPcm *pcm)
{
	qnx_alsa_handle *alsa_handle = nugu_pcm_get_driver_data(pcm);

	nugu_info("%s:%d!!!", __func__, __LINE__);

	g_return_val_if_fail(pcm != NULL, -1);

	if (alsa_handle == NULL) {
		nugu_error("internal error");
		return -1;
	}

	if (alsa_handle->pause) {
		nugu_dbg("pcm is already paused");
		return 0;
	}

	pthread_mutex_lock(&alsa_handle->mutex);

	alsa_handle->pause = 1;

	pthread_mutex_unlock(&alsa_handle->mutex);

	nugu_pcm_emit_status(pcm, NUGU_MEDIA_STATUS_PAUSED);

	nugu_dbg("pause done");

	return 0;
}

static int _pcm_resume(NuguPcmDriver *driver, NuguPcm *pcm)
{
	qnx_alsa_handle *alsa_handle = nugu_pcm_get_driver_data(pcm);

	nugu_info("%s:%d!!!", __func__, __LINE__);

	g_return_val_if_fail(pcm != NULL, -1);

	if (alsa_handle == NULL) {
		nugu_error("internal error");
		return -1;
	}

	if (!alsa_handle->pause) {
		nugu_dbg("pcm is not paused");
		return 0;
	}

	pthread_mutex_lock(&alsa_handle->mutex);

	alsa_handle->pause = 0;

	pthread_mutex_unlock(&alsa_handle->mutex);

	nugu_pcm_emit_status(pcm, NUGU_MEDIA_STATUS_PLAYING);

	nugu_dbg("resume done");

	return 0;
}

static int _pcm_set_volume(NuguPcmDriver *driver, NuguPcm *pcm, int volume)
{
	qnx_alsa_handle *alsa_handle = nugu_pcm_get_driver_data(pcm);
	snd_mixer_group_t group;
	int rtn;

	nugu_info("%s:%d!!!", __func__, __LINE__);

	g_return_val_if_fail(pcm != NULL, -1);

	if (alsa_handle == NULL) {
		nugu_error("internal error");
		return -1;
	}

	if ((rtn = snd_mixer_group_read(alsa_handle->mixer_handle, &group)) <
	    0) {
		fprintf(stderr, "snd_mixer_group_read failed: %s\n",
			snd_strerror(rtn));
		return -2;
	}

	nugu_dbg("volume - min: %d, max: %d", group.min, group.max);

	group.volume.names.front_left = volume;
	group.volume.names.front_right = volume;

	if ((int)group.volume.names.front_left > group.max)
		group.volume.names.front_left = group.max;
	if ((int)group.volume.names.front_left < group.min)
		group.volume.names.front_left = group.min;
	if ((int)group.volume.names.front_right > group.max)
		group.volume.names.front_right = group.max;
	if ((int)group.volume.names.front_right < group.min)
		group.volume.names.front_right = group.min;

	if ((rtn = snd_mixer_group_write(alsa_handle->mixer_handle, &group)) <
	    0) {
		fprintf(stderr, "snd_mixer_group_write failed: %s\n",
			snd_strerror(rtn));
		return -3;
	}

	if (group.max == group.min)
		printf("Volume Now at %d:%d\n", group.max, group.max);
	else
		printf("Volume Now at %d:%d \n",
		       100 * (group.volume.names.front_left - group.min) /
			       (group.max - group.min),
		       100 * (group.volume.names.front_right - group.min) /
			       (group.max - group.min));
	return 0;
}

static int _pcm_get_position(NuguPcmDriver *driver, NuguPcm *pcm)
{
	qnx_alsa_handle *alsa_handle = nugu_pcm_get_driver_data(pcm);
	g_return_val_if_fail(pcm != NULL, -1);

	if (alsa_handle == NULL) {
		nugu_error("internal error");
		return -1;
	}

	if (alsa_handle->is_start == 0) {
		nugu_error("pcm is not started");
		return -1;
	}

	return (alsa_handle->written / alsa_handle->samplerate);
}

static int _pcm_push_data(NuguPcmDriver *driver, NuguPcm *pcm, const char *data,
			  size_t size, int is_last)
{
	qnx_alsa_handle *alsa_handle = nugu_pcm_get_driver_data(pcm);
	int playing_flag = 0;

	nugu_info("%s:%d!!!", __func__, __LINE__);

	g_return_val_if_fail(pcm != NULL, -1);

	if (alsa_handle == NULL) {
		nugu_error("pcm is not started");
		return -1;
	}

	if (alsa_handle->is_first == 1)
		playing_flag = 1;

	if (playing_flag &&
	    nugu_pcm_get_status(pcm) != NUGU_MEDIA_STATUS_PLAYING) {
		nugu_info("%s:%d!!!", __func__, __LINE__);
		nugu_pcm_emit_status(pcm, NUGU_MEDIA_STATUS_PLAYING);
		nugu_info("%s:%d!!!", __func__, __LINE__);
	}

	return 0;
}

static struct nugu_pcm_driver_ops pcm_ops = {
	/* nugu_pcm_driver */
	.create = _pcm_create, /* nugu_pcm_new() */
	.destroy = _pcm_destroy, /* nugu_pcm_free() */
	.start = _pcm_start, /* nugu_pcm_start() */
	.stop = _pcm_stop, /* nugu_pcm_stop() */
	.pause = _pcm_pause, /* nugu_pcm_pause() */
	.resume = _pcm_resume, /* nugu_pcm_resume() */
	.push_data = _pcm_push_data, /* nugu_pcm_push_data() */
	.set_volume = _pcm_set_volume, /* nugu_pcm_set_volume() */
	.get_position = _pcm_get_position /* nugu_pcm_get_position() */
};

static int init(NuguPlugin *p)
{
	nugu_dbg("'%s' plugin initialized",
		 nugu_plugin_get_description(p)->name);

	pcm_driver = nugu_pcm_driver_new(PLUGIN_DRIVER_NAME, &pcm_ops);
	if (!pcm_driver) {
		nugu_error("nugu_pcm_driver_new() failed");
		return -1;
	}

	if (nugu_pcm_driver_register(pcm_driver) != 0) {
		nugu_error("nugu_pcm_driver_register() failed");
		nugu_pcm_driver_free(pcm_driver);
		pcm_driver = NULL;
		return -1;
	}

	if (nugu_pcm_driver_get_default() != pcm_driver) {
		nugu_dbg("set default driver to portaudio");
		nugu_pcm_driver_set_default(pcm_driver);
	}

	nugu_dbg("'%s' plugin initialized done",
		 nugu_plugin_get_description(p)->name);

	return 0;
}

static int load(void)
{
	nugu_dbg("plugin loaded");

	return 0;
}

static void unload(NuguPlugin *p)
{
	nugu_dbg("'%s' plugin unloaded", nugu_plugin_get_description(p)->name);

	if (pcm_driver) {
		nugu_pcm_driver_remove(pcm_driver);
		nugu_pcm_driver_free(pcm_driver);
		pcm_driver = NULL;
	}

	nugu_dbg("'%s' plugin unloaded done",
		 nugu_plugin_get_description(p)->name);
}

NUGU_PLUGIN_DEFINE(
	/* NUGU SDK Plug-in description */
	PLUGIN_DRIVER_NAME, /* Plugin name */
	NUGU_PLUGIN_PRIORITY_DEFAULT, /* Plugin priority */
	"0.0.1", /* Plugin version */
	load, /* dlopen */
	unload, /* dlclose */
	init /* initialize */
);
