#include <ucontext.h>
#include "thread.h"
#include "queue.h"
#include <valgrind/valgrind.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

struct thread
{
    thread_t *thread;
    ucontext_t uc;
    ucontext_t previous;
    SIMPLEQ_ENTRY(thread)
    entry;
    void *(*func)(void *);
    void *funcarg;
    void **retval;
    int valgrind_stackid;
};

typedef SIMPLEQ_HEAD(thread_queue_t, thread) head_t;
head_t head = SIMPLEQ_HEAD_INITIALIZER(head);

extern thread_t thread_self(void)
{
    struct thread *last = SIMPLEQ_LAST(&head, thread, entry);
    return last->thread;
}

struct thread *get_last_queue_element()
{
    struct thread *last = SIMPLEQ_LAST(&head, thread, entry);
    return last;
}

extern int thread_yield(void)
{
    if (SIMPLEQ_EMPTY(&head))
    {
        return -1;
    }
    struct thread *current = get_last_queue_element();
    ucontext_t uc_current = current->uc;
    SIMPLEQ_REMOVE(&head, current, thread, entry);
    if (SIMPLEQ_EMPTY(&head))
    {
        return -1;
    }
    struct thread *previous = get_last_queue_element();
    getcontext(&uc_current);
    uc_current.uc_stack.ss_size = 64 * 1024;
    uc_current.uc_stack.ss_sp = malloc(uc_current.uc_stack.ss_size);
    uc_current.uc_link = &previous->uc;

    swapcontext(&previous->uc, &uc_current);
    SIMPLEQ_REMOVE(&head, previous, thread, entry);
    SIMPLEQ_INSERT_TAIL(&head, current, entry);
    SIMPLEQ_INSERT_TAIL(&head, previous, entry);
    return EXIT_SUCCESS;
}

void meta_func(void *(*func)(void *), void *args, void *res)
{
    res = func(args);
}

int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg)
{
    // TODO : Free this malloc !!!
    struct thread *new_thread = malloc(sizeof(struct thread));
    new_thread->uc.uc_stack.ss_size = 64 * 1024;
    new_thread->uc.uc_stack.ss_sp = malloc(new_thread->uc.uc_stack.ss_size);
    new_thread->valgrind_stackid = VALGRIND_STACK_REGISTER(new_thread->uc.uc_stack.ss_sp, new_thread->uc.uc_stack.ss_sp + new_thread->uc.uc_stack.ss_size);
    new_thread->thread = newthread;
    if (getcontext(&(new_thread->uc)) == -1)
    {
        VALGRIND_STACK_DEREGISTER(new_thread->valgrind_stackid);
        free(new_thread->uc.uc_stack.ss_sp);
        free(new_thread);
        return -1;
    };

    new_thread->uc.uc_link = &(new_thread->previous);
    makecontext(&new_thread->uc, (void (*)(void))meta_func, 1, funcarg);

    new_thread->func = func;
    new_thread->funcarg = funcarg;

    SIMPLEQ_INSERT_HEAD(&head, new_thread, entry);

    return EXIT_SUCCESS;
}

extern int thread_join(thread_t thread, void **retval)
{
    printf("BEGINNING");
    struct thread *current = get_last_queue_element();
    thread_t current_thread = current->thread;
    if (current_thread == thread)
    {
        // can't wait for itself
        return -1;
    }
    struct thread *elm;
    SIMPLEQ_FOREACH(elm, &head, entry)
    {
        if (elm->thread == thread)
        {
            break;
        }
    }
    SIMPLEQ_REMOVE(&head, current, thread, entry);
    if (elm != NULL)
    {
        SIMPLEQ_INSERT_AFTER(&head, elm, current, entry);
    }
    printf("SECOND REMOVE");
    SIMPLEQ_REMOVE(&head, elm, thread, entry);
    SIMPLEQ_INSERT_AFTER(&head, current, elm, entry);
    if (retval != NULL)
    {
        printf("RETVAL NOT NULL");
        retval = elm->retval;
    }
    return EXIT_SUCCESS;
}

extern void thread_exit(void *retval)
{
    struct thread *current = get_last_queue_element();
    retval = *current->retval;
    SIMPLEQ_REMOVE(&head, current, thread, entry);
    VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
    free(current);
    exit(0);
}