#include <ucontext.h>
#include "thread.h"
#include "queue.h"
#include <valgrind/valgrind.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>

#include "logger.h"

enum status
{
    RUNNING,
    FINISHED
};
enum m_status
{
    UNLOCK,
    LOCK
};

struct thread
{
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

int num_threads = 0;

thread_t main_thread; // id of the main thread

typedef SIMPLEQ_HEAD(thread_queue_t, thread) head_t;
head_t head_run_queue;
head_t head_sleep_queue;

sigset_t sigprof;
static int init_timer(void);
static void block_sigprof(void);
static void unblock_sigprof(void);

__attribute__((__constructor__)) void my_init()
{
    head_t head_run_queue_tmp = SIMPLEQ_HEAD_INITIALIZER(head_run_queue);
    head_run_queue = head_run_queue_tmp;
    head_t head_sleep_queue_tmp = SIMPLEQ_HEAD_INITIALIZER(head_sleep_queue);
    head_sleep_queue = head_sleep_queue_tmp;

    thread_create(&main_thread, NULL, NULL);

    if (init_timer() == -1)
    {
        // free(new_thread_s);
        log_message(CRITIC, "init_timer failed\n");
        return;
    }
}

#ifdef DEBUG
void thread_debug(void)
{
    struct thread *t;
    log_message(DEBUGGING, "[%p]DEBUGGING\n", thread_self());
    SIMPLEQ_FOREACH(t, &head_run_queue, entry)
    {
        log_message(DEBUGGING, "[%p] %p \n", thread_self(), t->thread);
        log_message(DEBUGGING, "[%p]%p running ? %s\n", thread_self(), t->thread, t->status == RUNNING ? "yes" : "no");
    }
    log_message(DEBUGGING, "[%p]END DEBUGGING\n\n", thread_self());
}
#endif

int len_run_queue(void)
{
    // /* get the length of the run queue */
    // log_message(DEBUGGING, "[%p]len_run_queue\n", thread_self());
    // struct thread *t;
    // int len = 0;
    // SIMPLEQ_FOREACH(t, &head_run_queue, entry)
    // {
    //     len++;
    //     if (t->status == RUNNING)
    //     { // only count the running threads
    //     }
    // }

    // return len;

    return num_threads;
}

struct thread *go_back_to_main_thread(void)
{
/* Assumes that main thread is still in queue */
#ifdef DEBUG
    thread_t th_debug = thread_self();
    log_message(DEBUGGING, "[%p]go_back_to_main_thread\n", th_debug);
#endif
    // block_sigprof();
    struct thread *main_thread_s = SIMPLEQ_FIRST(&head_sleep_queue);
    SIMPLEQ_REMOVE_HEAD(&head_sleep_queue, entry);              // remove main thread from the sleep queue
    SIMPLEQ_INSERT_HEAD(&head_run_queue, main_thread_s, entry); // insert it into the run queue
    main_thread_s->status = RUNNING;                            // mark it as running
    // unblock_sigprof();
    return main_thread_s;
}

struct thread *get_first_run_queue_element(void)
{
    /* get the first element that is running in the queue, all of the finished threads go back to the beginning of the queue */
    // block_sigprof();
    struct thread *first = SIMPLEQ_FIRST(&head_run_queue);
#ifdef DEBUG
    log_message(DEBUGGING, "STATUS %d\n", first->status);
#endif

    while (first->status == FINISHED)
    {
        SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry); // remove finished thread from the run queue
        num_threads--;
        if (first->thread == main_thread)
        {
            // if the main thread is finished, we need to keep it in an accessible place (--> end of sleep queue)
            SIMPLEQ_INSERT_HEAD(&head_sleep_queue, first, entry);
            num_threads++;
        }
        else
        {
            SIMPLEQ_INSERT_TAIL(&head_sleep_queue, first, entry); // insert it into the sleep queue
            num_threads++;
        }
        first = SIMPLEQ_FIRST(&head_run_queue); // get the new first element of the run queue
    }
    // unblock_sigprof();

