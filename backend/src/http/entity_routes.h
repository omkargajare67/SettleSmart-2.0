#ifndef ENTITY_ROUTES_H
#define ENTITY_ROUTES_H

#include "http_server.h"

int entity_routes_handle(struct mg_connection *c, struct mg_http_message *hm,
                         const HttpServerConfig *config, const char *active_origin);

#endif
