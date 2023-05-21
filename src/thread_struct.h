#ifndef THREAD_STRUCT_H
#define THREAD_STRUCT_H
#include <ucontext.h>


enum thread_status {
    RUNNING,
    FINISHED
};

struct thread {
    thread_t thread_id;
    ucontext_t uc;
    SIMPLEQ_ENTRY(thread)
        entry;
    void *(*func)(void *);
    void *funcarg;
    void *retval;
    enum thread_status status;
    int valgrind_stackid;
};

#endif