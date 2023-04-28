#include <ucontext.h>
#include "thread.h"
#include "queue.h"
#include <valgrind/valgrind.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <signal.h>

enum status { RUNNING, FINISHED };

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

thread_t main_thread; // id of the main thread


typedef SIMPLEQ_HEAD(thread_queue_t, thread) head_t;
head_t head;

__attribute__((__constructor__)) void my_init() {
    head_t head_ = SIMPLEQ_HEAD_INITIALIZER(head);
    head = head_;
    thread_create(&main_thread, NULL, NULL);
    printf("[%p]INIT\n", thread_self());
}

void thread_debug(void) {
    struct thread *t;
    printf("[%p]DEBUGGING\n", thread_self());
    SIMPLEQ_FOREACH(t, &head, entry) {
        printf("[%p]%p running ? %s\n", thread_self(), t->thread, t->status == RUNNING ? "yes" : "no");
    }
    printf("[%p]END DEBUGGING\n\n", thread_self());
}

int len_queue(void) {
    struct thread *t;
    int len = 0;
    SIMPLEQ_FOREACH(t, &head, entry) {
        if (t->status == RUNNING) { // only count the running threads
            len++;
        }
    }
    return len;
}

struct thread *get_main_thread(void) {
    /* Assumes that main thread is still in queue */
    struct thread *t = SIMPLEQ_LAST(&head, thread, entry);
    while (t->thread != main_thread) {
        SIMPLEQ_REMOVE(&head, t, thread, entry);
        SIMPLEQ_INSERT_HEAD(&head, t, entry);
        t = SIMPLEQ_LAST(&head, thread, entry);
    }
    return t;
}

struct thread *get_last_running_queue_element() {
    /* get the last element that is running in the queue, all of the finished threads go back to the beginning of the queue */
    struct thread *last = SIMPLEQ_LAST(&head, thread, entry);
    while (last->status == FINISHED) {
        SIMPLEQ_REMOVE(&head, last, thread, entry);
        SIMPLEQ_INSERT_HEAD(&head, last, entry);
        last = SIMPLEQ_LAST(&head, thread, entry);
    }
    return last;
}

extern thread_t thread_self(void) {
    struct thread *last = SIMPLEQ_LAST(&head, thread, entry);
    return last->thread;
}

// Current thread placed at the beginning of the run queue (--> FIFO)
extern int thread_yield(void) {
    if (SIMPLEQ_EMPTY(&head)) {
        return -1;
    }

    // get the current thread
    struct thread *current = get_last_running_queue_element();
    if (len_queue() == 1) {
        // no need to yield if only one running thread in queue
        return EXIT_SUCCESS;
    }

    // remove the current thread from the queue
    SIMPLEQ_REMOVE(&head, current, thread, entry);

    if (SIMPLEQ_EMPTY(&head)) {
        // error if the queue becomes empty
        return -1;
    }
    SIMPLEQ_INSERT_HEAD(&head, current, entry); // add the current thread at the beginning of the queue


    // swap context with the next thread in the queue
    struct thread *next_executed_thread = get_last_running_queue_element();
    swapcontext(&current->uc, &next_executed_thread->uc);
    return EXIT_SUCCESS;
}

void meta_func(void *(*func)(void *), void *args, struct thread *current) {
    current->retval = func(args);
    // should only go here when the thread returns without using thread_exit
    if (len_queue() != 1) {
        current->status = FINISHED;
        struct thread *next_executed_thread = get_last_running_queue_element();
        setcontext(&next_executed_thread->uc);
    }
    printf("[%p] calling exit\n", thread_self());
    if (len_queue() == 1) {
        // if only one thread left in queue, exit
        if (thread_self() != main_thread) {
            // if that thread is not the main thread, return to the context of the main thread (just before exit(EXIT_SUCCESS)) in thread_exit
            struct thread *main_thread = get_main_thread();
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


    // add the thread to the queue
    SIMPLEQ_INSERT_HEAD(&head, new_thread_s, entry);
    return EXIT_SUCCESS;
}

extern int thread_join(thread_t thread, void **retval) {
    struct thread *current = get_last_running_queue_element();
    thread_t current_thread = current->thread;

    if (current_thread == thread) {
        printf("can't wait for itself\n");
        // can't wait for itself
        return -1;
    }


    struct thread *elm;
    int elm_found_bool = 0;
    SIMPLEQ_FOREACH(elm, &head, entry) {
        if (elm->thread == thread) {
            elm_found_bool = 1;
            break;
        }
    }

    if (elm_found_bool == 0) {
        printf("thread not found\n");
        // thread not found
        return -1;
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
    struct thread *current = get_last_running_queue_element();
    current->retval = retval;

    current->status = FINISHED;
    struct thread *next_executed_thread = get_last_running_queue_element();
    if (current->thread == main_thread) {
        // if main thread, swap context (will come back here when all threads are finished)
        swapcontext(&current->uc, &next_executed_thread->uc);
        exit(EXIT_SUCCESS);
    }
    setcontext(&next_executed_thread->uc);
}

__attribute__((__destructor__)) void my_end() {
    /* free all the threads */
    printf("[%p] destructor\n", thread_self());
    if (SIMPLEQ_EMPTY(&head)) {
        return;
    }
    while (!SIMPLEQ_EMPTY(&head)) {
        // remove first thread from queue
        struct thread *current = SIMPLEQ_LAST(&head, thread, entry);
        SIMPLEQ_REMOVE(&head, current, thread, entry);
        if (current->thread != main_thread) {
            // if not main thread, free the stack
            VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
            free(current->uc.uc_stack.ss_sp);
        }
        // free the thread structure
        free(current);
    }
}