#define _GNU_SOURCE
#include <ucontext.h>
#include "thread.h"
#include "queue.h"
#include <valgrind/valgrind.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>

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


typedef SIMPLEQ_HEAD(thread_queue_t, thread) head_t;
head_t head;

__attribute__((__constructor__)) void my_init() {
    head_t head_ = SIMPLEQ_HEAD_INITIALIZER(head);
    head = head_;
    // TO DO FREE THE MALLOC
    // thread_t *main_thread;
    thread_t *main_thread = malloc(sizeof(thread_t));
    thread_create(main_thread, NULL, NULL);

}

void thread_debug(void) {
    struct thread *t;
    printf("[%d]DEBUGGING\n", gettid());
    SIMPLEQ_FOREACH(t, &head, entry) {
        printf("[%d]%p running ? %s\n", gettid(), t->thread, t->status == RUNNING ? "yes" : "no");
    }
    printf("[%d]END DEBUGGING\n\n", gettid());
}

int len_queue(void) {
    struct thread *t;
    int len = 0;
    SIMPLEQ_FOREACH(t, &head, entry) {
        if (t->status == RUNNING) {
            len++;
        }
    }
    return len;
}

extern thread_t thread_self(void) {
    struct thread *last = SIMPLEQ_LAST(&head, thread, entry);
    return last->thread;
}

struct thread *get_last_running_queue_element() {
    struct thread *last = SIMPLEQ_LAST(&head, thread, entry);
    while (last->status == FINISHED) {
        SIMPLEQ_REMOVE(&head, last, thread, entry);
        SIMPLEQ_INSERT_HEAD(&head, last, entry);
        last = SIMPLEQ_LAST(&head, thread, entry);
    }
    return last;
}

// Current thread placed at the end of the run queue
extern int thread_yield(void) {
    if (SIMPLEQ_EMPTY(&head)) {
        return -1;
    }
    struct thread *current = get_last_running_queue_element();
    if (len_queue() == 1) {
        return EXIT_SUCCESS;
    }
    // thread_debug();


    SIMPLEQ_REMOVE(&head, current, thread, entry);

    if (SIMPLEQ_EMPTY(&head)) {
        return -1;
    }
    SIMPLEQ_INSERT_HEAD(&head, current, entry);



    struct thread *next_executed_thread = get_last_running_queue_element();
    swapcontext(&current->uc, &next_executed_thread->uc);
    return EXIT_SUCCESS;
}

void meta_func(void *(*func)(void *), void *args, void **res) {
    *res = func(args);
}

int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg) {
    // printf("[%d]THREAD CREATE\n", gettid());
    // thread_debug();
    // TODO : Free this malloc !!!
    struct thread *new_thread = malloc(sizeof(struct thread));
    new_thread->uc.uc_stack.ss_size = 64 * 1024;
    new_thread->uc.uc_stack.ss_sp = malloc(new_thread->uc.uc_stack.ss_size);
    new_thread->valgrind_stackid = VALGRIND_STACK_REGISTER(new_thread->uc.uc_stack.ss_sp, new_thread->uc.uc_stack.ss_sp + new_thread->uc.uc_stack.ss_size);
    // new_thread->thread = malloc(sizeof(thread_t));
    new_thread->thread = newthread;
    *newthread = newthread;
    new_thread->status = RUNNING;

    if (func != NULL) { // if not main thread
        if (getcontext(&(new_thread->uc)) == -1) {
            VALGRIND_STACK_DEREGISTER(new_thread->valgrind_stackid);
            free(new_thread->uc.uc_stack.ss_sp);
            free(new_thread);
            return -1;
        }
        new_thread->retval = malloc(sizeof(void *));

        makecontext(&new_thread->uc, (void (*)(void)) meta_func, 3, func, funcarg, new_thread->retval);

        new_thread->func = func;
        new_thread->funcarg = funcarg;
    } else {
        // getcontext(&(new_thread->uc));
    }



    SIMPLEQ_INSERT_HEAD(&head, new_thread, entry);

    new_thread->uc.uc_link = NULL;
    if (len_queue() > 1) {
        SIMPLEQ_NEXT(new_thread, entry)->uc.uc_link = &new_thread->uc;
    }

    // thread_debug();
    // if (func != NULL) {
    //     struct thread *current = get_last_running_queue_element();
    //     swapcontext(&current->uc, &new_thread->uc);
    // }
    // printf("[%d]END THREAD CREATE\n", gettid());
    return EXIT_SUCCESS;
}

extern int thread_join(thread_t thread, void **retval) {
    // printf("[%d]BEGINNING JOIN\n", gettid());
    // thread_debug();
    struct thread *current = get_last_running_queue_element();
    thread_t current_thread = current->thread;
    // printf("[%d]CURRENT THREAD : %p\n", gettid(), current_thread);
    // printf("[%d]THREAD : %p\n", gettid(), thread);

    if (current_thread == thread) {
        // printf("can't wait for itself\n");
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
        // printf("thread not found\n");
        // thread not found
        return -1;
    }
    // SIMPLEQ_REMOVE(&head, current, thread, entry);
    // if (elm != NULL) {
    //     SIMPLEQ_INSERT_AFTER(&head, elm, current, entry);
    // }
    // printf("[%d]SECOND REMOVE\n", gettid());
    // SIMPLEQ_REMOVE(&head, elm, thread, entry);
    // SIMPLEQ_INSERT_AFTER(&head, current, elm, entry);

    while (elm->status != FINISHED) {
        // printf("[%d]WAITING FOR THREAD %p\n", gettid(), thread);
        thread_yield();
    }
    // printf("[%d]retval = %p\n", gettid(), retval);
    if (retval != NULL) {
        // printf("RETVAL NOT NULL\n");
        // printf("[%d]elm->retval = %p\n", gettid(), elm->retval);
        *retval = elm->retval;
        // printf("[%d]retval = %p\n", gettid(), *retval);
    }

    return EXIT_SUCCESS;
}

extern void thread_exit(void *retval) {
    // printf("[%d]THREAD EXIT\n", gettid());
    struct thread *current = get_last_running_queue_element();
    // printf("[%d]THREAD EXIT : RETURN %p\n", gettid(), retval);
    current->retval = retval;
    // printf("[%d]THREAD EXIT : RETURN %p\n", gettid(), current->retval);
    // SIMPLEQ_REMOVE(&head, current, thread, entry);
    // VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
    // free(current);
    // thread_debug();
    current->status = FINISHED;
    struct thread *next_executed_thread = get_last_running_queue_element();
    setcontext(&next_executed_thread->uc);
    // swapcontext(&current->uc, &next_executed_thread->uc);
    // printf("[%d]END THREAD EXIT\n", gettid());
    // exit(0);
}