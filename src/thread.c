#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <valgrind/valgrind.h>
#include "adj_list.h"
#include "thread_struct.h"
#include "utils.h"

int nb_total_threads = 0;

__attribute__((__constructor__)) void my_init()
{
    /* Initializing queues */
    head_t head_run_queue_tmp = SIMPLEQ_HEAD_INITIALIZER(head_run_queue);
    head_run_queue = head_run_queue_tmp;
    head_t head_sleep_queue_tmp = SIMPLEQ_HEAD_INITIALIZER(head_sleep_queue);
    head_sleep_queue = head_sleep_queue_tmp;

#ifndef ALLOW_DEADLOCK
    adj_list_init(&head_run_queue);
#endif
    /* Initializing signal mask */
    sigemptyset(&sigprof);
    sigaddset(&sigprof, SIGPROF);

    thread_create(&main_thread, NULL, NULL);
#ifdef DEBUG
    log_message(DEBUGGING, "main thread id", main_thread);
#endif
}

#ifdef DEBUG
void thread_debug(void)
{
    struct thread *t;
    log_message(DEBUGGING, "[%p]DEBUGGING", thread_self());
    SIMPLEQ_FOREACH(t, &head_run_queue, entry)
    {
        log_message(DEBUGGING, "[%p] %p", thread_self(), t->thread);
        log_message(DEBUGGING, "[%p]%p running ? %s", thread_self(), t->thread, t->status == RUNNING ? "yes" : "no");
    }
    log_message(DEBUGGING, "[%p]END DEBUGGING", thread_self());
}
#endif

extern thread_t thread_self(void)
{
    struct thread *first = get_first_run_queue_element();
    return first->thread;
}

void meta_func(void *(*func)(void *), void *args, struct thread *current)
{
    /* function that is called by makecontext */
#ifdef PREEMPTION
    assert(nb_blocks == 1);
    unblock_sigprof(); // unblock SIGPROF signal --> signal est blocké puisqu'on arrive après un swapcontext qui bloque le signal
#endif
    thread_exit(func(args)); // exit the thread
}

int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg)
{
#ifdef PREEMPTION
    block_sigprof();
#endif
    struct thread *new_thread_s = malloc(sizeof(struct thread));
    new_thread_s->thread = (thread_t)new_thread_s;
    *newthread = new_thread_s->thread;

#ifdef DEBUG
    log_message(DEBUGGING, "new thread id: %p", new_thread_s->thread);
#endif

    new_thread_s->status = RUNNING;
    new_thread_s->retval = NULL;
    getcontext(&new_thread_s->uc);

    if (func != NULL)
    {
        // if not main thread

        // initialize stack + create the thread with makecontext
        new_thread_s->uc.uc_stack.ss_size = SIGSTKSZ;
        new_thread_s->uc.uc_stack.ss_sp = malloc(new_thread_s->uc.uc_stack.ss_size);
        new_thread_s->valgrind_stackid = VALGRIND_STACK_REGISTER(new_thread_s->uc.uc_stack.ss_sp, new_thread_s->uc.uc_stack.ss_sp + new_thread_s->uc.uc_stack.ss_size);
#ifdef PREEMPTION
        unblock_sigprof();
#endif
        makecontext(&new_thread_s->uc, (void (*)(void))meta_func, 3, func, funcarg, new_thread_s);
#ifdef PREEMPTION
        block_sigprof();
#endif
    }
    new_thread_s->func = func;
    new_thread_s->funcarg = funcarg;

    insert_tail_run_queue(new_thread_s);
    nb_total_threads++;
#ifdef PREEMPTION
    unblock_sigprof();
    if (nb_total_threads == 2)
    {
        // if it is the second thread created, initialize the timer
        // Useless for only one thread
        if (init_timer() == -1)
        {
            log_message(CRITIC, "init_timer failed\n");
            return EXIT_FAILURE;
        }
    }
#endif

    return EXIT_SUCCESS;
}

extern int thread_yield(void)
{
#ifdef PREEMPTION
    block_sigprof();
#endif
    int res = internal_thread_yield();
#ifdef PREEMPTION
    unblock_sigprof();
#endif
    return res;
}