    return first;
}

extern thread_t thread_self(void)
{
    // block_sigprof();
    struct thread *first = SIMPLEQ_FIRST(&head_run_queue);
    // unblock_sigprof();
    return first->thread;
}

void meta_func(void *(*func)(void *), void *args, struct thread *current)
{
    /* function that is called by makecontext */
    unblock_sigprof();
    if (init_timer() == -1)
    {
// free(new_thread_s);
#ifdef DEBUG
        log_message(CRITIC, "init_timer failed\n");
#endif
        return;
    }
    current->retval = func(args);
    unblock_sigprof();
    block_sigprof();

#ifdef DEBUG
    thread_t th_debug = thread_self();
    log_message(DEBUGGING, "[%p] meta_func\n", th_debug);
#endif

    // should only go here when the thread returns without using thread_exit
    if (len_run_queue() != 1)
    {
        current->status = FINISHED;
        struct thread *next_executed_thread = get_first_run_queue_element();
        // block_sigprof();
        setcontext(&next_executed_thread->uc);
        // unblock_sigprof();
        unblock_sigprof();
        exit(EXIT_SUCCESS);
    }
    else if (len_run_queue() == 1)
    {
        // if only one thread left in queue, exit
        if (thread_self() != main_thread)
        {
            // if that thread is not the main thread, return to the context of the main thread (just before exit(EXIT_SUCCESS)) in thread_exit
            struct thread *main_thread_s = go_back_to_main_thread();
            // block_sigprof();
            setcontext(&main_thread_s->uc);
        }
        unblock_sigprof();
        exit(EXIT_SUCCESS);
    }
}

int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg)
{
    // if (func != NULL)
    // #ifdef DEBUG
    //     thread_t th_debug = thread_self();
    //     log_message(DEBUGGING, "[%p] thread_create\n", th_debug);
    // #endif
    // thread_debug();
    unblock_sigprof();
    block_sigprof(); // Je pense qu'on doit blocker le signal pendant toute la création du thread
    struct thread *new_thread_s = malloc(sizeof(struct thread));
    new_thread_s->thread = newthread;
    *newthread = newthread;
    new_thread_s->status = RUNNING;
    new_thread_s->retval = NULL;
    getcontext(&new_thread_s->uc);
    if (func != NULL)
    { // if not main thread
        // if (getcontext(&(new_thread_s->uc)) == -1)
        // {
        //     // if error in getting context
        //     VALGRIND_STACK_DEREGISTER(new_thread_s->valgrind_stackid);
        //     free(new_thread_s->uc.uc_stack.ss_sp);
        //     free(new_thread_s);
        //     return -1;
        // }

        /*  */
        // if (thread_self() == main_thread) {
        //     if (init_timer() == EXIT_FAILURE) {
        //         return EXIT_FAILURE;
        //     }
        // }

        // initialize stack + create the thread with makecontext
        new_thread_s->uc.uc_stack.ss_size = SIGSTKSZ;
        new_thread_s->uc.uc_stack.ss_sp = malloc(new_thread_s->uc.uc_stack.ss_size);
        new_thread_s->valgrind_stackid = VALGRIND_STACK_REGISTER(new_thread_s->uc.uc_stack.ss_sp, new_thread_s->uc.uc_stack.ss_sp + new_thread_s->uc.uc_stack.ss_size);
        makecontext(&new_thread_s->uc, (void (*)(void))meta_func, 3, func, funcarg, new_thread_s);
        // unblock_sigprof();
        /* Timer initialized only for threads other than main */
        // if (init_timer() == -1) {
        //     free(new_thread_s);
        //     return -1;
        // }
    }
    new_thread_s->func = func;
    new_thread_s->funcarg = funcarg;

    // if (init_timer() == EXIT_FAILURE) {
    //     return EXIT_FAILURE;
    // }

    // add the thread to the queue

    SIMPLEQ_INSERT_TAIL(&head_run_queue, new_thread_s, entry);
    unblock_sigprof();
    num_threads++;
    return EXIT_SUCCESS;
}

