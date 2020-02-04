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

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "base/nugu_log.h"
#include "base/nugu_event.h"
#include "base/nugu_uuid.h"

#include "http2/http2_network.h"
#include "http2/v1_directives.h"
#include "http2/v1_event.h"
#include "http2/v1_event_attachment.h"
#include "http2/v1_ping.h"
#include "http2/v1_policies.h"
#include "http2/v1_events.h"

#include "dg_server.h"

struct _dg_server {
	HTTP2Network *net;
	V1Directives *directives;
	V1Ping *ping;

	int retry_count;
	gchar *host;
	NuguNetworkServerPolicy policy;
	enum dg_server_type type;

	int use_events;
	GHashTable *pending_events;
};

DGServer *dg_server_new(const NuguNetworkServerPolicy *policy)
{
	struct _dg_server *server;
	char *tmp;

	g_return_val_if_fail(policy != NULL, NULL);

	switch (policy->protocol) {
	case NUGU_NETWORK_PROTOCOL_H2:
		tmp = g_strdup_printf("https://%s:%d", policy->hostname,
				      policy->port);
		break;
	case NUGU_NETWORK_PROTOCOL_H2C:
		tmp = g_strdup_printf("http://%s:%d", policy->hostname,
				      policy->port);
		break;
	default:
		nugu_error("not supported protocol: %d", policy->protocol);
		return NULL;
	}

	nugu_dbg("server address: %s", tmp);

	server = calloc(1, sizeof(struct _dg_server));
	if (!server) {
		error_nomem();
		g_free(tmp);
		return NULL;
	}

	server->net = http2_network_new();
	if (!server->net) {
		free(server);
		g_free(tmp);
		return NULL;
	}

	memcpy(&(server->policy), policy, sizeof(NuguNetworkServerPolicy));
	server->host = tmp;
	server->type = DG_SERVER_TYPE_NORMAL;
	server->retry_count = 0;

#ifdef NUGU_ENV_NETWORK_USE_EVENTS
	tmp = getenv(NUGU_ENV_NETWORK_USE_EVENTS);
	if (tmp) {
		if (tmp[0] == '1') {
			nugu_dbg("use /v1/events (multipart)");
			server->use_events = 1;
			server->pending_events = g_hash_table_new_full(
				g_str_hash, g_str_equal, g_free,
				(GDestroyNotify)v1_events_free);
		}
	}
#endif

	http2_network_set_token(server->net, nugu_network_manager_peek_token());
	http2_network_enable_curl_log(server->net);
	http2_network_start(server->net);

	return server;
}

void dg_server_free(DGServer *server)
{
	g_return_if_fail(server != NULL);

	g_free(server->host);

	if (server->pending_events)
		g_hash_table_remove_all(server->pending_events);

	if (server->directives)
		v1_directives_free(server->directives);

	if (server->ping)
		v1_ping_free(server->ping);

	http2_network_free(server->net);

	memset(server, 0, sizeof(struct _dg_server));
	free(server);
}

int dg_server_set_type(DGServer *server, enum dg_server_type type)
{
	g_return_val_if_fail(server != NULL, -1);

	server->type = type;

	return 0;
}

enum dg_server_type dg_server_get_type(DGServer *server)
{
	g_return_val_if_fail(server != NULL, DG_SERVER_TYPE_NORMAL);

	return server->type;
}

int dg_server_connect_async(DGServer *server)
{
	int ret;

	g_return_val_if_fail(server != NULL, -1);

	if (server->directives)
		v1_directives_free(server->directives);

	server->directives = v1_directives_new(
		server->host, server->policy.connection_timeout_ms / 1000);

	ret = v1_directives_establish(server->directives, server->net);
	if (ret < 0) {
		v1_directives_free(server->directives);
		server->directives = NULL;
	}

	return ret;
}

int dg_server_start_health_check(DGServer *server,
				 const struct dg_health_check_policy *policy)
{
	g_return_val_if_fail(server != NULL, -1);
	g_return_val_if_fail(policy != NULL, -1);

	if (server->ping)
		v1_ping_free(server->ping);

	server->ping = v1_ping_new(server->host, policy);
	v1_ping_establish(server->ping, server->net);

	return 0;
}

unsigned int dg_server_get_retry_count(DGServer *server)
{
	g_return_val_if_fail(server != NULL, -1);

	return server->retry_count;
}

unsigned int dg_server_get_retry_count_limit(DGServer *server)
{
	g_return_val_if_fail(server != NULL, -1);

	return server->policy.retry_count_limit;
}

