#include <ucontext.h>
#include "thread.h"
#include "queue.h"
#include <valgrind/valgrind.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <signal.h>
#include <sys/time.h>

enum status { RUNNING, FINISHED };
enum m_status { UNLOCK, LOCK };
#define MAX_THREADS 1000

struct thread {
    thread_t thread;
    ucontext_t uc;
    SIMPLEQ_ENTRY(thread)
        entry;
    void *(*func)(void *);
    void *funcarg;
    void *retval;
    enum status status;
    int valgrind_stackid;
};

typedef struct {
    thread_mutex_t mutex;
    volatile int lock_flag;
} extended_mutex;

thread_t main_thread; // id of the main thread

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

volatile adj_list_entry_t* adj_list_head = NULL;
volatile int visited[MAX_THREADS] = {0};
volatile int rec_stack[MAX_THREADS] = {0};

int locker = 0;

typedef SIMPLEQ_HEAD(thread_queue_t, thread) head_t;
head_t head_run_queue;
head_t head_sleep_queue;

__attribute__((__constructor__)) void my_init() {
    head_t head_run_queue_tmp = SIMPLEQ_HEAD_INITIALIZER(head_run_queue);
    head_run_queue = head_run_queue_tmp;
    head_t head_sleep_queue_tmp = SIMPLEQ_HEAD_INITIALIZER(head_sleep_queue);
    head_sleep_queue = head_sleep_queue_tmp;
    thread_create(&main_thread, NULL, NULL);
}

void thread_debug(void) {
    struct thread *t;
    printf("[%p]DEBUGGING\n", thread_self());
    SIMPLEQ_FOREACH(t, &head_run_queue, entry) {
        // printf("[%p] %p \n", thread_self(), t->thread);
        printf("[%p]%p running ? %s\n", thread_self(), t->thread, t->status == RUNNING ? "yes" : "no");
    }
    printf("[%p]END DEBUGGING\n\n", thread_self());
}

int len_run_queue(void) {
    struct thread *t;
    int len = 0;
    SIMPLEQ_FOREACH(t, &head_run_queue, entry) {
        if (t->status == RUNNING) { // only count the running threads
            len++;
        }
    }
    return len;
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
    SIMPLEQ_FOREACH(elm, &head_run_queue, entry) {
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
	adj_list_entry_t* curr_entry = (adj_list_entry_t*) adj_list_head;
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
	adj_list_entry_t* curr_entry = (adj_list_entry_t*) adj_list_head;
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
		new_adj_list_entry->next = (adj_list_entry_t*) adj_list_head;
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
	adj_list_entry_t* curr_entry = (adj_list_entry_t*) adj_list_head;
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

				// adj_list_entry_t* adj_list_entry_to_free = curr_entry;
				// curr_entry = curr_entry->next;
				// free(adj_list_entry_to_free);
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
}

/**
 * @brief Check if there is a cycle in the adjacency list from the thread tid
 * 
 * @param tid thread from which the cycle is checked
 * @param visited list of visited threads
 * @param rec_stack list of threads in the recursion stack
 * @return int 
 */
int has_cycle(thread_t tid, volatile int* visited, volatile int* rec_stack) {
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
            // if (th_nghbr_idx == -1) {
            //     return 0;
            // }
            if (!visited[th_nghbr_idx] && has_cycle(neighbor_tid, visited, rec_stack)) {
                rec_stack[th_nghbr_idx] = 0;
                visited[th_nghbr_idx] = 0;
                return 1;
            } else if (rec_stack[th_nghbr_idx]) {
                rec_stack[th_nghbr_idx] = 0;
                visited[th_nghbr_idx] = 0;
                return 1;
            }
            curr_node = curr_node->next;
        }
    }

    // Remove this thread from the recursion stack
    rec_stack[th_idx] = 0;
    visited[th_idx] = 0;
    return 0;
}

/**
 * @brief Free the adjacency list
 * 
 */
