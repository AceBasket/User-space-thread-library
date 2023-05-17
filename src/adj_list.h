#ifndef ADJ_LIST_H
#define ADJ_LIST_H

#include "thread.h"

// function to print the adj_list content
void adj_list_debug();
void adj_list_init(head_t *head);
void add_edge(thread_t src_tid, thread_t dest_tid);
void remove_edge(thread_t src_tid, thread_t dest_tid);
void remove_edge_when_finished(thread_t tid);
int has_cycle(thread_t tid);
void free_adj_list();
#endif