// Current thread placed at the beginning of the run queue (--> FIFO)
extern int thread_yield(void)
{
#ifdef DEBUG
    thread_t th_debug = thread_self();
    log_message(DEBUGGING, "[%p] thread_yield\n", th_debug);
#endif
    unblock_sigprof();
    block_sigprof();
    if (SIMPLEQ_EMPTY(&head_run_queue))
    {
#ifdef DEBUG
        log_message(CRITIC, "NO HEAD FOR QUEUE\n");
#endif
        unblock_sigprof();
        return -1;
    }

    // block_sigprof(); // should be unblocked when context swaping
    // get the current thread
    struct thread *current = get_first_run_queue_element();
    // unblock_sigprof();
    if (len_run_queue() == 1)
    {
        // no need to yield if only one running thread in queue
        unblock_sigprof();
        return EXIT_SUCCESS;
    }

    // remove the current thread  the queue
    SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);

    if (SIMPLEQ_EMPTY(&head_run_queue))
    {
        // error if the queue becomes empty
        unblock_sigprof();
        return -1;
    }

    SIMPLEQ_INSERT_TAIL(&head_run_queue, current, entry); // add the current thread at the beginning of the queue

    // swap context with the next thread in the queue
    struct thread *next_executed_thread = get_first_run_queue_element();

    swapcontext(&current->uc, &next_executed_thread->uc);
    unblock_sigprof();

    return EXIT_SUCCESS;
}

extern int thread_join(thread_t thread, void **retval)
{
#ifdef DEBUG
    thread_t th_debug = thread_self();
    log_message(DEBUGGING, "[%p] thread_join\n", th_debug);
#endif
    unblock_sigprof();
    block_sigprof();
#ifdef DEBUG
    log_message(DEBUGGING, "head_sleep_queue = %p\n", &head_sleep_queue);
#endif
    struct thread *current = get_first_run_queue_element();
    thread_t current_thread = current->thread;

    if (current_thread == thread)
    {
        // can't wait for itself
        unblock_sigprof();
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
        log_message(DEBUGGING, "thread not found\n");
#endif DEBUG
        unblock_sigprof();
        // thread not found
        return -1;
    }

    while (elm->status != FINISHED)
    {
        // waiting for the thread to finish
        // block_sigprof();
        unblock_sigprof();
        assert(!thread_yield());
        unblock_sigprof();
        block_sigprof();
        // unblock_sigprof();
    }

    if (retval != NULL)
    {
        // store return value
        // block_sigprof();
        *retval = elm->retval;
        // unblock_sigprof();
    }
    unblock_sigprof();
    return EXIT_SUCCESS;
}

extern void thread_exit(void *retval)
{
#ifdef DEBUG
    thread_t th_debug = thread_self();
    log_message(DEBUGGING, "[%p] thread_exit\n", th_debug);
#endif
    /* Mark the thread as finished and switch context to newt thread */
    unblock_sigprof();
    block_sigprof();
    struct thread *current = get_first_run_queue_element();
    current->retval = retval;
    // unblock_sigprof();
    current->status = FINISHED;
    num_threads--;

    struct thread *next_executed_thread = get_first_run_queue_element();
    if (current->thread == main_thread)
    {
        // if main thread, swap context (will come back here when all threads are finished)
        // block_sigprof();
        swapcontext(&current->uc, &next_executed_thread->uc);
        unblock_sigprof();
        exit(EXIT_SUCCESS);
    }
    // block_sigprof();
    setcontext(&next_executed_thread->uc);

    unblock_sigprof();
}

