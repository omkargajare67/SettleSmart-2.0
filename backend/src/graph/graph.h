#ifndef GRAPH_H
#define GRAPH_H

#include "../transactions/transaction.h"
#include <stddef.h>
#include <stdint.h>

typedef struct GraphEdge {
    long long transaction_id;
    long long sender_entity_id;
    long long receiver_entity_id;
    int64_t amount_paise;
    char amount_str[32];
    char currency[4];
    TransactionStatus status;
    char created_at[32];
    struct GraphEdge *next;
} GraphEdge;

typedef struct GraphNode {
    long long entity_id;
    char entity_name[150];
    char entity_type[32];
    GraphEdge *edges;
    struct GraphNode *next;
} GraphNode;

typedef struct {
    GraphNode *nodes;
    size_t node_count;
    size_t edge_count;
    size_t max_edge_capacity;
} DirectedGraph;

typedef void (*GraphEdgeVisitor)(long long sender_id, const GraphEdge *edge, void *user_data);

int graph_init(DirectedGraph *g, size_t max_edge_capacity);
GraphNode *graph_get_or_create_node(DirectedGraph *g, long long entity_id, const char *name, const char *type);
GraphNode *graph_find_node(const DirectedGraph *g, long long entity_id);
int graph_add_edge(DirectedGraph *g, const Transaction *tx);
GraphEdge *graph_find_edge_by_tx_id(const DirectedGraph *g, long long tx_id, long long *out_sender_id);
int graph_update_edge_status(DirectedGraph *g, long long tx_id, TransactionStatus new_status);
int graph_remove_edge_by_tx_id(DirectedGraph *g, long long tx_id);
int graph_evict_oldest_edge(DirectedGraph *g);
int graph_node_has_edges(const DirectedGraph *g, long long entity_id);
size_t graph_get_node_count(const DirectedGraph *g);
size_t graph_get_edge_count(const DirectedGraph *g);
void graph_traverse_edges(const DirectedGraph *g, GraphEdgeVisitor visitor, void *user_data);
void graph_clear(DirectedGraph *g);
void graph_destroy(DirectedGraph *g);

#endif
