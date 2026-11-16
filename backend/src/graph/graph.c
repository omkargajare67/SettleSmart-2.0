#include "graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int graph_init(DirectedGraph *g, size_t max_edge_capacity) {
    if (!g) {
        return -1;
    }
    g->nodes = NULL;
    g->node_count = 0;
    g->edge_count = 0;
    g->max_edge_capacity = max_edge_capacity > 0 ? max_edge_capacity : 500;
    return 0;
}

GraphNode *graph_find_node(const DirectedGraph *g, long long entity_id) {
    if (!g || entity_id <= 0) {
        return NULL;
    }
    GraphNode *curr = g->nodes;
    while (curr) {
        if (curr->entity_id == entity_id) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

GraphNode *graph_get_or_create_node(DirectedGraph *g, long long entity_id, const char *name, const char *type) {
    if (!g || entity_id <= 0) {
        return NULL;
    }

    GraphNode *existing = graph_find_node(g, entity_id);
    if (existing) {
        if (name && strlen(name) > 0 && strlen(existing->entity_name) == 0) {
            strncpy(existing->entity_name, name, sizeof(existing->entity_name) - 1);
        }
        if (type && strlen(type) > 0 && strlen(existing->entity_type) == 0) {
            strncpy(existing->entity_type, type, sizeof(existing->entity_type) - 1);
        }
        return existing;
    }

    GraphNode *node = (GraphNode *)calloc(1, sizeof(GraphNode));
    if (!node) {
        return NULL;
    }

    node->entity_id = entity_id;
    if (name) {
        strncpy(node->entity_name, name, sizeof(node->entity_name) - 1);
    }
    if (type) {
        strncpy(node->entity_type, type, sizeof(node->entity_type) - 1);
    }
    node->edges = NULL;
    node->next = g->nodes;
    g->nodes = node;
    g->node_count++;
    return node;
}

int graph_node_has_edges(const DirectedGraph *g, long long entity_id) {
    if (!g || entity_id <= 0) {
        return 0;
    }

    GraphNode *n = graph_find_node(g, entity_id);
    if (n && n->edges) {
        return 1;
    }

    GraphNode *curr = g->nodes;
    while (curr) {
        GraphEdge *e = curr->edges;
        while (e) {
            if (e->receiver_entity_id == entity_id) {
                return 1;
            }
            e = e->next;
        }
        curr = curr->next;
    }
    return 0;
}

static int graph_remove_isolated_node(DirectedGraph *g, long long entity_id) {
    if (!g || entity_id <= 0) {
        return 0;
    }
    if (graph_node_has_edges(g, entity_id)) {
        return 0;
    }

    GraphNode *curr = g->nodes;
    GraphNode *prev = NULL;
    while (curr) {
        if (curr->entity_id == entity_id) {
            if (prev) {
                prev->next = curr->next;
            } else {
                g->nodes = curr->next;
            }
            free(curr);
            g->node_count--;
            return 1;
        }
        prev = curr;
        curr = curr->next;
    }
    return 0;
}

GraphEdge *graph_find_edge_by_tx_id(const DirectedGraph *g, long long tx_id, long long *out_sender_id) {
    if (!g || tx_id <= 0) {
        return NULL;
    }

    GraphNode *curr = g->nodes;
    while (curr) {
        GraphEdge *e = curr->edges;
        while (e) {
            if (e->transaction_id == tx_id) {
                if (out_sender_id) {
                    *out_sender_id = curr->entity_id;
                }
                return e;
            }
            e = e->next;
        }
        curr = curr->next;
    }
    return NULL;
}

int graph_update_edge_status(DirectedGraph *g, long long tx_id, TransactionStatus new_status) {
    if (!g || tx_id <= 0) {
        return -1;
    }

    GraphEdge *e = graph_find_edge_by_tx_id(g, tx_id, NULL);
    if (!e) {
        return -1;
    }

    e->status = new_status;
    return 0;
}

int graph_remove_edge_by_tx_id(DirectedGraph *g, long long tx_id) {
    if (!g || tx_id <= 0) {
        return -1;
    }

    GraphNode *curr = g->nodes;
    while (curr) {
        GraphEdge *e = curr->edges;
        GraphEdge *prev = NULL;
        while (e) {
            if (e->transaction_id == tx_id) {
                long long s_id = curr->entity_id;
                long long r_id = e->receiver_entity_id;
                if (prev) {
                    prev->next = e->next;
                } else {
                    curr->edges = e->next;
                }
                free(e);
                g->edge_count--;
                graph_remove_isolated_node(g, s_id);
                graph_remove_isolated_node(g, r_id);
                return 0;
            }
            prev = e;
            e = e->next;
        }
        curr = curr->next;
    }
    return -1;
}

int graph_evict_oldest_edge(DirectedGraph *g) {
    if (!g || g->edge_count == 0) {
        return -1;
    }

    long long oldest_id = -1;
    char oldest_time[32] = "";

    GraphNode *curr = g->nodes;
    while (curr) {
        GraphEdge *e = curr->edges;
        while (e) {
            if (oldest_id == -1 ||
                strcmp(e->created_at, oldest_time) < 0 ||
                (strcmp(e->created_at, oldest_time) == 0 && e->transaction_id < oldest_id)) {
                oldest_id = e->transaction_id;
                strncpy(oldest_time, e->created_at, sizeof(oldest_time) - 1);
            }
            e = e->next;
        }
        curr = curr->next;
    }

    if (oldest_id <= 0) {
        return -1;
    }

    return graph_remove_edge_by_tx_id(g, oldest_id);
}

int graph_add_edge(DirectedGraph *g, const Transaction *tx) {
    if (!g || !tx || tx->id <= 0 || tx->sender_entity_id <= 0 || tx->receiver_entity_id <= 0) {
        return -1;
    }

    if (graph_find_edge_by_tx_id(g, tx->id, NULL)) {
        return -2;
    }

    if (g->max_edge_capacity > 0 && g->edge_count >= g->max_edge_capacity) {
        graph_evict_oldest_edge(g);
    }

    GraphNode *s_node = graph_get_or_create_node(g, tx->sender_entity_id, "", "");
    GraphNode *r_node = graph_get_or_create_node(g, tx->receiver_entity_id, "", "");
    if (!s_node || !r_node) {
        return -1;
    }

    GraphEdge *edge = (GraphEdge *)calloc(1, sizeof(GraphEdge));
    if (!edge) {
        return -1;
    }

    edge->transaction_id = tx->id;
    edge->sender_entity_id = tx->sender_entity_id;
    edge->receiver_entity_id = tx->receiver_entity_id;
    edge->amount_paise = tx->amount_paise;
    strncpy(edge->amount_str, tx->amount_str, sizeof(edge->amount_str) - 1);
    strncpy(edge->currency, tx->currency, sizeof(edge->currency) - 1);
    edge->status = tx->status;
    strncpy(edge->created_at, tx->created_at, sizeof(edge->created_at) - 1);

    edge->next = s_node->edges;
    s_node->edges = edge;
    g->edge_count++;
    return 0;
}

size_t graph_get_node_count(const DirectedGraph *g) {
    return g ? g->node_count : 0;
}

size_t graph_get_edge_count(const DirectedGraph *g) {
    return g ? g->edge_count : 0;
}

void graph_traverse_edges(const DirectedGraph *g, GraphEdgeVisitor visitor, void *user_data) {
    if (!g || !visitor) {
        return;
    }
    GraphNode *curr = g->nodes;
    while (curr) {
        GraphEdge *e = curr->edges;
        while (e) {
            visitor(curr->entity_id, e, user_data);
            e = e->next;
        }
        curr = curr->next;
    }
}

void graph_clear(DirectedGraph *g) {
    if (!g) {
        return;
    }

    GraphNode *n_curr = g->nodes;
    while (n_curr) {
        GraphEdge *e_curr = n_curr->edges;
        while (e_curr) {
            GraphEdge *e_next = e_curr->next;
            free(e_curr);
            e_curr = e_next;
        }
        n_curr->edges = NULL;
        GraphNode *n_next = n_curr->next;
        free(n_curr);
        n_curr = n_next;
    }

    g->nodes = NULL;
    g->node_count = 0;
    g->edge_count = 0;
}

void graph_destroy(DirectedGraph *g) {
    if (!g) {
        return;
    }
    graph_clear(g);
    g->max_edge_capacity = 0;
}