static void sigprof_handler(int signum, siginfo_t *nfo, void *context)
{
    (void)signum;
#ifdef DEBUG
    thread_t th_debug = thread_self();
    log_message(DEBUGGING, "[%p] SIGPROF\n", th_debug);
#endif
    unblock_sigprof();
    block_sigprof();
#ifdef DEBUG
    th_debug = thread_self();
    log_message(DEBUGGING, "[%p] SIGPROF\n", th_debug);
#endif
    // puts("SIGPROF");

    // This code can be useful to change thread context with the context given by signal handler

    /* Let's do equivalent of thread_yield */
    if (SIMPLEQ_EMPTY(&head_run_queue))
    {
        return;
    }
    // Backup the current context
    struct thread *current = get_first_run_queue_element();
// ucontext_t *stored_context = &current->uc;

/* On avait souvent void *context = NULL */
// ucontext_t *updated = (ucontext_t *)context;

// current->uc.uc_flags = updated->uc_flags;
// current->uc.uc_link = updated->uc_link;
// current->uc.uc_mcontext = updated->uc_mcontext;
// current->uc.uc_sigmask = updated->uc_sigmask;
#ifdef DEBUG
    log_message(DEBUGGING, "current->uc.uc_stack.ss_sp = %p\n", current->uc.uc_stack.ss_sp);
    log_message(DEBUGGING, "current->uc.uc_stack.ss_size = %ld\n", current->uc.uc_stack.ss_size);
    log_message(DEBUGGING, "current->uc.uc_stack.ss_flags = %d\n", current->uc.uc_stack.ss_flags);
    log_message(DEBUGGING, "current->uc = %p\n", &current->uc);
#endif

    // setcontext(&current->uc);
    // thread_yield();

    if (SIMPLEQ_FIRST(&head_run_queue) == SIMPLEQ_LAST(&head_run_queue, thread, entry))
    {
        // only one thread in queue
        return;
    }

    // struct thread *current = get_first_run_queue_element();
    SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);
    SIMPLEQ_INSERT_TAIL(&head_run_queue, current, entry);
    struct thread *next_executed_thread = get_first_run_queue_element();
#ifdef DEBUG
    log_message(DEBUGGING, "next_executed_thread->uc.uc_stack.ss_sp = %p\n", next_executed_thread->uc.uc_stack.ss_sp);
    log_message(DEBUGGING, "next_executed_thread->uc.uc_stack.ss_size = %ld\n", next_executed_thread->uc.uc_stack.ss_size);
    log_message(DEBUGGING, "next_executed_thread->uc.uc_stack.ss_flags = %d\n", next_executed_thread->uc.uc_stack.ss_flags);
    log_message(DEBUGGING, "next_executed_thread->uc = %p\n", &next_executed_thread->uc);
#endif
    swapcontext(&current->uc, &next_executed_thread->uc);
    unblock_sigprof();
}

static int init_timer(void)
{
/* Every 10 ms of thread execution, a SIGPROF signal is sent */
#ifdef DEBUG
    thread_t th_debug = thread_self();
    log_message(DEBUGGING, "[%p] init_timer\n", th_debug);
#endif
    sigset_t all;
    sigfillset(&all);

    struct sigaction sa_alarm = {
        .sa_sigaction = sigprof_handler,
        .sa_mask = all,
        .sa_flags = SA_SIGINFO | SA_RESTART};

    struct sigaction old_sigaction;
    if (sigaction(SIGPROF, &sa_alarm, &old_sigaction) == -1)
    {
        perror("sigaction");
        return EXIT_FAILURE;
    }
    struct itimerval timer = {
        // {1, 0},
        {0, 10000}, // 10 000 microseconds = 10 ms
        {0, 1}      // arms the timer as soon as possible
    };

    // Enable timer
    if (setitimer(ITIMER_PROF, &timer, NULL) == -1)
    {
#ifdef DEBUG
        log_message(DEBUGGING, "setitimer failed\n");
#endif
        if (errno == EFAULT)
        {
            log_message(CRITIC, "DEFAULT: new_value is not a valid pointer\n");
        }
        else if (errno == EINVAL)
        {
            log_message(CRITIC, "which is not one of ITIMER_REAL, ITIMER_VIRTUAL, or ITIMER_PROF; or one of the tv_usec fields in the structure pointed to by new_value contains a value outside the range 0 to 999999\n");
        }
        if (sigaction(SIGPROF, &old_sigaction, NULL) == -1)
        {
            perror("sigaction");
            return -1;
        }
        return -1;
    }
    return EXIT_SUCCESS;
}