extern int thread_join(thread_t thread, void **retval)
{
#ifdef DEBUG
    log_message(DEBUGGING, "[%p] thread_join", thread_self());
#endif
#ifdef PREEMPTION
    block_sigprof();
#endif
    thread_t current_thread = thread_self();

    if (current_thread == thread)
    {
// can't wait for itself
#ifdef PREEMPTION
        unblock_sigprof();
#endif
        return -1;
    }

    struct thread *elm;
    int elm_found_bool = 0;
    // look for the thread in the run queue
    SIMPLEQ_FOREACH(elm, &head_run_queue, entry)
    {
        if (elm->thread == thread)
        {
            elm_found_bool = 1;
            break;
        }
    }

    // look for the thread in the sleep queue
    if (elm_found_bool == 0)
    {
        SIMPLEQ_FOREACH(elm, &head_sleep_queue, entry)
        {
            if (elm->thread == thread)
            {
                elm_found_bool = 1;
                break;
            }
        }
    }

    if (elm_found_bool == 0)
    {
#ifdef DEBUG
        log_message(DEBUGGING, "thread not found");
#endif
#ifdef PREEMPTION
        unblock_sigprof();
#endif
        // thread not found
        return -1;
    }

#ifndef ALLOW_DEADLOCK
    add_edge(current_thread, thread);
    if (has_cycle(current_thread))
    {
        log_message(CRITIC, "cycle detected when trying to join %p with %p", current_thread, thread);
        remove_edge(current_thread, thread);
        return 35;
    }
#endif

    while (elm->status != FINISHED)
    {
        // waiting for the thread to finish
        if (internal_thread_yield() == -1)
        {
            assert(0);
        }
    }

    if (retval != NULL)
    {
        // store return value
        *retval = elm->retval;
    }
#ifdef PREEMPTION
    unblock_sigprof();
#endif
    return EXIT_SUCCESS;
}

extern void thread_exit(void *retval)
{
#ifdef DEBUG
    thread_t th_debug = thread_self();
    log_message(DEBUGGING, "[%p] thread_exit", th_debug);
#endif
/* Mark the thread as finished and switch context to newt thread */
#ifdef PREEMPTION
    block_sigprof();
#endif
    struct thread *current = get_first_run_queue_element();
    current->retval = retval;
    current->status = FINISHED;

#ifdef DEBUG
    log_message(DEBUGGING, "thread %p finished", current->thread);
#endif

#ifndef ALLOW_DEADLOCK
    // removing the thread from the adjacency list
    remove_edge_when_finished(current->thread);
#endif
    struct thread *next_executed_thread;

    /* Is the current thread the last one running + exiting while not being the main ? */
    if (len_run_queue() == 1 && current->thread != main_thread)
    {
        next_executed_thread = go_back_to_main_thread();
    }
    else
    {
        next_executed_thread = get_first_run_queue_element();
    }

#ifdef PREEMPTION
    assert(nb_blocks == 1);
#endif
    swapcontext(&current->uc, &next_executed_thread->uc);
#ifdef PREEMPTION
    assert(nb_blocks == 1);
#endif

    /* If the current thread is main, exit */
    if (len_run_queue() == 1 && current->thread == main_thread)
    {
#ifdef PREEMPTION
        assert(nb_blocks == 1);
#endif
        exit(EXIT_SUCCESS);
    }
}

__attribute__((__destructor__)) void my_end()
{
/* free all the threads */
#ifdef PREEMPTION
    disarm_timer();
#endif
    free_sleep_queue();
    if (SIMPLEQ_EMPTY(&head_run_queue))
    {
        return;
    }
    while (!SIMPLEQ_EMPTY(&head_run_queue))
    {
        // remove first thread from queue
        struct thread *current = SIMPLEQ_FIRST(&head_run_queue);
        remove_head_run_queue();
        if (current->thread != main_thread)
        {
            // if not main thread, free the stack
            VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
            free(current->uc.uc_stack.ss_sp);
        }
        // free the thread structure
        free(current);
    }
#ifndef ALLOW_DEADLOCK
    free_adj_list();
#endif
}

int thread_mutex_init(thread_mutex_t *mutex)
{
#ifdef PREEMPTION
    block_sigprof();
#endif
    mutex->locker = NULL;
    mutex->status = UNLOCK;
#ifdef DEBUG
    log_message(DEBUGGING, "mutex initialized [%p]", mutex);
#endif
#ifdef PREEMPTION
    unblock_sigprof();
#endif
    return EXIT_SUCCESS;
}

int thread_mutex_destroy(thread_mutex_t *mutex)
{
#ifdef DEBUG
    log_message(DEBUGGING, "mutex destroyed [%p]", mutex);
#endif
    (void)mutex;
    return EXIT_SUCCESS;
}

int thread_mutex_lock(thread_mutex_t *mutex)
{
#ifdef PREEMPTION
    block_sigprof();
#endif
    while (mutex->status == 1)
    {
        mutex_yield(mutex);
    }
    mutex->status = 1;
    mutex->locker = get_first_run_queue_element();
#ifdef PREEMPTION
    unblock_sigprof();
#endif
    return EXIT_SUCCESS;
}

int thread_mutex_unlock(thread_mutex_t *mutex)
{
#ifdef PREEMPTION
    block_sigprof();
#endif
    mutex->status = 0;
    mutex->locker = NULL;
#ifdef PREEMPTION
    unblock_sigprof();
#endif
    return EXIT_SUCCESS;
}
