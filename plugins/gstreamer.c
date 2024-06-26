/*
 * Copyright (c) 2019 SK Telecom Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef NUGU_PLUGIN_BUILTIN_GSTREAMER
#define NUGU_PLUGIN_BUILTIN
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include "base/nugu_log.h"
#include "base/nugu_plugin.h"
#include "base/nugu_player.h"

#define GST_SET_VOLUME_MIN 0
#define GST_SET_VOLUME_MAX 1

#define NOTIFY_STATUS_CHANGED(s)                                               \
	{                                                                      \
		if (gh->player && gh->status != s) {                           \
			nugu_player_emit_status(gh->player, s);                \
			gh->status = s;                                        \
		}                                                              \
	}

#define NOTIFY_EVENT(e)                                                        \
	{                                                                      \
		if (gh->player)                                                \
			nugu_player_emit_event(gh->player, e);                 \
	}

typedef struct gst_handle GstreamerHandle;

struct gst_handle {
	GstElement *pipeline;
	GstElement *audio_src;
	GstElement *audio_convert;
	GstElement *audio_sink;
	GstElement *caps_filter;
	GstElement *volume;
	GstDiscoverer *discoverer;
	GstState last_state;
	gboolean is_buffering;
	gboolean is_live;
	gboolean is_file;
	gboolean seekable;
	gboolean seek_done;
	int seek_reserve;
	int duration;
	gulong sig_msg_hid;
	char *playurl;
	NuguPlayer *player;
	enum nugu_media_status status;
};

static NuguPlayerDriver *driver;
static int _uniq_id;

#ifdef ENABLE_GSTREAMER_PLUGIN_VOLUME
static const gdouble VOLUME_ZERO = 0.0000001;
#endif

#ifdef ENABLE_PULSEAUDIO
static void _set_audio_attribute(GstreamerHandle *gh, NuguPlayer *player)
{
	int attr = nugu_player_get_audio_attribute(player);
	const char *media_role;
	GstStructure *s;

	if (attr == -1)
		attr = NUGU_AUDIO_ATTRIBUTE_MUSIC;

	media_role = nugu_audio_get_attribute_str(attr);

	s = gst_structure_new("properties", "media.role", G_TYPE_STRING,
			      media_role, NULL);

	nugu_dbg("media.role => %s", media_role);

	g_object_set(G_OBJECT(gh->audio_sink), "stream-properties", s, NULL);
	gst_structure_free(s);
}
#endif

static int _seek_action(GstreamerHandle *gh, int sec)
{
	if (!gst_element_seek_simple(gh->pipeline, GST_FORMAT_TIME,
				     (GstSeekFlags)(GST_SEEK_FLAG_FLUSH |
						    GST_SEEK_FLAG_KEY_UNIT),
				     sec * GST_SECOND)) {
		nugu_error("seek is failed. status: %d", gh->status);
		return -1;
	}

	nugu_dbg("seek is success. status: %d", gh->status);
	return 0;
}

static void _discovered_cb(GstDiscoverer *discoverer, GstDiscovererInfo *info,
			   GError *err, GstreamerHandle *gh)
{
	const gchar *discoverer_uri = NULL;
	GstDiscovererResult discoverer_result;

	discoverer_uri = gst_discoverer_info_get_uri(info);
	discoverer_result = gst_discoverer_info_get_result(info);

	switch (discoverer_result) {
	case GST_DISCOVERER_URI_INVALID:
		nugu_dbg("Invalid URI '%s'", discoverer_uri);
		break;
	case GST_DISCOVERER_ERROR:
		nugu_dbg("Discoverer error: %s", err->message);
		break;
	case GST_DISCOVERER_TIMEOUT:
		nugu_dbg("Timeout");
		break;
	case GST_DISCOVERER_BUSY:
		nugu_dbg("Busy");
		break;
	case GST_DISCOVERER_MISSING_PLUGINS: {
		const GstStructure *s;
		gchar *str;

		s = gst_discoverer_info_get_misc(info);
		str = gst_structure_to_string(s);

		nugu_dbg("Missing plugins: %s", str);
		g_free(str);
		break;
	}
	case GST_DISCOVERER_OK:
		nugu_dbg("Discovered '%s'", discoverer_uri);
		break;

	default:
		break;
	}

	if (discoverer_result != GST_DISCOVERER_OK) {
		if (gh->status == NUGU_MEDIA_STATUS_PLAYING) {
			nugu_dbg("Gstreamer is already played!!");
			return;
		}

		if (gh->last_state == GST_STATE_READY) {
			nugu_dbg("Gstreamer is not ready gst_state(%d)",
				 gh->last_state);
			return;
		}

		if (discoverer_result == GST_DISCOVERER_URI_INVALID)
			NOTIFY_EVENT(NUGU_MEDIA_EVENT_MEDIA_INVALID)
		else
			NOTIFY_EVENT(NUGU_MEDIA_EVENT_MEDIA_LOAD_FAILED)

		nugu_dbg("This URI cannot be played");
		gst_element_set_state(gh->pipeline, GST_STATE_NULL);
		return;
	}

	gh->duration =
		GST_TIME_AS_SECONDS(gst_discoverer_info_get_duration(info));
	gh->seekable = gst_discoverer_info_get_seekable(info);

	nugu_dbg("duration: %d, seekable: %d", gh->duration, gh->seekable);

	nugu_dbg("[gstreamer]Duration: %" GST_TIME_FORMAT "",
		 GST_TIME_ARGS(gst_discoverer_info_get_duration(info)));
}

static void _cb_message(GstBus *bus, GstMessage *msg, GstreamerHandle *gh)
{
	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_ERROR: {
		GError *err;
		gchar *debug;

		gst_message_parse_error(msg, &err, &debug);
		nugu_dbg("GST_MESSAGE_ERROR: %s, gst_state: %d", err->message,
			 gh->last_state);

		if (gh->last_state == GST_STATE_READY) {
			NOTIFY_EVENT(NUGU_MEDIA_EVENT_MEDIA_LOAD_FAILED);
			NOTIFY_STATUS_CHANGED(NUGU_MEDIA_STATUS_STOPPED);
		}

		g_error_free(err);
		g_free(debug);

		gst_element_set_state(gh->pipeline, GST_STATE_NULL);
		break;
	}
	case GST_MESSAGE_STATE_CHANGED: {
		GstState old_state, new_state, pending_state;

		gst_message_parse_state_changed(msg, &old_state, &new_state,
						&pending_state);

		if (GST_MESSAGE_SRC(msg) != GST_OBJECT(gh->pipeline))
			break;

		gh->last_state = new_state;
		nugu_dbg("State set to %s -> %s",
			 gst_element_state_get_name(old_state),
			 gst_element_state_get_name(new_state));

		if (new_state != GST_STATE_PLAYING)
			break;

		if (gh->status == NUGU_MEDIA_STATUS_PAUSED && gh->seek_done) {
			nugu_dbg("player is paused...");
			gst_element_set_state(gh->pipeline, GST_STATE_PAUSED);
			gh->seek_done = FALSE;
			break;
		}

		if (gh->seek_reserve) {
			nugu_dbg("seek(%d sec) action", gh->seek_reserve);
			_seek_action(gh, gh->seek_reserve);
			gh->seek_reserve = 0;
		}

		if (gh->status != NUGU_MEDIA_STATUS_PAUSED)
			NOTIFY_STATUS_CHANGED(NUGU_MEDIA_STATUS_PLAYING);
		break;
	}
	case GST_MESSAGE_EOS: {
		gint64 current;

		nugu_dbg("GST_MESSAGE_EOS");

		if (gst_element_query_position(gh->pipeline, GST_FORMAT_TIME,
					       &current)) {
			int last_position;

			last_position = GST_TIME_AS_SECONDS(current);
			nugu_dbg("last position => %d sec", last_position);
			nugu_player_set_position(gh->player, last_position);
		}

		gst_element_set_state(gh->pipeline, GST_STATE_READY);
		NOTIFY_EVENT(NUGU_MEDIA_EVENT_END_OF_STREAM);
		NOTIFY_STATUS_CHANGED(NUGU_MEDIA_STATUS_STOPPED);
		break;
	}

	case GST_MESSAGE_BUFFERING: {
		gint percent = 0;

		if (gh->is_live)
			break;

		gst_message_parse_buffering(msg, &percent);
		nugu_dbg("Buffering... (%3d%%), state: %d", percent,
			 gh->last_state);

		if (percent == 100) {
			gh->is_buffering = FALSE;
			nugu_dbg("Buffering Done!! (%3d%%)", percent);
			if (gh->status == NUGU_MEDIA_STATUS_PAUSED) {
				nugu_dbg("player is paused...");
				break;
			}
			gst_element_set_state(gh->pipeline, GST_STATE_PLAYING);
		} else {
			if (!gh->is_buffering &&
			    gh->last_state == GST_STATE_PLAYING) {
				nugu_dbg("Buffering Pause!! (%3d%%)", percent);
				gst_element_set_state(gh->pipeline,
						      GST_STATE_PAUSED);
				gh->is_buffering = TRUE;

				NOTIFY_EVENT(NUGU_MEDIA_EVENT_MEDIA_UNDERRUN);
			}
		}
		break;
	}

	case GST_MESSAGE_CLOCK_LOST:
		nugu_dbg("GST_MESSAGE_CLOCK_LOST");
		/* Get a new clock */
		gst_element_set_state(gh->pipeline, GST_STATE_PAUSED);
		gst_element_set_state(gh->pipeline, GST_STATE_PLAYING);
		break;

	case GST_MESSAGE_STREAM_START:
		nugu_dbg("GST_MESSAGE_STREAM_START");
		if (gh->status == NUGU_MEDIA_STATUS_STOPPED)
			NOTIFY_STATUS_CHANGED(NUGU_MEDIA_STATUS_READY);

		NOTIFY_EVENT(NUGU_MEDIA_EVENT_MEDIA_LOADED);
		break;

	default:
		break;
	}
}

