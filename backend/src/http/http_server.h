#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "../database/db_client.h"
#include "../auth/auth_service.h"
#include "../auth/session.h"
#include "../../third_party/mongoose/mongoose.h"

typedef struct {
    int port;
    char allowed_origins[512];
    char app_env[32];
    int cookie_secure;
    int enable_public_registration;
    char instance_id[64];
    DBClient *db_client;
} HttpServerConfig;

int http_server_start(HttpServerConfig *config);
void http_server_poll(int ms);
void http_server_stop(void);

int http_is_origin_allowed(const char *allowed_origins, const char *origin);

int http_parse_cookie(const char *cookie_str, size_t cookie_len,
                      const char *cookie_name, char *out_val, size_t max_out);

void http_handle_request(struct mg_connection *c, struct mg_http_message *hm,
                         HttpServerConfig *config);

void http_send_json_reply(struct mg_connection *c, int status_code,
                          const char *origin, const HttpServerConfig *config,
                          const char *extra_headers, const char *json_body);

int http_extract_authenticated_session(struct mg_http_message *hm,
                                      DBClient *db_client,
                                      SessionInfo *out_session,
                                      char *raw_session_token,
                                      size_t raw_token_len);

int http_verify_csrf_header(struct mg_http_message *hm, const SessionInfo *session);

#endif
