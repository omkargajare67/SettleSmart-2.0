#ifndef GRAPH_SERVICE_H
#define GRAPH_SERVICE_H

#include "graph.h"
#include "../database/db_client.h"
#include <stddef.h>

int graph_service_recover_from_db(DirectedGraph *g, DBClient *client, long long org_id, size_t limit);

int graph_service_get_snapshot_json(DBClient *client, long long org_id, long long entity_id,
                                    const char *status, const char *from_date, const char *to_date,
                                    int page, int limit, char *out_json, size_t max_len);

#endif