static void _connect_message_to_pipeline(GstreamerHandle *gh)
{
	GstBus *bus = gst_element_get_bus(gh->pipeline);

	gst_bus_add_signal_watch(bus);
	gh->sig_msg_hid = g_signal_connect(G_OBJECT(bus), "message",
					   G_CALLBACK(_cb_message), gh);

	gst_object_unref(bus);
}

static void _disconnect_message_to_pipeline(GstreamerHandle *gh)
{
	GstBus *bus = gst_element_get_bus(gh->pipeline);

	if (gh->sig_msg_hid == 0)
		return;

	g_signal_handler_disconnect(G_OBJECT(bus), gh->sig_msg_hid);
	gh->sig_msg_hid = 0;

	gst_object_unref(bus);
}

static void _pad_added_handler(GstElement *src, GstPad *new_src_pad,
			       GstreamerHandle *gh)
{
	GstPad *sink_pad =
		gst_element_get_static_pad(gh->audio_convert, "sink");

	nugu_dbg("Received new source pad '%s' <--- '%s':",
		 GST_PAD_NAME(new_src_pad), GST_ELEMENT_NAME(src));

	if (gst_pad_is_linked(sink_pad)) {
		nugu_error("We are already linked. Ignoring.");
	} else {
		GstCaps *new_pad_caps = NULL;
		const gchar *new_pad_type = NULL;
		GstPadLinkReturn ret;

		/* Check the new pad's type */
		new_pad_caps = gst_pad_get_current_caps(new_src_pad);
		new_pad_type = gst_structure_get_name(
			gst_caps_get_structure(new_pad_caps, 0));

		if (!g_str_has_prefix(new_pad_type, "audio/x-raw")) {
			nugu_error("It has type '%s' which is not raw audio.",
				   new_pad_type);

			if (new_pad_caps != NULL)
				gst_caps_unref(new_pad_caps);

			gst_object_unref(sink_pad);
			return;
		}

		/* Attempt the link */
		ret = gst_pad_link(new_src_pad, sink_pad);
		if (GST_PAD_LINK_FAILED(ret))
			nugu_error("Type is '%s' but link failed.",
				   new_pad_type);
		else
			nugu_dbg("Link succeeded (type '%s').", new_pad_type);
	}
	gst_object_unref(sink_pad);
}

