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

#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include "base/nugu_log.h"

#include "http2_request.h"
#include "v1_event_attachment.h"

#define QUERY                                                                  \
	"?namespace=%s"                                                        \
	"&name=%s"                                                             \
	"&dialogRequestId=%s"                                                  \
	"&messageId=%s"                                                        \
	"&version=%s"                                                          \
	"&parentMessageId=%s"                                                  \
	"&seq=%d"                                                              \
	"&isEnd=%s"

struct _v1_event_attachment {
	HTTP2Request *req;
	char *host;
	char *msgid;
	int seq;
};

V1EventAttachment *v1_event_attachment_new(const char *host)
{
	struct _v1_event_attachment *attach;

	g_return_val_if_fail(host != NULL, NULL);

	attach = calloc(1, sizeof(struct _v1_event_attachment));
	if (!attach) {
		nugu_error_nomem();
		return NULL;
	}

	attach->req = http2_request_new();
	http2_request_set_method(attach->req, HTTP2_REQUEST_METHOD_POST);
	http2_request_set_content_type(attach->req,
				       HTTP2_REQUEST_CONTENT_TYPE_OCTET, NULL);

	/* Set maximum timeout to 5 seconds */
	http2_request_set_timeout(attach->req, 5);

	attach->host = g_strdup(host);

	return attach;
}

void v1_event_attachment_free(V1EventAttachment *attach)
{
	g_return_if_fail(attach != NULL);

	if (attach->host)
		g_free(attach->host);

	if (attach->msgid)
		g_free(attach->msgid);

	if (attach->req)
		http2_request_unref(attach->req);

	memset(attach, 0, sizeof(V1EventAttachment));
	free(attach);
}

void v1_event_attachment_set_query(V1EventAttachment *attach,
				   const char *name_space, const char *name,
				   const char *version,
				   const char *parent_msg_id,
				   const char *msg_id, const char *dialog_id,
				   int seq, int is_end)
{
	char *tmp;
	const char *endstr = "false";

	if (is_end)
		endstr = "true";

	attach->seq = seq;
	attach->msgid = g_strdup(msg_id);

	tmp = g_strdup_printf("%s/v1/event-attachment" QUERY, attach->host,
			      name_space, name, dialog_id, msg_id, version,
			      parent_msg_id, seq, endstr);
	http2_request_set_url(attach->req, tmp);
	nugu_dbg("attachment req(%p)", attach->req);
	g_free(tmp);
}

int v1_event_attachment_set_data(V1EventAttachment *attach,
				 const unsigned char *data, size_t length)
{
	g_return_val_if_fail(attach != NULL, -1);

	if (data != NULL && length > 0) {
		if (http2_request_add_send_data(attach->req, data, length) < 0)
			return -1;
	}

	return http2_request_close_send_data(attach->req);
}

/* invoked in a thread loop */
static void _on_finish(HTTP2Request *req, void *userdata)
{
	nugu_log_protocol_send(NUGU_LOG_LEVEL_INFO, "EventAttachment\n%s",
			       http2_request_peek_url(req));
}

int v1_event_attachment_send_with_free(V1EventAttachment *attach,
				       HTTP2Network *net)
{
	int ret;

	g_return_val_if_fail(attach != NULL, -1);
	g_return_val_if_fail(net != NULL, -1);

	http2_request_set_finish_callback(attach->req, _on_finish, NULL);

	ret = http2_network_add_request(net, attach->req);
	if (ret < 0)
		return ret;

	http2_request_unref(attach->req);
	attach->req = NULL;

	v1_event_attachment_free(attach);

	return ret;
}
