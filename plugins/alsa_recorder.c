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
#include "base/nugu_recorder.h"

#define PLUGIN_DRIVER_NAME "qnx_alsa_recorder"

/* hjkim: patch for vmware. This definition is located in asound_common.h */
#ifndef SND_PCM_EVENT_UNDERRUN
#define SND_PCM_EVENT_UNDERRUN 3
#endif
#ifndef SND_PCM_EVENT_OVERRUN
#define SND_PCM_EVENT_OVERRUN 4
#endif

#define SAMPLE_SIZE 512*10
//#define ENV_DUMP_PATH_RECORDER "/home/qnxuser/"

typedef struct _qnx_alsa_handle {
	NuguRecorder *rec;
	snd_pcm_t *pcm_handle;
	snd_mixer_t *mixer_handle;
	pthread_t read_thread;
	pthread_mutex_t mutex;

	int samplerate;
	int samplebyte;
	int channel;

	int start;
	int stop;

#if defined(ENV_DUMP_PATH_RECORDER)
	int dump_fd;
#endif

} qnx_alsa_handle;

static NuguRecorderDriver *rec_driver;

#if defined(ENV_DUMP_PATH_RECORDER)
static int _dumpfile_open(const char *path, const char *prefix)
{
	char ymd[9];
	char hms[7];
	time_t now;
	struct tm now_tm;
	char *buf = NULL;
	int fd;

	if (!path)
		return -1;

	now = time(NULL);
	localtime_r(&now, &now_tm);

	snprintf(ymd, 9, "%04d%02d%02d", now_tm.tm_year + 1900,
		 now_tm.tm_mon + 1, now_tm.tm_mday);
	snprintf(hms, 7, "%02d%02d%02d", now_tm.tm_hour, now_tm.tm_min,
		 now_tm.tm_sec);

	buf = g_strdup_printf("%s/%s_%s_%s.dat", path, prefix, ymd, hms);

	fd = open(buf, O_CREAT | O_WRONLY, 0644);
	if (fd < 0)
		nugu_error("open(%s) failed: %s", buf, strerror(errno));

	nugu_dbg("%s filedump to '%s' (fd=%d)", prefix, buf, fd);

	free(buf);

	return fd;
}
#endif

static const char *why_failed(int why_failed)
{
	switch (why_failed) {
	case SND_PCM_PARAMS_BAD_MODE:
		return ("Bad Mode Parameter");
	case SND_PCM_PARAMS_BAD_START:
		return ("Bad Start Parameter");
	case SND_PCM_PARAMS_BAD_STOP:
		return ("Bad Stop Parameter");
	case SND_PCM_PARAMS_BAD_FORMAT:
		return ("Bad Format Parameter");
	case SND_PCM_PARAMS_BAD_RATE:
		return ("Bad Rate Parameter");
	case SND_PCM_PARAMS_BAD_VOICES:
		return ("Bad Vocies Parameter");
	case SND_PCM_PARAMS_NO_CHANNEL:
		return ("No Channel Available");
	default:
		return ("Unknown Error");
	}

	return ("No Error");
}