static int _create(NuguPlayerDriver *driver, NuguPlayer *player)
{
	GstreamerHandle *gh;
	char caps_filter[128];
	char audio_source[128];
	char audio_convert[128];
	char audio_sink[128];
	char volume[128];
	char discoverer[128];
	char pipeline[128];
	GError *err;

	g_return_val_if_fail(player != NULL, -1);

	g_snprintf(caps_filter, 128, "caps_filter#%d", _uniq_id);
	g_snprintf(audio_source, 128, "audio_source#%d", _uniq_id);
	g_snprintf(audio_convert, 128, "audio_convert#%d", _uniq_id);
	g_snprintf(audio_sink, 128, "audio_sink#%d", _uniq_id);
	g_snprintf(volume, 128, "volume#%d", _uniq_id);
	g_snprintf(discoverer, 128, "discoverer#%d", _uniq_id);
	g_snprintf(pipeline, 128, "pipeline#%d", _uniq_id);

	gh = (GstreamerHandle *)g_malloc0(sizeof(GstreamerHandle));
	if (!gh) {
		nugu_error_nomem();
		return -1;
	}

	nugu_dbg("create pipeline(%s)", pipeline);
	gh->pipeline = gst_pipeline_new(pipeline);
	if (!gh->pipeline) {
		nugu_error("create pipeline(%s) failed", pipeline);
		goto error_out;
	}

	gh->audio_src = gst_element_factory_make("uridecodebin", audio_source);
	if (!gh->audio_src) {
		nugu_error("create gst_element for 'uridecodebin' failed");
		goto error_out;
	}

	gh->audio_convert =
		gst_element_factory_make("audioconvert", audio_convert);
	if (!gh->audio_convert) {
		nugu_error("create gst_element for 'audioconvert' failed");
		goto error_out;
	}

#if defined(__MSYS__) || defined(_WIN32)
	gh->audio_sink =
		gst_element_factory_make("directsoundsink", audio_sink);
	if (!gh->audio_sink) {
		nugu_error("create gst_element for 'directsoundsink' failed");
		goto error_out;
	}
#else
#ifdef ENABLE_PULSEAUDIO
	gh->audio_sink = gst_element_factory_make("pulsesink", audio_sink);
	if (!gh->audio_sink) {
		nugu_error("create gst_element for 'pulsesink' failed");
		goto error_out;
	}
#else
	gh->audio_sink = gst_element_factory_make("autoaudiosink", audio_sink);
	if (!gh->audio_sink) {
		nugu_error("create gst_element for 'autoaudiosink' failed");
		goto error_out;
	}
#endif
#endif

#ifdef ENABLE_GSTREAMER_PLUGIN_VOLUME
	gh->volume = gst_element_factory_make("volume", volume);
	if (!gh->volume) {
		nugu_error("create gst_element for 'volume' failed");
		goto error_out;
	}
#endif

	gh->discoverer =
		gst_discoverer_new(NUGU_SET_LOADING_TIMEOUT * GST_SECOND, &err);
	if (!gh->discoverer) {
		nugu_error("create gst_discoverer failed");
		goto error_out;
	}

	gh->is_buffering = FALSE;
	gh->is_live = FALSE;
	gh->is_file = FALSE;
	gh->seekable = FALSE;
	gh->seek_done = FALSE;
	gh->seek_reserve = 0;
	gh->duration = -1;
	gh->status = NUGU_MEDIA_STATUS_STOPPED;
	gh->player = player;

#ifdef ENABLE_GSTREAMER_PLUGIN_VOLUME
	gst_bin_add_many(GST_BIN(gh->pipeline), gh->audio_src,
			 gh->audio_convert, gh->volume, gh->audio_sink, NULL);
	gst_element_link_many(gh->audio_convert, gh->volume, gh->audio_sink,
			      NULL);
#else
	gst_bin_add_many(GST_BIN(gh->pipeline), gh->audio_src,
			 gh->audio_convert, gh->audio_sink, NULL);
	gst_element_link_many(gh->audio_convert, gh->audio_sink, NULL);
#endif
	_connect_message_to_pipeline(gh);

	/* Connect to the pad-added signal */
	g_signal_connect(gh->audio_src, "pad-added",
			 G_CALLBACK(_pad_added_handler), gh);

	/* Connect to the discoverer signal */
	g_signal_connect(gh->discoverer, "discovered",
			 G_CALLBACK(_discovered_cb), gh);

	nugu_player_set_driver_data(player, gh);

	_uniq_id++;
	return 0;

error_out:
	if (!gh)
		return -1;

	if (gh->audio_src)
		g_object_unref(gh->audio_src);
	if (gh->audio_convert)
		g_object_unref(gh->audio_convert);
	if (gh->audio_sink)
		g_object_unref(gh->audio_sink);
	if (gh->volume)
		g_object_unref(gh->volume);

	if (gh->discoverer)
		g_object_unref(gh->discoverer);
	if (gh->pipeline)
		g_object_unref(gh->pipeline);

	memset(gh, 0, sizeof(GstreamerHandle));
	g_free(gh);

	return -1;
}

