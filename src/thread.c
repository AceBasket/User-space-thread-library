#include <ucontext.h>

#include "thread.h"
#include "queue.h"

struct thread
{
    thread_t *thread;
    ucontext_t uc;
    ucontext_t previous;
    SIMPLEQ_ENTRY(thread)
    entry;
    void *(*func)(void *);
    void *funcarg;
};

SIMPLEQ_HEAD(thread_queue_t, thread);

struct thread_queue_t thread_queue = SIMPLEQ_HEAD_INITIALIZER(thread_queue);

void meta_func(void *(*func)(void *), void *args, void *res)
{
    res = func(args);
}

int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg)
{
    // TODO : Free this malloc !!!
    struct thread *new_thread = malloc(sizeof(struct thread));
    new_thread->thread = newthread;

    if (getcontext(&(new_thread->uc)) == -1)
    {
        free(new_thread);
        return -1;
    };
    new_thread->uc.uc_stack.ss_size = 64 * 1024;
    new_thread->uc.uc_stack.ss_sp = malloc(new_thread->uc.uc_stack.ss_size);
    new_thread->uc.uc_link = &(new_thread->previous);
    makecontext(&new_thread->uc, (void (*)(void))meta_func, 1, funcarg);

    new_thread->func = func;
    new_thread->funcarg = funcarg;

    SIMPLEQ_INSERT_HEAD(&thread_queue, new_thread, entry);

    return 0;
}

extern int thread_join(thread_t thread, void **retval)
{
    return 0;
}