static void *_rec_read_thread(void *data)
{
	qnx_alsa_handle *handle = (qnx_alsa_handle *)data;

	nugu_info("%s:%d!!!", __func__, __LINE__);

	while (1) {
		snd_pcm_channel_params_t pp;
		snd_pcm_channel_status_t status;
		//snd_pcm_event_t event;
		char buf[SAMPLE_SIZE];
		int read = 0;
		int rtn;

		if (handle->stop) {
			nugu_dbg("request to stop thread\n");
			break;
		}

		if (!handle->start) {
			nugu_info("%s:%d!!!", __func__, __LINE__);
			usleep(10 * 1000);
			continue;
		}

		memset(buf, 0, SAMPLE_SIZE);
		read = snd_pcm_plugin_read(handle->pcm_handle, buf, SAMPLE_SIZE);
		nugu_dbg("=> read (%d/%d)\n", read, SAMPLE_SIZE);
		if (read > 0)
			nugu_recorder_push_frame(handle->rec, buf, read);

#ifdef ENV_DUMP_PATH_RECORDER
	if (handle->dump_fd != -1) {
		if (write(handle->dump_fd, buf, read) < 0)
			nugu_error("write to fd-%d failed", handle->dump_fd);
	}
#endif

#if 0
		if ((rtn = snd_pcm_channel_read_event(alsa_handle->pcm_handle,
						      SND_PCM_CHANNEL_CAPTURE,
						      &event)) == EOK) {
			switch (event.type) {
			case SND_PCM_EVENT_OVERRUN:
				printf("Overrun event received\n");
				break;
			default:
				printf("Unknown PCM event type for capture - %d\n",
				       event.type);
				break;
			}
		} else
			printf("snd_pcm_channel_read_event() failed with %d(%s)\n",
			       rtn, strerror(errno));
#endif

		if (read == SAMPLE_SIZE)
			continue;

		memset(&status, 0, sizeof(status));
		status.channel = SND_PCM_CHANNEL_CAPTURE;
		if ((rtn = snd_pcm_plugin_status(handle->pcm_handle, &status)) < 0) {
			fprintf(stderr, "Capture channel status error - %s\n", snd_strerror(rtn));
			break;
		}

		if (status.status == SND_PCM_STATUS_CHANGE ||
		    status.status == SND_PCM_STATUS_READY ||
		    status.status == SND_PCM_STATUS_OVERRUN) {
			if (status.status == SND_PCM_STATUS_CHANGE) {
				fprintf(stderr,
					"change: capture channel capability change\n");
				if (snd_pcm_plugin_params(handle->pcm_handle, &pp) <
				    0) {
					fprintf(stderr,
						"Capture channel snd_pcm_plugin_params error\n");
					//cleanup_and_exit(EXIT_FAILURE);
					break;
				}
			}
			if (status.status == SND_PCM_STATUS_OVERRUN) {
				fprintf(stderr, "overrun: capture channel\n");
			}
			if (snd_pcm_plugin_prepare(
				    handle->pcm_handle, SND_PCM_CHANNEL_CAPTURE) < 0) {
				fprintf(stderr,
					"Capture channel prepare error\n");
				//cleanup_and_exit(EXIT_FAILURE);
				break;
			}
		} else if (status.status == SND_PCM_STATUS_ERROR) {
			fprintf(stderr, "error: capture channel failure\n");
			//cleanup_and_exit(EXIT_FAILURE);
			break;
		} else if (status.status == SND_PCM_STATUS_PREEMPTED) {
			fprintf(stderr, "error: capture channel preempted\n");
			//cleanup_and_exit(EXIT_FAILURE);
			break;
		}
	}

	nugu_dbg("thread is stopped\n");

	return NULL;
}