static int _start(NuguPlayerDriver *driver, NuguPlayer *player)
{
	GstreamerHandle *gh;
	GstStateChangeReturn ret;

	g_return_val_if_fail(player != NULL, -1);

	gh = nugu_player_get_driver_data(player);
	if (!gh) {
		nugu_error("invalid player (no driver data)");
		return -1;
	}

#ifdef ENABLE_PULSEAUDIO
	_set_audio_attribute(gh, player);
#endif

	if (!gh->is_file) {
		/* Start the discoverer process (nothing to do yet) */
		gst_discoverer_stop(gh->discoverer);
		gst_discoverer_start(gh->discoverer);

		/* Add a request to process asynchronously the URI passed
		 * through the command line
		 */
		if (!gst_discoverer_discover_uri_async(gh->discoverer,
						       gh->playurl))
			nugu_dbg("Failed to start discovering URI '%s'",
				 gh->playurl);
	}

	ret = gst_element_set_state(gh->pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		nugu_dbg("unable to set the pipeline to the playing state.");
		return -1;
	} else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
		nugu_dbg("the media is live!!");
		gh->is_live = TRUE;
	}

	return 0;
}

static int _stop(NuguPlayerDriver *driver, NuguPlayer *player)
{
	GstreamerHandle *gh;
	GstStateChangeReturn ret;

	g_return_val_if_fail(player != NULL, -1);

	gh = nugu_player_get_driver_data(player);
	if (!gh) {
		nugu_error("invalid player (no driver data)");
		return -1;
	}

	gh->is_buffering = FALSE;
	gh->is_live = FALSE;

	if (gh->pipeline == NULL)
		nugu_dbg("pipeline is not valid!!");
	else {
		if (!gh->is_file)
			gst_discoverer_stop(gh->discoverer);

		ret = gst_element_set_state(gh->pipeline, GST_STATE_NULL);
		if (ret == GST_STATE_CHANGE_FAILURE) {
			nugu_error("failed to set state stop to pipeline.");
			return -1;
		}
	}

	NOTIFY_STATUS_CHANGED(NUGU_MEDIA_STATUS_STOPPED);

	return 0;
}

