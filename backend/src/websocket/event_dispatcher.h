#ifndef EVENT_DISPATCHER_H
#define EVENT_DISPATCHER_H

#include "../database/db_client.h"
#include "../transactions/transaction.h"
#include <stddef.h>
#include <stdint.h>

int outbox_record_event(DBClient *client, long long org_id, long long tx_id,
                        const char *event_type, const char *payload, int64_t *out_seq);

int event_dispatcher_build_tx_payload(const Transaction *tx, char *out_payload, size_t max_len);

int event_dispatcher_format_ws_message(const char *event_type, int64_t event_seq,
                                       const char *payload_json, char *out_msg, size_t max_len);

int outbox_get_watermark(DBClient *client, long long org_id, int64_t *out_watermark);

int outbox_get_events_after(DBClient *client, long long org_id, int64_t after_seq,
                            int limit, char *out_json, size_t max_len, int *out_count);

#endif