int dg_server_is_retry_over(DGServer *server)
{
	g_return_val_if_fail(server != NULL, -1);

	if (server->retry_count >= server->policy.retry_count_limit)
		return 1;

	return 0;
}

void dg_server_increse_retry_count(DGServer *server)
{
	g_return_if_fail(server != NULL);

	server->retry_count++;
}

void dg_server_reset_retry_count(DGServer *server)
{
	g_return_if_fail(server != NULL);

	server->retry_count = 0;
}

static int _send_event(DGServer *server, NuguEvent *nev, const char *payload)
{
	V1Event *e;

	e = v1_event_new(server->host);
	if (!e)
		return -1;

	v1_event_set_json(e, payload, strlen(payload));
	v1_event_send_with_free(e, server->net);

	return 0;
}

static int _send_events(DGServer *server, NuguEvent *nev, const char *payload)
{
	V1Events *e;

	e = v1_events_new(server->host, server->net);
	if (!e)
		return -1;

	v1_events_send_json(e, payload, strlen(payload));

	if (nugu_event_get_type(nev) == NUGU_EVENT_TYPE_DEFAULT) {
		v1_events_send_done(e);
		v1_events_free(e);
		return 0;
	}

	if (server->pending_events == NULL) {
		nugu_error("pending_events is NULL");
		return -1;
	}

	/* add to pending list for use in sending attachments */
	g_hash_table_insert(server->pending_events,
			    g_strdup(nugu_event_peek_msg_id(nev)), e);

	return 0;
}

int dg_server_send_event(DGServer *server, NuguEvent *nev)
{
	char *payload;
	int ret;

	payload = nugu_event_generate_payload(nev);
	if (!payload)
		return -1;

	if (server->use_events == 0)
		ret = _send_event(server, nev, payload);
	else
		ret = _send_events(server, nev, payload);

	g_free(payload);

	return ret;
}

static int _send_attachment(DGServer *server, NuguEvent *nev, int is_end,
			    size_t length, unsigned char *data)
{
	char *msg_id;
	V1EventAttachment *ea;

	ea = v1_event_attachment_new(server->host);
	if (!ea)
		return -1;

	msg_id = nugu_uuid_generate_time();
	v1_event_attachment_set_query(ea, nugu_event_peek_namespace(nev),
				      nugu_event_peek_name(nev),
				      nugu_event_peek_version(nev),
				      nugu_event_peek_msg_id(nev), msg_id,
				      nugu_event_peek_dialog_id(nev),
				      nugu_event_get_seq(nev), is_end);
	free(msg_id);

	v1_event_attachment_set_data(ea, data, length);

	return v1_event_attachment_send_with_free(ea, server->net);
}

static int _send_events_attachment(DGServer *server, NuguEvent *nev, int is_end,
				   size_t length, unsigned char *data)
{
	V1Events *e;

	/* find an active event from pending list */
	e = g_hash_table_lookup(server->pending_events,
				nugu_event_peek_msg_id(nev));
	if (!e) {
		nugu_error("invalid attachment (can't find an event)");
		return -1;
	}

	v1_events_send_binary(e, nugu_event_get_seq(nev), is_end, length, data);

	if (is_end == 0)
		return 0;

	v1_events_send_done(e);

	g_hash_table_remove(server->pending_events,
			    nugu_event_peek_msg_id(nev));

	return 0;
}

int dg_server_send_attachment(DGServer *server, NuguEvent *nev, int is_end,
			      size_t length, unsigned char *data)
{
	int ret;

	g_return_val_if_fail(server != NULL, -1);
	g_return_val_if_fail(nev != NULL, -1);

	if (server->use_events == 0)
		ret = _send_attachment(server, nev, is_end, length, data);
	else
		ret = _send_events_attachment(server, nev, is_end, length,
					      data);

	if (ret == 0)
		nugu_event_increase_seq(nev);

	return ret;
}

int dg_server_force_close_event(DGServer *server, NuguEvent *nev)
{
	V1Events *e;

	g_return_val_if_fail(server != NULL, -1);
	g_return_val_if_fail(nev != NULL, -1);

	if (server->use_events == 0) {
		nugu_error("force_close is not supported");
		return -1;
	}

	e = g_hash_table_lookup(server->pending_events,
				nugu_event_peek_msg_id(nev));
	if (!e) {
		nugu_error("can't find an event from pending list");
		return -1;
	}

	v1_events_send_done(e);

	g_hash_table_remove(server->pending_events,
			    nugu_event_peek_msg_id(nev));

	return 0;
}