static int _set_source(NuguPlayerDriver *driver, NuguPlayer *player,
		       const char *url)
{
	GstreamerHandle *gh;

	g_return_val_if_fail(player != NULL, -1);
	g_return_val_if_fail(url != NULL, -1);

	gh = nugu_player_get_driver_data(player);
	if (!gh) {
		nugu_error("invalid player (no driver data)");
		return -1;
	}

	gh->is_buffering = FALSE;
	gh->is_live = FALSE;
	gh->is_file = FALSE;
	gh->seekable = FALSE;
	gh->seek_done = FALSE;
	gh->seek_reserve = 0;
	gh->duration = -1;

	if (gh->playurl)
		g_free(gh->playurl);

	gh->playurl = g_malloc0(strlen(url) + 1);
	memcpy(gh->playurl, url, strlen(url));
	nugu_dbg("playurl:%s", gh->playurl);

	if (!strncmp("file://", gh->playurl, 7))
		gh->is_file = TRUE;

	nugu_player_emit_event(player, NUGU_MEDIA_EVENT_MEDIA_SOURCE_CHANGED);
	_stop(driver, player);

	g_object_set(gh->audio_src, "uri", gh->playurl, NULL);

	return 0;
}

static void _destroy(NuguPlayerDriver *driver, NuguPlayer *player)
{
	GstreamerHandle *gh;

	g_return_if_fail(player != NULL);

	gh = nugu_player_get_driver_data(player);
	if (!gh) {
		nugu_error("invalid player (no driver data)");
		return;
	}

	_disconnect_message_to_pipeline(gh);

	if (player == gh->player)
		_stop(driver, gh->player);

	if (gh->playurl)
		g_free(gh->playurl);

	if (gh->discoverer)
		g_object_unref(gh->discoverer);

	if (gh->pipeline)
		g_object_unref(gh->pipeline);

	memset(gh, 0, sizeof(GstreamerHandle));
	g_free(gh);
}