static int _rec_start(NuguRecorderDriver *driver, NuguRecorder *rec,
		      NuguAudioProperty prop)
{
	qnx_alsa_handle *alsa_handle;
	snd_pcm_filter_t pevent;
	snd_pcm_channel_info_t pi;
	snd_pcm_channel_params_t pp;
	snd_pcm_channel_setup_t setup;
	snd_mixer_group_t group;
	snd_pcm_voice_conversion_t voice_conversion;
	int rate_method = 0;
	int fragsize = SAMPLE_SIZE;
	int card = -1, device = 0;
	const char *env_card;
	const char *env_device;
	int rtn;
	struct {
		snd_pcm_chmap_t map;
		unsigned int pos[32];
	} map;

	nugu_info("%s:%d!!!", __func__, __LINE__);

	alsa_handle = nugu_recorder_get_driver_data(rec);
	if (alsa_handle) {
		nugu_dbg("already start");
		return 0;
	}

	alsa_handle = (qnx_alsa_handle *)g_malloc0(sizeof(qnx_alsa_handle));
	alsa_handle->rec = rec;

	pthread_mutex_init(&alsa_handle->mutex, NULL);
	pthread_create(&alsa_handle->read_thread, NULL, _rec_read_thread,
		       alsa_handle);

	env_card = getenv("NUGU_QNX_CAPTURE_CARD");
	if (env_card)
		card = atoi(env_card);

	env_device = getenv("NUGU_QNX_CAPTURE_DEVICE");
	if (env_device)
		device = atoi(env_device);

	if (card == -1) {
		rtn = snd_pcm_open_preferred(&alsa_handle->pcm_handle, &card,
					     &device, SND_PCM_OPEN_CAPTURE);
		if (rtn < 0) {
			fprintf(stderr, "snd_pcm_open_preferred failed - %s\n",
				snd_strerror(rtn));
			return -1;
		}
	} else {
		printf("QNX capture sound card:evice = [%d:%d]", card, device);

		rtn = snd_pcm_open(&alsa_handle->pcm_handle, card, device,
				   SND_PCM_OPEN_CAPTURE);
		if (rtn < 0) {
			fprintf(stderr, "snd_pcm_open failed - %s\n",
				snd_strerror(rtn));
			return -2;
		}
	}

	/* Enable PCM events */
	pevent.enable = (1 << SND_PCM_EVENT_OVERRUN) |
			(1 << SND_PCM_EVENT_AUDIOMGMT_STATUS);
	snd_pcm_set_filter(alsa_handle->pcm_handle, SND_PCM_CHANNEL_CAPTURE, &pevent);

	memset(&pi, 0, sizeof(pi));
	pi.channel = SND_PCM_CHANNEL_CAPTURE;
	if ((rtn = snd_pcm_plugin_info(alsa_handle->pcm_handle, &pi)) < 0) {
		fprintf(stderr, "snd_pcm_plugin_info failed: %s\n",
			snd_strerror(rtn));
		return -3;
	}

	memset(&pp, 0, sizeof(pp));
	pp.mode = SND_PCM_MODE_BLOCK;
	pp.channel = SND_PCM_CHANNEL_CAPTURE;
	pp.start_mode = SND_PCM_START_DATA;
	pp.stop_mode = SND_PCM_STOP_STOP;
	pp.time = 1;

	pp.buf.block.frag_size = pi.max_fragment_size;
	if (fragsize != -1)
		pp.buf.block.frag_size = fragsize;
	pp.buf.block.frags_max = -1;
	pp.buf.block.frags_min = 1;

	pp.format.interleave = 0;
	pp.format.rate = 16000;
	pp.format.voices = 1;
	pp.format.format = SND_PCM_SFMT_S16_LE;

	if ((rtn = snd_pcm_plugin_set_src_method(alsa_handle->pcm_handle,
						 rate_method)) != rate_method) {
		fprintf(stderr, "Failed to apply rate_method %d, using %d\n",
			rate_method, rtn);
		return -4;
	}

	if ((rtn = snd_pcm_plugin_params(alsa_handle->pcm_handle, &pp)) < 0) {
		fprintf(stderr, "snd_pcm_plugin_params failed: %s - %s\n",
			snd_strerror(rtn), why_failed(pp.why_failed));
		return -5;
	}

	memset(&setup, 0, sizeof(setup));
	memset(&group, 0, sizeof(group));
	setup.channel = SND_PCM_CHANNEL_CAPTURE;
	setup.mixer_gid = &group.gid;
	if ((rtn = snd_pcm_plugin_setup(alsa_handle->pcm_handle, &setup)) < 0) {
		fprintf(stderr, "snd_pcm_plugin_setup failed: %s\n",
			snd_strerror(rtn));
		return -6;
	}
	printf("Format %s \n", snd_pcm_get_format_name(setup.format.format));
	printf("Frag Size %d \n", setup.buf.block.frag_size);
	printf("Total Frags %d \n", setup.buf.block.frags);
	printf("Rate %d \n", setup.format.rate);

#if 1 // debug
	map.map.channels = 32;
	if ((rtn = snd_pcm_query_channel_map(alsa_handle->pcm_handle, &map.map)) ==
	    EOK) {
		int i, j;

		printf("Channel map:");
		if ((rtn = snd_pcm_plugin_get_voice_conversion(
			     alsa_handle->pcm_handle, SND_PCM_CHANNEL_CAPTURE,
			     &voice_conversion)) != EOK) {
			// The hardware map is the same as the voice map
			for (i = 0; i < (int)map.map.channels; i++) {
				printf(" (%d)", map.map.pos[i]);
			}
			printf("\n");
		} else {
			// Map hardware channels according to the voice map
			for (i = 0; i < (int)voice_conversion.app_voices; i++) {
				bool printed = false;
				printf(" (");
				for (j = 0; j < (int)voice_conversion.hw_voices;
				     j++) {
					if (voice_conversion.matrix[i] &
					    (1 << j)) {
						if (printed) {
							printf(" ");
						} else {
							printed = true;
						}
						printf("%d", map.map.pos[j]);
					}
				}
				printf(")");
			}
			printf("\n");
		}
	}
#endif

	if ((rtn = snd_pcm_plugin_prepare(alsa_handle->pcm_handle,
					  SND_PCM_CHANNEL_CAPTURE)) < 0) {
		fprintf(stderr, "snd_pcm_plugin_prepare failed: %s\n",
			snd_strerror(rtn));
		return -7;
	}

	pthread_mutex_lock(&alsa_handle->mutex);

	alsa_handle->start = 1;

	pthread_mutex_unlock(&alsa_handle->mutex);

#ifdef ENV_DUMP_PATH_RECORDER
	alsa_handle->dump_fd =
		_dumpfile_open(ENV_DUMP_PATH_RECORDER, "parec");
#endif

	nugu_recorder_set_driver_data(rec, alsa_handle);

	nugu_dbg("start done");
	return 0;
}

