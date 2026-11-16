CFLAGS = -Wall -Wextra -std=c11 -Isrc/config -Isrc/database -Isrc/auth -Isrc/entities -Isrc/transactions -Isrc/dsa -Isrc/graph -Isrc/websocket -Isrc/algorithms -Isrc/http -Ithird_party/argon2/include -Ithird_party/mongoose

ifeq ($(OS),Windows_NT)
    CC = C:/mingw64/bin/gcc.exe
    MYSQL_DIR ?= C:/Program Files/MySQL/MySQL Server 9.7
    CFLAGS += -I"$(MYSQL_DIR)/include"
    LDFLAGS += -L"$(MYSQL_DIR)/lib" -llibmysql -lws2_32 -lbcrypt
    EXE_EXT = .exe
    RM = del /Q /F
    export PATH := $(MYSQL_DIR)/bin;$(MYSQL_DIR)/lib;$(PATH)
else
    CC ?= gcc
    CFLAGS += $(shell mysql_config --cflags 2>/dev/null || mariadb_config --cflags 2>/dev/null || echo "-I/usr/include/mysql")
    LDFLAGS += $(shell mysql_config --libs 2>/dev/null || mariadb_config --libs 2>/dev/null || echo "-lmysqlclient") -lpthread
    EXE_EXT =
    RM = rm -f
endif

SRC_CONFIG = src/config/env_loader.c
SRC_DB = src/database/db_client.c
SRC_MONGOOSE = third_party/mongoose/mongoose.c
SRC_ARGON2 = third_party/argon2/src/argon2.c third_party/argon2/src/core.c third_party/argon2/src/encoding.c third_party/argon2/src/opt.c third_party/argon2/src/thread.c third_party/argon2/src/blake2/blake2b.c
SRC_AUTH = src/auth/crypto_utils.c src/auth/password.c src/auth/authorization.c src/auth/session.c src/auth/auth_service.c
SRC_ENTITY = src/entities/entity.c src/entities/entity_service.c
SRC_WEBSOCKET = src/websocket/event_dispatcher.c src/websocket/websocket.c src/websocket/outbox_consumer.c
SRC_TRANSACTION = src/transactions/transaction.c src/transactions/transaction_service.c $(SRC_WEBSOCKET)
SRC_DSA = src/dsa/linked_list.c src/dsa/hashmap.c src/dsa/queue.c src/dsa/stack.c src/dsa/priority_queue.c src/dsa/dsa_state.c
SRC_GRAPH = src/graph/graph.c src/graph/graph_service.c
SRC_ALGORITHMS = src/algorithms/bubble_sort.c src/algorithms/binary_search.c src/algorithms/settlement.c
SRC_PROCESSOR = src/transactions/transaction_processor.c $(SRC_ALGORITHMS)
SRC_HTTP = src/http/http_server.c src/http/entity_routes.c src/http/transaction_routes.c src/http/graph_routes.c src/http/processing_routes.c

AUTH_ALL_SRCS = $(SRC_AUTH) $(SRC_ARGON2) $(SRC_MONGOOSE) $(SRC_DB) $(SRC_CONFIG)
ENTITY_ALL_SRCS = $(SRC_ENTITY) $(AUTH_ALL_SRCS)
TRANSACTION_ALL_SRCS = $(SRC_TRANSACTION) $(ENTITY_ALL_SRCS)
DSA_ALL_SRCS = $(SRC_DSA) $(TRANSACTION_ALL_SRCS)
GRAPH_ALL_SRCS = $(SRC_GRAPH) $(TRANSACTION_ALL_SRCS)
PROCESSOR_ALL_SRCS = $(SRC_PROCESSOR) $(SRC_DSA) $(SRC_GRAPH) $(TRANSACTION_ALL_SRCS)
HTTP_ALL_SRCS = $(SRC_HTTP) $(PROCESSOR_ALL_SRCS)