static int _pause(NuguPlayerDriver *driver, NuguPlayer *player)
{
	GstreamerHandle *gh;
	GstStateChangeReturn ret;

	g_return_val_if_fail(player != NULL, -1);

	gh = nugu_player_get_driver_data(player);
	if (!gh) {
		nugu_error("invalid player (no driver data)");
		return -1;
	}

	if (gh->pipeline == NULL) {
		nugu_error("pipeline is not valid!!");
		return -1;
	}

	ret = gst_element_set_state(gh->pipeline, GST_STATE_PAUSED);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		nugu_error("unable to set the pipeline to the pause state.");
		return -1;
	}

	NOTIFY_STATUS_CHANGED(NUGU_MEDIA_STATUS_PAUSED);

	return 0;
}

static int _resume(NuguPlayerDriver *driver, NuguPlayer *player)
{
	GstreamerHandle *gh;
	GstStateChangeReturn ret;

	g_return_val_if_fail(player != NULL, -1);

	gh = nugu_player_get_driver_data(player);
	if (!gh) {
		nugu_error("invalid player (no driver data)");
		return -1;
	}

	if (gh->pipeline == NULL) {
		nugu_error("pipeline is not valid!!");
		return -1;
	}

	ret = gst_element_set_state(gh->pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		nugu_error("unable to set the pipeline to the resume state.");
		return -1;
	}

	NOTIFY_STATUS_CHANGED(NUGU_MEDIA_STATUS_PLAYING);

	return 0;
}

static int _seek(NuguPlayerDriver *driver, NuguPlayer *player, int sec)
{
	GstreamerHandle *gh;

	g_return_val_if_fail(player != NULL, -1);

	gh = nugu_player_get_driver_data(player);
	if (!gh) {
		nugu_error("invalid player (no driver data)");
		return -1;
	}

	gh->is_buffering = FALSE;

	if (gh->pipeline == NULL) {
		nugu_error("pipeline is not valid!!");
		return -1;
	}

	if (gh->status == NUGU_MEDIA_STATUS_READY ||
	    gh->status == NUGU_MEDIA_STATUS_STOPPED) {
		/* wait to play & seek */
		nugu_dbg("seek is reserved (%d sec)", sec);
		gh->seek_reserve = sec;
	} else {
		if (!gh->seekable) {
			nugu_dbg("this media is not supported seek function");
			return -1;
		}

		if (_seek_action(gh, sec) != 0)
			return -1;

		/* seek & pause */
		gh->seek_done = TRUE;
	}

	return 0;
}

