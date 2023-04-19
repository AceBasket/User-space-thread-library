#include <ucontext.h>
#include "thread.h"
#include "queue.h"
#include <valgrind/valgrind.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

struct thread {
    thread_t *thread;
    ucontext_t uc;
    SIMPLEQ_ENTRY(thread)
        entry;
    void *(*func)(void *);
    void *funcarg;
    void **retval;
    int valgrind_stackid;
};

typedef SIMPLEQ_HEAD(thread_queue_t, thread) head_t;
head_t head;

__attribute__((__constructor__)) void my_init() {
    head_t head_ = SIMPLEQ_HEAD_INITIALIZER(head);
    head = head_;
    // TO DO FREE THE MALLOC
    thread_t *main_thread = malloc(sizeof(thread_t));
    thread_create(main_thread, NULL, NULL);

}

void thread_debug(void) {
    struct thread *t;
    printf("DEBUGGING\n");
    SIMPLEQ_FOREACH(t, &head, entry) {
        // printf("%p\n", t->thread);
        printf("%p\n", t);
    }
    printf("END DEBUGGING\n\n");
}

int len_queue(void) {
    struct thread *t;
    int len = 0;
    // thread_debug();
    // printf("BEGIN LEN\n");
    // printf("head first = %p\n", *(&head.sqh_first));
    // printf("head last = %p\n", **(&head.sqh_last));
    SIMPLEQ_FOREACH(t, &head, entry) {
        // printf("LEN QUEUE, len = %d\n", len);
        // printf("t = %p\n", t);
        // printf("t->entry = %p\n", t->entry);
        // printf("t->entry.next = %p\n", (*(&t->entry.sqe_next)));
        // if (len > 5) exit(0);
        len++;
    }
    // printf("END LEN\n");
    return len;
}

extern thread_t thread_self(void) {
    struct thread *last = SIMPLEQ_LAST(&head, thread, entry);
    return last->thread;
}

struct thread *get_last_queue_element() {
    struct thread *last = SIMPLEQ_LAST(&head, thread, entry);
    return last;
}

// Current thread placed at the end of the run queue
extern int thread_yield(void) {
    // printf("0 head last = %p\n", **(&head.sqh_last));
    if (SIMPLEQ_EMPTY(&head)) {
        return -1;
    }
    struct thread *current = get_last_queue_element();
    if (len_queue() == 1) {
        return EXIT_SUCCESS;
    }
    // printf("current queue before switching: \n");
    // thread_debug();
    // printf("1 head last = %p\n", **(&head.sqh_last));


    SIMPLEQ_REMOVE(&head, current, thread, entry);
    // printf("2 head last = %p\n", **(&head.sqh_last));

    if (SIMPLEQ_EMPTY(&head)) {
        return -1;
    }
    // printf("TAIL REMOVED\n");
    // printf("3 head last = %p\n", **(&head.sqh_last));

    struct thread *next_executed_thread = get_last_queue_element();
    // printf("4 head last = %p\n", **(&head.sqh_last));
    // getcontext(&uc_current);
    // printf("GOT CONTEXT\n");
    // uc_current.uc_stack.ss_size = 64 * 1024;
    // uc_current.uc_stack.ss_sp = malloc(uc_current.uc_stack.ss_size);

    SIMPLEQ_INSERT_HEAD(&head, current, entry);
    // printf("TAIL ADDED TO HEAD\n");
    // printf("5 head last = %p\n", **(&head.sqh_last));

    // printf("current queue after switching: \n");
    // thread_debug();
    // uc_current.uc_link = NULL;
    // SIMPLEQ_NEXT(current, entry)->uc.uc_link = &uc_current;
    // printf("BEFORE CONTEXT SWAPPED\n");
    // printf("next_executed_thread = %p\n", next_executed_thread);
    swapcontext(&current->uc, &next_executed_thread->uc);
    // printf("6 head last = %p\n", **(&head.sqh_last));
    // printf("CONTEXT SWAPPED\n");

    // SIMPLEQ_REMOVE(&head, previous, thread, entry);
    // SIMPLEQ_INSERT_TAIL(&head, previous, entry);
    // thread_debug();
    return EXIT_SUCCESS;
}

void meta_func(void *(*func)(void *), void *args, void **res) {
    *res = func(args);
}

int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg) {
    // TODO : Free this malloc !!!
    struct thread *new_thread = malloc(sizeof(struct thread));
    new_thread->uc.uc_stack.ss_size = 64 * 1024;
    new_thread->uc.uc_stack.ss_sp = malloc(new_thread->uc.uc_stack.ss_size);
    new_thread->valgrind_stackid = VALGRIND_STACK_REGISTER(new_thread->uc.uc_stack.ss_sp, new_thread->uc.uc_stack.ss_sp + new_thread->uc.uc_stack.ss_size);
    new_thread->thread = newthread;

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



    // printf("-1 head last = %p\n", **(&head.sqh_last));
    SIMPLEQ_INSERT_HEAD(&head, new_thread, entry);

    new_thread->uc.uc_link = NULL;
    // printf("THREAD CREATE\n");
    if (len_queue() > 1) {
        SIMPLEQ_NEXT(new_thread, entry)->uc.uc_link = &new_thread->uc;
    }

    // struct thread *current = get_last_queue_element();
    // swapcontext(&current->uc, &new_thread->uc);

    // thread_debug();
    return EXIT_SUCCESS;
}

extern int thread_join(thread_t thread, void **retval) {
    printf("BEGINNING JOIN");
    struct thread *current = get_last_queue_element();
    thread_t current_thread = current->thread;
    if (current_thread == thread) {
        // can't wait for itself
        return -1;
    }
    struct thread *elm;
    SIMPLEQ_FOREACH(elm, &head, entry) {
        if (elm->thread == thread) {
            break;
        }
    }
    SIMPLEQ_REMOVE(&head, current, thread, entry);
    if (elm != NULL) {
        SIMPLEQ_INSERT_AFTER(&head, elm, current, entry);
    }
    printf("SECOND REMOVE");
    SIMPLEQ_REMOVE(&head, elm, thread, entry);
    SIMPLEQ_INSERT_AFTER(&head, current, elm, entry);
    if (retval != NULL) {
        printf("RETVAL NOT NULL");
        retval = elm->retval;
    }
    return EXIT_SUCCESS;
}

extern void thread_exit(void *retval) {
    printf("THREAD EXIT\n");
    struct thread *current = get_last_queue_element();
    retval = *current->retval;
    SIMPLEQ_REMOVE(&head, current, thread, entry);
    struct thread *next_executed_thread = get_last_queue_element();
    swapcontext(&current->uc, &next_executed_thread->uc);
    VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
    free(current);
    // exit(0);
}