ALL_TESTS = tests/env_loader_test$(EXE_EXT) \
            tests/db_connection_test$(EXE_EXT) \
            tests/db_dummy_table_test$(EXE_EXT) \
            tests/db_integrity_test$(EXE_EXT) \
            tests/auth_password_test$(EXE_EXT) \
            tests/auth_registration_test$(EXE_EXT) \
            tests/auth_login_test$(EXE_EXT) \
            tests/auth_session_test$(EXE_EXT) \
            tests/auth_authorization_test$(EXE_EXT) \
            tests/auth_concurrent_admin_test$(EXE_EXT) \
            tests/auth_csrf_test$(EXE_EXT) \
            tests/auth_http_endpoints_test$(EXE_EXT) \
            tests/entity_validation_test$(EXE_EXT) \
            tests/entity_crud_test$(EXE_EXT) \
            tests/entity_pagination_test$(EXE_EXT) \
            tests/entity_concurrency_test$(EXE_EXT) \
            tests/entity_http_test$(EXE_EXT) \
            tests/transaction_validation_test$(EXE_EXT) \
            tests/transaction_crud_test$(EXE_EXT) \
            tests/transaction_idempotency_test$(EXE_EXT) \
            tests/transaction_concurrency_test$(EXE_EXT) \
            tests/transaction_history_test$(EXE_EXT) \
            tests/transaction_reversal_test$(EXE_EXT) \
            tests/transaction_http_test$(EXE_EXT) \
            tests/dsa_linked_list_test$(EXE_EXT) \
            tests/dsa_hashmap_test$(EXE_EXT) \
            tests/dsa_queue_test$(EXE_EXT) \
            tests/dsa_stack_test$(EXE_EXT) \
            tests/dsa_priority_queue_test$(EXE_EXT) \
            tests/dsa_state_recovery_test$(EXE_EXT) \
            tests/dsa_multi_org_test$(EXE_EXT) \
            tests/dsa_transaction_sync_test$(EXE_EXT) \
            tests/graph_structure_test$(EXE_EXT) \
            tests/graph_edge_test$(EXE_EXT) \
            tests/graph_cache_test$(EXE_EXT) \
            tests/graph_recovery_test$(EXE_EXT) \
            tests/graph_multi_org_test$(EXE_EXT) \
            tests/graph_http_test$(EXE_EXT) \
            tests/websocket_auth_test$(EXE_EXT) \
            tests/websocket_event_test$(EXE_EXT) \
            tests/websocket_isolation_test$(EXE_EXT) \
            tests/websocket_recovery_test$(EXE_EXT) \
            tests/websocket_concurrency_test$(EXE_EXT) \
            tests/algorithm_bubble_sort_test$(EXE_EXT) \
            tests/algorithm_binary_search_test$(EXE_EXT) \
            tests/algorithm_settlement_test$(EXE_EXT) \
            tests/processing_normal_test$(EXE_EXT) \
            tests/processing_reversal_test$(EXE_EXT) \
            tests/processing_concurrency_test$(EXE_EXT) \
            tests/processing_http_test$(EXE_EXT) \
            tests/multitab_csrf_test$(EXE_EXT) \
            tests/multi_instance_ratelimit_test$(EXE_EXT) \
            tests/graph_snapshot_consistency_test$(EXE_EXT) \
            tests/multi_instance_websocket_test$(EXE_EXT) \
            tests/multi_instance_dsa_test$(EXE_EXT) \
            tests/db_failure_recovery_test$(EXE_EXT)

all: $(ALL_TESTS) server$(EXE_EXT)

server$(EXE_EXT): src/main.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/env_loader_test$(EXE_EXT): tests/env_loader_test.c $(SRC_CONFIG)
	$(CC) -Wall -Wextra -std=c11 -Isrc/config -o $@ $^

