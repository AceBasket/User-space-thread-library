#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adj_list.h"
#include "thread_struct.h"
#include "queue.h"

#define MAX_THREADS 1000

// Structure to represent a node in the adjacency list
typedef struct node_t {
    struct thread* thread; // pointer to the thread represented by this node
    struct node_t* next; // pointer to the next node in the adjacency list
} node_t;

typedef struct adj_list_entry_t {
    thread_t tid; // thread id -> field thread of the struct thread
    node_t* head_joined_threads_arr; // pointer to the head of the list of threads that this thread is waiting for
    struct adj_list_entry_t* next; // pointer to the next entry in the adjacency list
} adj_list_entry_t;

adj_list_entry_t* adj_list_head = NULL;
static head_t * head_run_queue = NULL;
int visited[MAX_THREADS] = {0};
int rec_stack[MAX_THREADS] = {0};

void adj_list_init(head_t * head) {
	head_run_queue = head;
}

void adj_list_debug() {
    printf("ADJACENCY LIST\n");
	adj_list_entry_t* curr_entry = adj_list_head;
	while (curr_entry != NULL) {
		printf("tid: %p\n", curr_entry->tid);
		node_t* curr_node = curr_entry->head_joined_threads_arr;
		while (curr_node != NULL) {
			printf("-> %p ", curr_node->thread->thread);
			curr_node = curr_node->next;
		}
		printf("\n");
		curr_entry = curr_entry->next;
	}
}

/**
 * @brief Get the struct thread adress from the thread_t tid if it exists, return NULL otherwise
 * 
 * @param tid id of the thread to retrieve
 * @return struct thread* 
 */
struct thread *get_thread_by_tid(thread_t tid) {
    struct thread *elm;
    // look for the thread in the run queue
    SIMPLEQ_FOREACH(elm, head_run_queue, entry) {
        if (elm->thread == tid)
            return elm;
    }
    return NULL;
}

/**
 * @brief Get the entry in the adjacency list with a tid == tid
 * 
 * @param tid id of the thread to retrieve to entry from the adjacency list
 * @return adj_list_entry_t* 
 */
adj_list_entry_t* get_thread_adj_list_entry(thread_t tid) {
	adj_list_entry_t* curr_entry =  adj_list_head;
	while (curr_entry != NULL) {
		if (curr_entry->tid == tid) {
			return curr_entry;
		}
		curr_entry = curr_entry->next;
	}
    return NULL;
}

/**
 * @brief Get the thread position int the linked adjacency list (used to access visited and rec_stack arrays)
 * 
 * @param tid id of the thread
 * @return int
 */
int get_thread_adj_list_idx(thread_t tid) {
    int idx = 0;
	adj_list_entry_t* curr_entry = adj_list_head;
	while (curr_entry != NULL) {
		if (curr_entry->tid == tid) {
			return idx;
		}
		curr_entry = curr_entry->next;
		idx++;
	}
    return -1;
}

/**
 * @brief Add an edge from src_tid to dest_tid in the adjacency list
 * 
 * @param src_tid id of the source thread
 * @param dest_tid id of the destination thread
 */
void add_edge(thread_t src_tid, thread_t dest_tid) {
    // Create a new node for the destination thread
    if (get_thread_by_tid(dest_tid) == NULL) {
        return;
    }
    node_t* new_node = (node_t*) malloc(sizeof(node_t));
	adj_list_entry_t* src_th_adj_list_entry = get_thread_adj_list_entry(src_tid);
    new_node->thread = get_thread_by_tid(dest_tid);
    new_node->next = (src_th_adj_list_entry != NULL) ? src_th_adj_list_entry->head_joined_threads_arr : NULL;
	if (src_th_adj_list_entry == NULL) {
		adj_list_entry_t* new_adj_list_entry = (adj_list_entry_t*) malloc(sizeof(adj_list_entry_t));
		new_adj_list_entry->tid = src_tid;
		new_adj_list_entry->head_joined_threads_arr = new_node;
		new_adj_list_entry->next = adj_list_head;
		adj_list_head = new_adj_list_entry;
	} else {
		src_th_adj_list_entry->head_joined_threads_arr = new_node;
	}
}

/**
 * @brief remove the edge from src_tid to dest_tid in the adjacency list
 * 
 * @param src_tid id of the source thread
 * @param dest_tid id of the destination thread
 */
void remove_edge(thread_t src_tid, thread_t dest_tid) {
    adj_list_entry_t* src_th_adj_list_entry = get_thread_adj_list_entry(src_tid);
    if (src_th_adj_list_entry == NULL) {
        return;
    }
    node_t* prev_node = NULL;
    node_t* curr_node = src_th_adj_list_entry->head_joined_threads_arr;
    while (curr_node != NULL) {
        if (curr_node->thread->thread == dest_tid) {
            if (prev_node == NULL) {
                src_th_adj_list_entry->head_joined_threads_arr = curr_node->next;
            } else {
                prev_node->next = curr_node->next;
            }
            free(curr_node);
            break;
        }
        prev_node = curr_node;
        curr_node = curr_node->next;
    }
}