void free_adj_list() {
	adj_list_entry_t* curr_entry = (adj_list_entry_t*) adj_list_head;
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

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

struct thread *go_back_to_main_thread(void) {
    /* Assumes that main thread is still in queue */
    struct thread *main_thread = SIMPLEQ_FIRST(&head_sleep_queue);
    SIMPLEQ_REMOVE_HEAD(&head_sleep_queue, entry); // remove main thread from the sleep queue
    SIMPLEQ_INSERT_HEAD(&head_run_queue, main_thread, entry); // insert it into the run queue
    main_thread->status = RUNNING; // mark it as running
    return main_thread;
}

struct thread *get_first_run_queue_element(void) {
    /* get the first element that is running in the queue, all of the finished threads go back to the beginning of the queue */
    struct thread *first = SIMPLEQ_FIRST(&head_run_queue);
    while (first->status == FINISHED) {
        SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry); // remove finished thread from the run queue
        if (first->thread == main_thread) {
            // if the main thread is finished, we need to keep it in an accessible place (--> end of sleep queue)
            SIMPLEQ_INSERT_HEAD(&head_sleep_queue, first, entry);
        } else {
            SIMPLEQ_INSERT_TAIL(&head_sleep_queue, first, entry); // insert it into the sleep queue
        }
        first = SIMPLEQ_FIRST(&head_run_queue); // get the new first element of the run queue
    }
    return first;
}

void sigprof_handler(int signum, siginfo_t *siginfo, void *context) {
// void sigprof_handler(int signum) {
    /* Handler for SIGPROF signal */
    printf("SIGPROF HANDLER\n");
    thread_yield();
}

int init_timer(void) {
    /* Every 10 ms of thread execution, a SIGPROF signal is sent */
    struct sigaction sa;
    sigset_t all;
    sigfillset(&all); // check later : should only handle SIGPROF

    sa.sa_sigaction = sigprof_handler;
    // sigemptyset(&sa.sa_mask);
    sa.sa_mask = all;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    struct sigaction old_sigaction;
    if (sigaction(SIGPROF, &sa, &old_sigaction) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 10000; // 10 miliseconds
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 1; // arms the timer as soon as possible

    printf("TIMER INTIALIZED\n");

    // Enable timer
    if (setitimer(ITIMER_PROF, &timer, NULL) == -1) {
        if (sigaction(SIGPROF, &old_sigaction, NULL) == -1) {
            perror("sigaction");
            return EXIT_FAILURE;
        }
        return EXIT_FAILURE;
    }

    printf("TIMER ENABLED\n");
    return EXIT_SUCCESS;
}

extern thread_t thread_self(void) {
    struct thread *first = SIMPLEQ_FIRST(&head_run_queue);
    return first->thread;
}

// Current thread placed at the beginning of the run queue (--> FIFO)
extern int thread_yield(void) {
    if (SIMPLEQ_EMPTY(&head_run_queue)) {
        return -1;
    }

    // get the current thread
    struct thread *current = get_first_run_queue_element();
    if (len_run_queue() == 1) {
        // no need to yield if only one running thread in queue
        return EXIT_SUCCESS;
    }

    // remove the current thread from the queue
    SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);

    if (SIMPLEQ_EMPTY(&head_run_queue)) {
        // error if the queue becomes empty
        return -1;
    }
    SIMPLEQ_INSERT_TAIL(&head_run_queue, current, entry); // add the current thread at the beginning of the queue


    // swap context with the next thread in the queue
    struct thread *next_executed_thread = get_first_run_queue_element();
    swapcontext(&current->uc, &next_executed_thread->uc);
    return EXIT_SUCCESS;
}

void meta_func(void *(*func)(void *), void *args, struct thread *current) {
    current->retval = func(args);
    // removing the thread from the adjacency list
    remove_edge_when_finished(current->thread);
    // should only go here when the thread returns without using thread_exit
    if (len_run_queue() != 1) {
        current->status = FINISHED;
        struct thread *next_executed_thread = get_first_run_queue_element();
        setcontext(&next_executed_thread->uc);
    }
    if (len_run_queue() == 1) {
        // if only one thread left in queue, exit
        if (thread_self() != main_thread) {
            // if that thread is not the main thread, return to the context of the main thread (just before exit(EXIT_SUCCESS)) in thread_exit
            struct thread *main_thread = go_back_to_main_thread();
            setcontext(&main_thread->uc);
        }
        exit(EXIT_SUCCESS);
    }
    exit(EXIT_SUCCESS);
}