/**
 * Block reception of SIGPROF signal
 */
static void block_sigprof(void)
{
    sigemptyset(&sigprof);
    sigaddset(&sigprof, SIGPROF);
    if (sigprocmask(SIG_BLOCK, &sigprof, NULL) == -1)
    {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    // puts("block");
}

/**
 * Unblock reception of SIGPROF signal
 */
static void unblock_sigprof(void)
{
    sigemptyset(&sigprof);
    sigaddset(&sigprof, SIGPROF);
    // puts("unblock");
    if (sigprocmask(SIG_UNBLOCK, &sigprof, NULL) == -1)
    {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }
}

void free_sleep_queue()
{
    while (!SIMPLEQ_EMPTY(&head_sleep_queue))
    {
        struct thread *current = SIMPLEQ_FIRST(&head_sleep_queue);
        SIMPLEQ_REMOVE_HEAD(&head_sleep_queue, entry);
        VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
        free(current->uc.uc_stack.ss_sp);
        free(current);
    }
}

__attribute__((__destructor__)) void my_end()
{
    /* free all the threads */
    unblock_sigprof();
    block_sigprof();
    free_sleep_queue();
    if (SIMPLEQ_EMPTY(&head_run_queue))
    {
        return;
    }
    while (!SIMPLEQ_EMPTY(&head_run_queue))
    {
        // remove first thread from queue
        struct thread *current = SIMPLEQ_FIRST(&head_run_queue);
        SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);
        if (current->thread != main_thread)
        {
            // if not main thread, free the stack
            VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
            free(current->uc.uc_stack.ss_sp);
        }
        // free the thread structure
        free(current);
    }
}

int thread_mutex_init(thread_mutex_t *mutex)
{
    unblock_sigprof();
    block_sigprof();
    mutex->locker = NULL;
    mutex->status = UNLOCK;
    unblock_sigprof();
    return EXIT_SUCCESS;
}

int thread_mutex_destroy(thread_mutex_t *mutex)
{
    return EXIT_SUCCESS;
}

int mutex_yield(thread_mutex_t *mutex);

int thread_mutex_lock(thread_mutex_t *mutex)
{
    unblock_sigprof();
    block_sigprof();
    while (mutex->status == 1)
    {
        mutex_yield(mutex);
    }
    mutex->status = 1;
    mutex->locker = get_first_run_queue_element();
    // thread_debug();
    unblock_sigprof();
    return EXIT_SUCCESS;
}

int thread_mutex_unlock(thread_mutex_t *mutex)
{
    unblock_sigprof();
    block_sigprof();
    mutex->status = 0;
    mutex->locker = NULL;
    unblock_sigprof();
    return EXIT_SUCCESS;
}

int mutex_yield(thread_mutex_t *mutex)
{
    unblock_sigprof();
    block_sigprof();
    if (len_run_queue() <= 1)
    {
        unblock_sigprof();
        return EXIT_SUCCESS;
    }
    struct thread *current = get_first_run_queue_element();
    SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);
    SIMPLEQ_INSERT_TAIL(&head_run_queue, current, entry); // add the current thread at the beginning of the queue

    struct thread *next_executed_thread = mutex->locker;
    SIMPLEQ_REMOVE(&head_run_queue, next_executed_thread, thread, entry);
    SIMPLEQ_INSERT_HEAD(&head_run_queue, next_executed_thread, entry);
    swapcontext(&current->uc, &next_executed_thread->uc);
    unblock_sigprof();
    return EXIT_SUCCESS;
}