/**
 * @brief Remove all edges with thread tid and remove entry with tid == tid
 * 
 * @param tid id of the thread to remove the edges and entry from the adjacency list
 */
void remove_edge_when_finished(thread_t tid) {
    adj_list_entry_t* entry_to_be_freed = NULL;
	adj_list_entry_t* curr_entry = adj_list_head;
	adj_list_entry_t* prev_entry = NULL;
    while (curr_entry != NULL) {
        // equals 1 when the entry tid is equals to tid
		int is_thread_entry = 0;
        if (curr_entry->tid != NULL) {
            // if the thread is the one we are removing, remove all edges from it and set its tid to -1
            if (curr_entry->tid == tid) {
                is_thread_entry = 1;
				curr_entry->tid = NULL;
                node_t* prev_node = NULL;
                node_t* curr_node = curr_entry->head_joined_threads_arr;
                // freeing every nodes
				while(curr_node != NULL) {
                    prev_node = curr_node;
                    curr_node = curr_node->next;
                    free(prev_node);
                }
				
                entry_to_be_freed = curr_entry;
				if (prev_entry == NULL) {
                    curr_entry = curr_entry->next;
					adj_list_head = curr_entry;
				} else {
                    curr_entry = curr_entry->next;
					prev_entry->next = curr_entry;
				}

                free(entry_to_be_freed);

            } else {
                // for every other node of adj_list, remove the edge with tid from it
                node_t* prev_node = NULL;
                node_t* curr_node = curr_entry->head_joined_threads_arr;
                while (curr_node != NULL) {
                    if (curr_node->thread->thread == tid) {
                        if (prev_node == NULL) {
                            curr_entry->head_joined_threads_arr = curr_node->next;
                        } else {
                            prev_node->next = curr_node->next;
                        }
                        free(curr_node);
                        break;
                    }
                    prev_node = curr_node;
                    curr_node = curr_node->next;
                }
            }
        }
		if (!is_thread_entry) {
			prev_entry = curr_entry;
			curr_entry = curr_entry->next;
		}
    }
    // printf("DEBUG: %p\n", tid);
    // adj_list_debug();
}

/**
 * @brief Check if there is a cycle in the adjacency list from the thread tid
 * 
 * @param tid thread from which the cycle is checked
 * @param visited list of visited threads
 * @param rec_stack list of threads in the recursion stack
 * @return int 
 */
int has_cycle_rec(thread_t tid, int* visited, int* rec_stack) {
    int th_idx = get_thread_adj_list_idx(tid);
	adj_list_entry_t* th_adj_list_entry = get_thread_adj_list_entry(tid);
    if (th_idx == -1 || th_adj_list_entry == NULL) {
        return 0;
    }
    if (!visited[th_idx]) {
        // Mark this thread as visited and add it to the recursion stack
        visited[th_idx] = 1;
        rec_stack[th_idx] = 1;

        // Recursively check for cycles in the threads that this thread has joined
        node_t* curr_node = th_adj_list_entry->head_joined_threads_arr;
        while (curr_node != NULL) {
            if (curr_node->thread == NULL) {
                return 0;
            }
            thread_t neighbor_tid = curr_node->thread->thread;
            int th_nghbr_idx = get_thread_adj_list_idx(neighbor_tid);
            if (!visited[th_nghbr_idx] && has_cycle_rec(neighbor_tid, visited, rec_stack)) {
                return 1;
            } else if (rec_stack[th_nghbr_idx]) {
                return 1;
            }
            curr_node = curr_node->next;
        }
    }

    // Remove this thread from the recursion stack
    rec_stack[th_idx] = 0;
    return 0;
}


int has_cycle(thread_t tid) {
    // reset visited and rec_stack arrays
    memset(visited, 0, sizeof(visited));
    memset(rec_stack, 0, sizeof(rec_stack));
	return has_cycle_rec(tid, visited, rec_stack);
}

/**
 * @brief Free the adjacency list
 * 
 */
void free_adj_list() {
	adj_list_entry_t* curr_entry = adj_list_head;
	while (curr_entry != NULL) {
		node_t* curr_node = curr_entry->head_joined_threads_arr;
		while (curr_node != NULL) {
			node_t* prev_node = curr_node;
			curr_node = curr_node->next;
			free(prev_node);
		}
		adj_list_entry_t* prev_entry = curr_entry;
		curr_entry = curr_entry->next;
		free(prev_entry);
	}
}