int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg) {
    // thread_debug();
    // TODO : Free this malloc !!!
    struct thread *new_thread_s = malloc(sizeof(struct thread));
    new_thread_s->thread = newthread;
    *newthread = newthread;
    new_thread_s->status = RUNNING;

    if (func != NULL) { // if not main thread
        if (getcontext(&(new_thread_s->uc)) == -1) {
            // if error in getting context
            VALGRIND_STACK_DEREGISTER(new_thread_s->valgrind_stackid);
            free(new_thread_s->uc.uc_stack.ss_sp);
            free(new_thread_s);
            return -1;
        }

        // initialize stack + create the thread with makecontext
        new_thread_s->uc.uc_stack.ss_size = SIGSTKSZ;
        new_thread_s->uc.uc_stack.ss_sp = malloc(new_thread_s->uc.uc_stack.ss_size);
        new_thread_s->valgrind_stackid = VALGRIND_STACK_REGISTER(new_thread_s->uc.uc_stack.ss_sp, new_thread_s->uc.uc_stack.ss_sp + new_thread_s->uc.uc_stack.ss_size);
        makecontext(&new_thread_s->uc, (void (*)(void)) meta_func, 3, func, funcarg, new_thread_s);

    }
    new_thread_s->func = func;
    new_thread_s->funcarg = funcarg;

    // if (init_timer() == EXIT_FAILURE) {
    //     return EXIT_FAILURE;
    // }


    // add the thread to the queue
    SIMPLEQ_INSERT_TAIL(&head_run_queue, new_thread_s, entry);
    return EXIT_SUCCESS;
}

extern int thread_join(thread_t thread, void **retval) {
    struct thread *current = get_first_run_queue_element();
    thread_t current_thread = current->thread;
    if (current_thread == thread) {
        printf("can't wait for itself\n");
        // can't wait for itself
        return -1;
    }


    struct thread *elm;
    int elm_found_bool = 0;
    // look for the thread in the run queue
    SIMPLEQ_FOREACH(elm, &head_run_queue, entry) {
        if (elm->thread == thread) {
            elm_found_bool = 1;
            break;
        }
    }

    // look for the thread in the sleep queue
    if (elm_found_bool == 0) {
        SIMPLEQ_FOREACH(elm, &head_sleep_queue, entry) {
            if (elm->thread == thread) {
                elm_found_bool = 1;
                break;
            }
        }
    }

    if (elm_found_bool == 0) {
        printf("thread not found\n");
        // thread not found
        return -1;
    }

    add_edge(current_thread, thread);
    if (has_cycle(current_thread, visited, rec_stack)) {
        printf("cycle detected\n");
        remove_edge(current_thread, thread);
        return 35;
    }
    while (elm->status != FINISHED) {
        // waiting for the thread to finish
        assert(!thread_yield());
    }

    if (retval != NULL) {
        //store return value
        *retval = elm->retval;
    }

    return EXIT_SUCCESS;
}

extern void thread_exit(void *retval) {
    /* Mark the thread as finished and switch context to newt thread */
    struct thread *current = get_first_run_queue_element();
    current->retval = retval;

    current->status = FINISHED;
    // removing the thread from the adjacency list
    remove_edge_when_finished(current->thread);
    struct thread *next_executed_thread = get_first_run_queue_element();
    if (current->thread == main_thread) {
        // if main thread, swap context (will come back here when all threads are finished)
        swapcontext(&current->uc, &next_executed_thread->uc);
        exit(EXIT_SUCCESS);
    }
    setcontext(&next_executed_thread->uc);
}

void free_sleep_queue() {
    while (!SIMPLEQ_EMPTY(&head_sleep_queue)) {
        struct thread *current = SIMPLEQ_FIRST(&head_sleep_queue);
        SIMPLEQ_REMOVE_HEAD(&head_sleep_queue, entry);
        VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
        free(current->uc.uc_stack.ss_sp);
        free(current);
    }
}

__attribute__((__destructor__)) void my_end() {
    /* free all the threads */
    free_sleep_queue();
    if (SIMPLEQ_EMPTY(&head_run_queue)) {
        return;
    }
    while (!SIMPLEQ_EMPTY(&head_run_queue)) {
        // remove first thread from queue
        struct thread *current = SIMPLEQ_FIRST(&head_run_queue);
        SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);
        if (current->thread != main_thread) {
            // if not main thread, free the stack
            VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
            free(current->uc.uc_stack.ss_sp);
        }
        // free the thread structure
        free(current);
    }
    free_adj_list();
}

int thread_mutex_init(thread_mutex_t *mutex) {
    mutex->dummy = UNLOCK;
    return EXIT_SUCCESS;
}


int thread_mutex_destroy(thread_mutex_t *mutex) {
    return EXIT_SUCCESS;
}

int thread_mutex_lock(thread_mutex_t *mutex) {
    while (mutex->dummy == 1) {
        thread_yield();
    }
    mutex->dummy = 1;
    return EXIT_SUCCESS;
}

int thread_mutex_unlock(thread_mutex_t *mutex) {
    mutex->dummy = 0;
    return EXIT_SUCCESS;
}