tests/db_connection_test$(EXE_EXT): tests/db_connection_test.c $(SRC_DB) $(SRC_CONFIG)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/db_dummy_table_test$(EXE_EXT): tests/db_dummy_table_test.c $(SRC_DB) $(SRC_CONFIG)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/db_integrity_test$(EXE_EXT): tests/db_integrity_test.c $(SRC_DB) $(SRC_CONFIG)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/auth_password_test$(EXE_EXT): tests/auth_password_test.c src/auth/password.c src/auth/crypto_utils.c $(SRC_ARGON2) $(SRC_MONGOOSE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/auth_registration_test$(EXE_EXT): tests/auth_registration_test.c $(AUTH_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/auth_login_test$(EXE_EXT): tests/auth_login_test.c $(AUTH_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/auth_session_test$(EXE_EXT): tests/auth_session_test.c $(AUTH_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/auth_authorization_test$(EXE_EXT): tests/auth_authorization_test.c $(AUTH_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/auth_concurrent_admin_test$(EXE_EXT): tests/auth_concurrent_admin_test.c $(AUTH_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/auth_csrf_test$(EXE_EXT): tests/auth_csrf_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/auth_http_endpoints_test$(EXE_EXT): tests/auth_http_endpoints_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/entity_validation_test$(EXE_EXT): tests/entity_validation_test.c src/entities/entity.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/entity_crud_test$(EXE_EXT): tests/entity_crud_test.c $(SRC_ENTITY) $(SRC_DB) $(SRC_CONFIG)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/entity_pagination_test$(EXE_EXT): tests/entity_pagination_test.c $(SRC_ENTITY) $(SRC_DB) $(SRC_CONFIG)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/entity_concurrency_test$(EXE_EXT): tests/entity_concurrency_test.c $(SRC_ENTITY) $(SRC_DB) $(SRC_CONFIG)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/entity_http_test$(EXE_EXT): tests/entity_http_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/transaction_validation_test$(EXE_EXT): tests/transaction_validation_test.c src/transactions/transaction.c src/auth/crypto_utils.c third_party/mongoose/mongoose.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/transaction_crud_test$(EXE_EXT): tests/transaction_crud_test.c $(TRANSACTION_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/transaction_idempotency_test$(EXE_EXT): tests/transaction_idempotency_test.c $(TRANSACTION_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/transaction_concurrency_test$(EXE_EXT): tests/transaction_concurrency_test.c $(TRANSACTION_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/transaction_history_test$(EXE_EXT): tests/transaction_history_test.c $(TRANSACTION_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/transaction_reversal_test$(EXE_EXT): tests/transaction_reversal_test.c $(TRANSACTION_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/transaction_http_test$(EXE_EXT): tests/transaction_http_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/dsa_linked_list_test$(EXE_EXT): tests/dsa_linked_list_test.c src/dsa/linked_list.c src/transactions/transaction.c src/auth/crypto_utils.c third_party/mongoose/mongoose.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/dsa_hashmap_test$(EXE_EXT): tests/dsa_hashmap_test.c src/dsa/hashmap.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/dsa_queue_test$(EXE_EXT): tests/dsa_queue_test.c src/dsa/queue.c src/transactions/transaction.c src/auth/crypto_utils.c third_party/mongoose/mongoose.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/dsa_stack_test$(EXE_EXT): tests/dsa_stack_test.c src/dsa/stack.c src/transactions/transaction.c src/auth/crypto_utils.c third_party/mongoose/mongoose.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/dsa_priority_queue_test$(EXE_EXT): tests/dsa_priority_queue_test.c src/dsa/priority_queue.c src/transactions/transaction.c src/auth/crypto_utils.c third_party/mongoose/mongoose.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/dsa_state_recovery_test$(EXE_EXT): tests/dsa_state_recovery_test.c $(DSA_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/dsa_multi_org_test$(EXE_EXT): tests/dsa_multi_org_test.c $(DSA_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/dsa_transaction_sync_test$(EXE_EXT): tests/dsa_transaction_sync_test.c $(DSA_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/graph_structure_test$(EXE_EXT): tests/graph_structure_test.c src/graph/graph.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/graph_edge_test$(EXE_EXT): tests/graph_edge_test.c src/graph/graph.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/graph_cache_test$(EXE_EXT): tests/graph_cache_test.c src/graph/graph.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/graph_recovery_test$(EXE_EXT): tests/graph_recovery_test.c $(GRAPH_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/graph_multi_org_test$(EXE_EXT): tests/graph_multi_org_test.c $(GRAPH_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/graph_http_test$(EXE_EXT): tests/graph_http_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/websocket_auth_test$(EXE_EXT): tests/websocket_auth_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/websocket_event_test$(EXE_EXT): tests/websocket_event_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/websocket_isolation_test$(EXE_EXT): tests/websocket_isolation_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/websocket_recovery_test$(EXE_EXT): tests/websocket_recovery_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/websocket_concurrency_test$(EXE_EXT): tests/websocket_concurrency_test.c $(GRAPH_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/algorithm_bubble_sort_test$(EXE_EXT): tests/algorithm_bubble_sort_test.c src/algorithms/bubble_sort.c src/transactions/transaction.c src/auth/crypto_utils.c third_party/mongoose/mongoose.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/algorithm_binary_search_test$(EXE_EXT): tests/algorithm_binary_search_test.c src/algorithms/binary_search.c src/transactions/transaction.c src/auth/crypto_utils.c third_party/mongoose/mongoose.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/algorithm_settlement_test$(EXE_EXT): tests/algorithm_settlement_test.c src/algorithms/settlement.c src/algorithms/binary_search.c src/transactions/transaction.c src/auth/crypto_utils.c third_party/mongoose/mongoose.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/processing_normal_test$(EXE_EXT): tests/processing_normal_test.c $(PROCESSOR_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/processing_reversal_test$(EXE_EXT): tests/processing_reversal_test.c $(PROCESSOR_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/processing_concurrency_test$(EXE_EXT): tests/processing_concurrency_test.c $(PROCESSOR_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/processing_http_test$(EXE_EXT): tests/processing_http_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/multitab_csrf_test$(EXE_EXT): tests/multitab_csrf_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/multi_instance_ratelimit_test$(EXE_EXT): tests/multi_instance_ratelimit_test.c $(AUTH_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/graph_snapshot_consistency_test$(EXE_EXT): tests/graph_snapshot_consistency_test.c $(PROCESSOR_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/multi_instance_websocket_test$(EXE_EXT): tests/multi_instance_websocket_test.c $(HTTP_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/multi_instance_dsa_test$(EXE_EXT): tests/multi_instance_dsa_test.c $(PROCESSOR_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/db_failure_recovery_test$(EXE_EXT): tests/db_failure_recovery_test.c $(TRANSACTION_ALL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: all
	tests/env_loader_test$(EXE_EXT)
	tests/db_connection_test$(EXE_EXT)
	tests/db_dummy_table_test$(EXE_EXT)
	tests/db_integrity_test$(EXE_EXT)
	tests/auth_password_test$(EXE_EXT)
	tests/auth_registration_test$(EXE_EXT)
	tests/auth_login_test$(EXE_EXT)
	tests/auth_session_test$(EXE_EXT)
	tests/auth_authorization_test$(EXE_EXT)
	tests/auth_concurrent_admin_test$(EXE_EXT)
	tests/auth_csrf_test$(EXE_EXT)
	tests/auth_http_endpoints_test$(EXE_EXT)
	tests/entity_validation_test$(EXE_EXT)
	tests/entity_crud_test$(EXE_EXT)
	tests/entity_pagination_test$(EXE_EXT)
	tests/entity_concurrency_test$(EXE_EXT)
	tests/entity_http_test$(EXE_EXT)
	tests/transaction_validation_test$(EXE_EXT)
	tests/transaction_crud_test$(EXE_EXT)
	tests/transaction_idempotency_test$(EXE_EXT)
	tests/transaction_concurrency_test$(EXE_EXT)
	tests/transaction_history_test$(EXE_EXT)
	tests/transaction_reversal_test$(EXE_EXT)
	tests/transaction_http_test$(EXE_EXT)
	tests/dsa_linked_list_test$(EXE_EXT)
	tests/dsa_hashmap_test$(EXE_EXT)
	tests/dsa_queue_test$(EXE_EXT)
	tests/dsa_stack_test$(EXE_EXT)
	tests/dsa_priority_queue_test$(EXE_EXT)
	tests/dsa_state_recovery_test$(EXE_EXT)
	tests/dsa_multi_org_test$(EXE_EXT)
	tests/dsa_transaction_sync_test$(EXE_EXT)
	tests/graph_structure_test$(EXE_EXT)
	tests/graph_edge_test$(EXE_EXT)
	tests/graph_cache_test$(EXE_EXT)
	tests/graph_recovery_test$(EXE_EXT)
	tests/graph_multi_org_test$(EXE_EXT)
	tests/graph_http_test$(EXE_EXT)
	tests/websocket_auth_test$(EXE_EXT)
	tests/websocket_event_test$(EXE_EXT)
	tests/websocket_isolation_test$(EXE_EXT)
	tests/websocket_recovery_test$(EXE_EXT)
	tests/websocket_concurrency_test$(EXE_EXT)
	tests/algorithm_bubble_sort_test$(EXE_EXT)
	tests/algorithm_binary_search_test$(EXE_EXT)
	tests/algorithm_settlement_test$(EXE_EXT)
	tests/processing_normal_test$(EXE_EXT)
	tests/processing_reversal_test$(EXE_EXT)
	tests/processing_concurrency_test$(EXE_EXT)
	tests/processing_http_test$(EXE_EXT)
	tests/multitab_csrf_test$(EXE_EXT)
	tests/multi_instance_ratelimit_test$(EXE_EXT)
	tests/graph_snapshot_consistency_test$(EXE_EXT)
	tests/multi_instance_websocket_test$(EXE_EXT)
	tests/multi_instance_dsa_test$(EXE_EXT)
	tests/db_failure_recovery_test$(EXE_EXT)

clean:
	$(RM) tests\*$(EXE_EXT)