static int _rec_stop(NuguRecorderDriver *driver, NuguRecorder *rec)
{
	qnx_alsa_handle *alsa_handle = nugu_recorder_get_driver_data(rec);

	if (alsa_handle == NULL) {
		nugu_dbg("already stop");
		return 0;
	}

	nugu_info("%s:%d!!!", __func__, __LINE__);

	pthread_mutex_lock(&alsa_handle->mutex);

	if (alsa_handle->start && !alsa_handle->stop) {
		void *retval;

		alsa_handle->stop = 1;
		alsa_handle->start = 0;

		pthread_join(alsa_handle->read_thread, &retval);
	}

	pthread_mutex_unlock(&alsa_handle->mutex);

	snd_pcm_plugin_flush(alsa_handle->pcm_handle, SND_PCM_CHANNEL_CAPTURE);

	if (alsa_handle->mixer_handle)
		snd_mixer_close(alsa_handle->mixer_handle);

	if (alsa_handle->pcm_handle)
		snd_pcm_close(alsa_handle->pcm_handle);

#ifdef ENV_DUMP_PATH_RECORDER
	if (alsa_handle->dump_fd >= 0) {
		close(alsa_handle->dump_fd);
		alsa_handle->dump_fd = -1;
	}
#endif

	memset(alsa_handle, 0, sizeof(qnx_alsa_handle));
	g_free(alsa_handle);
	alsa_handle = NULL;

	nugu_recorder_set_driver_data(rec, NULL);

	nugu_dbg("stop done");

	return 0;
}

static struct nugu_recorder_driver_ops rec_ops = {
	/* nugu_recorder_driver */
	.start = _rec_start, /* nugu_recorder_start() */
	.stop = _rec_stop /* nugu_recorder_stop() */
};

static int init(NuguPlugin *p)
{
	nugu_dbg("'%s' plugin initialized",
		 nugu_plugin_get_description(p)->name);

	rec_driver = nugu_recorder_driver_new(PLUGIN_DRIVER_NAME, &rec_ops);
	if (!rec_driver) {
		nugu_error("nugu_recorder_driver_new() failed");
		return -1;
	}

	if (nugu_recorder_driver_register(rec_driver) != 0) {
		nugu_recorder_driver_free(rec_driver);
		rec_driver = NULL;
		return -1;
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

	if (rec_driver) {
		nugu_recorder_driver_remove(rec_driver);
		nugu_recorder_driver_free(rec_driver);
		rec_driver = NULL;
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
