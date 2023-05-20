#include "queue.h"
#include "thread.h"


#ifndef __UTILS_H__
#define __UTILS_H__

#define TRUE 1
#define FALSE 0

enum ERROR_TYPE {
    SUCCESS,
    INFO,
    WARN,
    DEBUGGING,
    CRITIC,
};


enum thread_status {
    RUNNING,
    FINISHED
};
enum mutex_status {
    UNLOCK,
    LOCK
};

struct thread {
    thread_t thread;
    ucontext_t uc;
    SIMPLEQ_ENTRY(thread)
        entry;
    void *(*func)(void *);
    void *funcarg;
    void *retval;
    enum thread_status status;
    int valgrind_stackid;
};

typedef SIMPLEQ_HEAD(thread_queue_t, thread) head_t;

extern head_t head_run_queue;
extern head_t head_sleep_queue;

extern int nb_run_queue_threads;
extern int nb_blocks;

extern thread_t main_thread; // id of the main thread

extern sigset_t sigprof;

/**
 * @brief Log messages
 *
 * @param type
 * @param message
 */
void log_message(const enum ERROR_TYPE type, const char message[], ...);

/**
 * exit printing error prefixed by `prefix` if `condition` is true (non zero)
 */
void exit_if(int condition, const char *prefix);

/**
 * @brief Get the length of the run queue
 *
 * @return int
 */
int len_run_queue(void);


/**
 * @brief Removes main thread from sleep queue and adds it to run queue
 *
 * @return struct thread* main thread
 */
struct thread *go_back_to_main_thread(void);

/**
 * @brief Get the first run queue element object. If the first element if FINISHED, it is removed from the run queue and added to the sleep queue
 *
 * @return struct thread* first running thread
 */
struct thread *get_first_run_queue_element(void);

/**
 * @brief Initialize the timer
 *
 * @return int success
 */
int init_timer(void);

/**
 * @brief Disarm the timer
 *
 * @return int success
 */
int disarm_timer(void);

/**
 * @brief Block the signal SIGPROF from being delivered
 *
 */
void block_sigprof(void);

/**
 * @brief Unblock the signal SIGPROF from being delivered
 *
 */
void unblock_sigprof(void);

/**
 * @brief Free memory allocated for the threads in the sleep queue
 *
 */
void free_sleep_queue(void);

/**
 * @brief Update the run queue and execute the next unlocked thread
 *
 */
int mutex_yield(thread_mutex_t *mutex);

/**
 * @brief Update the run queue and execute the next running thread
 *
 */
int internal_thread_yield(void);

/**
 * @brief Remove the head of the run queue
 *
 */
void remove_head_run_queue(void);

/**
 * @brief Insert a thread at the tail of the run queue
 *
 * @param thread
 */
void insert_tail_run_queue(struct thread *thread);

/**
 * @brief Insert a thread at the head of the run queue
 *
 * @param thread
 */
void insert_head_run_queue(struct thread *thread);


/**
 * @brief Remove a thread from the run queue
 *
 * @param thread
 */
void remove_run_queue(struct thread *thread_to_remove);

#endif //__UTILS_H__