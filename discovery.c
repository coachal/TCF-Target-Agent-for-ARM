/*******************************************************************************
 * Copyright (c) 2007, 2009 Wind River Systems, Inc. and others.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 * The Eclipse Public License is available at
 * http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 * http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *     Wind River Systems - initial API and implementation
 *******************************************************************************/

/*
 * Implements auto-discovery and Locator service.
 */

#include "config.h"

#include <stddef.h>
#include <errno.h>
#include <assert.h>
#include "discovery.h"
#include "discovery_udp.h"
#include "protocol.h"
#include "channel.h"
#include "proxy.h"
#include "myalloc.h"
#include "events.h"
#include "trace.h"
#include "exceptions.h"
#include "json.h"
#include "peer.h"

#if SERVICE_Locator

static const char * LOCATOR = "Locator";
static int peer_cnt = 0;

static int write_peer_properties(PeerServer * ps, void * arg) {
    int i;
    OutputStream * out = (OutputStream *)arg;

    if (peer_cnt > 0) write_stream(out, ',');
    write_stream(out, '{');
    json_write_string(out, "ID");
    write_stream(out, ':');
    json_write_string(out, ps->id);
    for (i = 0; i < ps->ind; i++) {
        write_stream(out, ',');
        json_write_string(out, ps->list[i].name);
        write_stream(out, ':');
        json_write_string(out, ps->list[i].value);
    }
    write_stream(out, '}');
    peer_cnt++;
    return 0;
}

static void command_sync(char * token, Channel * c) {
    if (read_stream(&c->inp) != MARKER_EOM) exception(ERR_JSON_SYNTAX);
    write_stringz(&c->out, "R");
    write_stringz(&c->out, token);
    write_errno(&c->out, 0);
    write_stream(&c->out, MARKER_EOM);
}

typedef struct RedirectInfo {
    Channel * channel;
    char token[256];
} RedirectInfo;

static void channel_connected(void * args, int error, Channel * c2) {
    RedirectInfo * info = (RedirectInfo *)args;
    Channel * c1 = info->channel;

    if (!is_stream_closed(c1)) {
        if (!error) proxy_create(c1, c2);

        write_stringz(&c1->out, "R");
        write_stringz(&c1->out, info->token);
        write_errno(&c1->out, error);
        write_stream(&c1->out, MARKER_EOM);
    }
    else if (!error) {
        channel_close(c2);
    }
    stream_unlock(c1);
    loc_free(info);
}

static void command_redirect(char * token, Channel * c) {
    char id[256];
    PeerServer * ps = NULL;

    json_read_string(&c->inp, id, sizeof(id));
    if (read_stream(&c->inp) != 0) exception(ERR_JSON_SYNTAX);
    if (read_stream(&c->inp) != MARKER_EOM) exception(ERR_JSON_SYNTAX);

    ps = peer_server_find(id);
    if (ps != NULL) {
        RedirectInfo * info = loc_alloc_zero(sizeof(RedirectInfo));
        stream_lock(c);
        info->channel = c;
        strncpy(info->token, token, sizeof(info->token) - 1);
        channel_connect(ps, channel_connected, info);
    }
    else {
        write_stringz(&c->out, "R");
        write_stringz(&c->out, token);
        write_errno(&c->out, ERR_UNKNOWN_PEER);
        write_stream(&c->out, MARKER_EOM);
    }
}

static void command_get_peers(char * token, Channel * c) {
    if (read_stream(&c->inp) != MARKER_EOM) exception(ERR_JSON_SYNTAX);

    write_stringz(&c->out, "R");
    write_stringz(&c->out, token);
    write_errno(&c->out, 0);
    write_stream(&c->out, '[');
    peer_cnt = 0;
    peer_server_iter(write_peer_properties, &c->out);
    write_stream(&c->out, ']');
    write_stream(&c->out, 0);
    write_stream(&c->out, MARKER_EOM);
}

static void peer_change_event(PeerServer * ps, int type, void * arg) {
    OutputStream * out = (OutputStream *)arg;

    if ((ps->flags & PS_FLAG_DISCOVERABLE) == 0) return;
    write_stringz(out, "E");
    write_stringz(out, LOCATOR);
    switch (type) {
    case PS_EVENT_ADDED:
        write_stringz(out, "peerAdded");
        peer_cnt = 0;
        write_peer_properties(ps, out);
        break;
    case PS_EVENT_CHANGED:
        write_stringz(out, "peerChanged");
        peer_cnt = 0;
        write_peer_properties(ps, out);
        break;
    case PS_EVENT_HEART_BEAT:
        write_stringz(out, "peerHeartBeat");
        json_write_string(out, ps->id);
        break;
    case PS_EVENT_REMOVED:
        write_stringz(out, "peerRemoved");
        json_write_string(out, ps->id);
        break;
    }
    write_stream(out, 0);
    write_stream(out, MARKER_EOM);
    flush_stream(out);
}

void ini_locator_service(Protocol * p, TCFBroadcastGroup * bcg) {
    assert(is_dispatch_thread());
    peer_server_add_listener(peer_change_event, &bcg->out);
    add_command_handler(p, LOCATOR, "sync", command_sync);
    add_command_handler(p, LOCATOR, "redirect", command_redirect);
    add_command_handler(p, LOCATOR, "getPeers", command_get_peers);
}
#endif /* SERVICE_Locator */

void discovery_start(void) {
#if ENABLE_Discovery
    discovery_start_udp();
#endif
}

