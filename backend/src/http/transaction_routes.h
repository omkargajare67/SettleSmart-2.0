#ifndef TRANSACTION_ROUTES_H
#define TRANSACTION_ROUTES_H

#include "http_server.h"

int transaction_routes_handle(struct mg_connection *c, struct mg_http_message *hm,
                              const HttpServerConfig *config, const char *active_origin);

#endif
