#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "../database/db_client.h"
#include "../../third_party/mongoose/mongoose.h"
#include <stddef.h>
#include <stdint.h>

#define WS_MAX_CLIENTS 128

typedef struct {
    struct mg_connection *conn;
    long long organization_id;
    long long user_id;
    int is_active;
} WsClient;

void ws_init(DBClient *db_client);
int ws_handle_upgrade(struct mg_connection *c, struct mg_http_message *hm,
                      const char *allowed_origins, DBClient *db_client);
void ws_handle_message(struct mg_connection *c, struct mg_ws_message *wm, DBClient *db_client);
void ws_handle_close(struct mg_connection *c);
void ws_broadcast_event(long long org_id, const char *msg);
int ws_replay_events(struct mg_connection *c, DBClient *db_client, long long org_id, int64_t last_seen_seq);
void ws_cleanup(void);

#endif