static int _set_volume(NuguPlayerDriver *driver, NuguPlayer *player, int vol)
{
#ifdef ENABLE_GSTREAMER_PLUGIN_VOLUME
	GstreamerHandle *gh;
	gdouble volume;

	g_return_val_if_fail(player != NULL, -1);
	g_return_val_if_fail(vol >= 0, -1);

	gh = nugu_player_get_driver_data(player);
	if (!gh) {
		nugu_error("invalid player (no driver data)");
		return -1;
	}

	volume = (gdouble)vol * (GST_SET_VOLUME_MAX - GST_SET_VOLUME_MIN) /
		 (NUGU_SET_VOLUME_MAX - NUGU_SET_VOLUME_MIN);

	nugu_dbg("volume: %f", volume);

	if (volume == 0)
		g_object_set(gh->volume, "volume", VOLUME_ZERO, NULL);
	else
		g_object_set(gh->volume, "volume", volume, NULL);
#endif
	return 0;
}

static int _get_duration(NuguPlayerDriver *driver, NuguPlayer *player)
{
	GstreamerHandle *gh;
	gint64 duration;

	g_return_val_if_fail(player != NULL, -1);

	gh = nugu_player_get_driver_data(player);
	if (!gh) {
		nugu_error("invalid player (no driver data)");
		return -1;
	}

	if (gh->duration <= 0) {
		/* Query the current position of the stream */
		if (!gst_element_query_duration(gh->pipeline, GST_FORMAT_TIME,
						&duration)) {
			nugu_error("Could not query duration!!");
			return -1;
		}
		gh->duration = GST_TIME_AS_SECONDS(duration);
	}

	return gh->duration;
}

static int _get_position(NuguPlayerDriver *driver, NuguPlayer *player)
{
	GstreamerHandle *gh;
	gint64 current;

	g_return_val_if_fail(player != NULL, -1);

	gh = nugu_player_get_driver_data(player);
	if (!gh) {
		nugu_error("invalid player (no driver data)");
		return -1;
	}

	if (gh->pipeline == NULL) {
		nugu_error("pipeline is not valid!!");
		return -1;
	}

	/* Query the current position of the stream */
	if (!gst_element_query_position(gh->pipeline, GST_FORMAT_TIME,
					&current)) {
		nugu_error("Could not query current position!!");
		return -1;
	}
	return GST_TIME_AS_SECONDS(current);
}

static struct nugu_player_driver_ops player_ops = {
	.create = _create,
	.destroy = _destroy,
	.set_source = _set_source,
	.start = _start,
	.stop = _stop,
	.pause = _pause,
	.resume = _resume,
	.seek = _seek,
	.set_volume = _set_volume,
	.get_duration = _get_duration,
	.get_position = _get_position
};

static int init(NuguPlugin *p)
{
	const struct nugu_plugin_desc *desc;

	desc = nugu_plugin_get_description(p);
	nugu_dbg("plugin-init '%s'", desc->name);

	if (gst_is_initialized() == FALSE)
		gst_init(NULL, NULL);

	driver = nugu_player_driver_new(desc->name, &player_ops);
	if (!driver) {
		nugu_error("nugu_player_driver_new() failed");
		return -1;
	}

	if (nugu_player_driver_register(driver) != 0) {
		nugu_player_driver_free(driver);
		driver = NULL;
		return -1;
	}

	nugu_dbg("'%s' plugin initialized", desc->name);

	return 0;
}

static int load(void)
{
	nugu_dbg("plugin loaded");
	return 0;
}

static void unload(NuguPlugin *p)
{
	nugu_dbg("plugin-unload '%s'", nugu_plugin_get_description(p)->name);

	if (driver) {
		nugu_player_driver_remove(driver);
		nugu_player_driver_free(driver);
		driver = NULL;
	}

	nugu_dbg("'%s' plugin unloaded", nugu_plugin_get_description(p)->name);
}

NUGU_PLUGIN_DEFINE(gstreamer,
	NUGU_PLUGIN_PRIORITY_DEFAULT,
	"0.0.1",
	load,
	unload,
	init);
