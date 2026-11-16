#ifndef OUTBOX_CONSUMER_H
#define OUTBOX_CONSUMER_H

#include "../database/db_client.h"
#include "../dsa/dsa_state.h"
#include "../graph/graph.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    char instance_id[64];
    DBClient *db_client;
    DsaState *dsa_state;
    DirectedGraph *graph;
    int is_running;
} OutboxConsumer;

int outbox_consumer_init(OutboxConsumer *consumer, const char *instance_id,
                         DBClient *client, DsaState *optional_state,
                         DirectedGraph *optional_graph);

int outbox_consumer_get_cursor(DBClient *client, const char *instance_id,
                               long long org_id, int64_t *out_seq);

int outbox_consumer_set_cursor(DBClient *client, const char *instance_id,
                               long long org_id, int64_t seq);

int outbox_consumer_poll_org(OutboxConsumer *consumer, long long org_id,
                             int limit, int *out_events_processed);

int outbox_consumer_poll_all(OutboxConsumer *consumer, int limit,
                             int *out_events_processed);

void outbox_consumer_destroy(OutboxConsumer *consumer);

#endif
