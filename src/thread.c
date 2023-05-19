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

int num_threads=0;

thread_t main_thread; // id of the main thread

typedef SIMPLEQ_HEAD(thread_queue_t, thread) head_t;
head_t head_run_queue;
head_t head_sleep_queue;

sigset_t sigprof;
int nb_blocks = 0;
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
    printf("main thread id: %p\n", main_thread);

    if (init_timer() == -1)
    {
        printf("init_timer failed\n");
        return;
    }
}

void thread_debug(void)
{
    struct thread *t;
    printf("[%p]DEBUGGING\n", thread_self());
    SIMPLEQ_FOREACH(t, &head_run_queue, entry)
    {
        printf("[%p]%p running ? %s\n", thread_self(), t->thread, t->status == RUNNING ? "yes" : "no");
    }
    printf("[%p]END DEBUGGING\n\n", thread_self());
}

int len_run_queue(void)
{
    // /* get the length of the run queue */
    // // printf("[%p]len_run_queue\n", thread_self());
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
    /* Assumes that there is only one thread in run queue (not main) and it is finished */
    struct thread *last_thread = SIMPLEQ_FIRST(&head_run_queue);
    assert(last_thread->thread != main_thread);
    assert(last_thread->status == FINISHED);
    SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry); // remove finished thread from the run queue
    SIMPLEQ_INSERT_TAIL(&head_sleep_queue, last_thread, entry); // insert it into the sleep queue
    // assert(SIMPLEQ_EMPTY(&head_run_queue));

    /* Get back main thread */
    struct thread *main_thread_s = SIMPLEQ_FIRST(&head_sleep_queue);
    SIMPLEQ_REMOVE_HEAD(&head_sleep_queue, entry);            // remove main thread from the sleep queue
    SIMPLEQ_INSERT_HEAD(&head_run_queue, main_thread_s, entry); // insert it into the run queue
    main_thread_s->status = RUNNING;                            // mark it as running
    return main_thread_s;
}

struct thread *get_first_run_queue_element(void)
{
    /* get the first element that is running in the queue, all of the finished threads go back to the beginning of the queue */

    struct thread *first = SIMPLEQ_FIRST(&head_run_queue);
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

    return first;
}

extern thread_t thread_self(void)
{
    struct thread *first = SIMPLEQ_FIRST(&head_run_queue);
    return first->thread;
}

void meta_func(void *(*func)(void *), void *args, struct thread *current)
{
    /* function that is called by makecontext */
    assert(nb_blocks == 1);
    unblock_sigprof(); // unblock SIGPROF signal --> signal est blocké puisqu'on arrive après un swapcontext qui bloque le signal
    if (init_timer() == -1)
    {
        // free(new_thread_s);
        // printf("init_timer failed\n");
        return;
    }
    // current->retval = func(args);
    thread_exit(func(args)); // exit the thread
    block_sigprof();
}

int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg)
{
    // if (func != NULL)
    //     printf("[%p] thread_create\n", thread_self());
    // thread_debug();
    block_sigprof(); 
    struct thread *new_thread_s = malloc(sizeof(struct thread));
    new_thread_s->thread = (thread_t)new_thread_s;
    *newthread = new_thread_s->thread;
    printf("new thread id: %p\n", new_thread_s->thread);
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
        unblock_sigprof();
        makecontext(&new_thread_s->uc, (void (*)(void))meta_func, 3, func, funcarg, new_thread_s);
        block_sigprof();
    }
    new_thread_s->func = func;
    new_thread_s->funcarg = funcarg;


    // add the thread to the queue

    SIMPLEQ_INSERT_TAIL(&head_run_queue, new_thread_s, entry);
    unblock_sigprof();
    num_threads++;
    return EXIT_SUCCESS;
}

// Current thread placed at the beginning of the run queue (--> FIFO)
extern int thread_yield(void)
{
    // printf("[%p] thread_yield\n", thread_self());
    block_sigprof();
    if (SIMPLEQ_EMPTY(&head_run_queue))
    {
        // printf("NO HEAD FOR QUEUE\n");
        unblock_sigprof();
        return -1;
    }

    // get the current thread
    struct thread *current = get_first_run_queue_element();
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

    assert(nb_blocks == 1);
    swapcontext(&current->uc, &next_executed_thread->uc);
    assert(nb_blocks == 1);
    unblock_sigprof();

    return EXIT_SUCCESS;
}

extern int thread_join(thread_t thread, void **retval)
{
    // printf("[%p] thread_join\n", thread_self());
    block_sigprof();
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
        // printf("thread not found\n");
        unblock_sigprof();
        // thread not found
        return -1;
    }

    while (elm->status != FINISHED)
    {
        // waiting for the thread to finish
        unblock_sigprof();
        assert(!thread_yield());
        block_sigprof();
    }

    if (retval != NULL)
    {
        // store return value
        *retval = elm->retval;
    }
    unblock_sigprof();
    return EXIT_SUCCESS;
}

extern void thread_exit(void *retval)
{
    // printf("[%p] thread_exit\n", thread_self());
    /* Mark the thread as finished and switch context to newt thread */
    block_sigprof();
    struct thread *current = get_first_run_queue_element();
    current->retval = retval;
    // unblock_sigprof();
    current->status = FINISHED;
    num_threads--;

    printf("thread %p finished\n", current->thread);

    struct thread *next_executed_thread;
    
    if (len_run_queue() == 0 && current->thread != main_thread) {
        next_executed_thread = go_back_to_main_thread();
    } else {
        next_executed_thread = get_first_run_queue_element();
    }

    assert(nb_blocks == 1);
    swapcontext(&current->uc, &next_executed_thread->uc);
    assert(nb_blocks == 1);
    unblock_sigprof();

    if (len_run_queue() == 0 && current->thread == main_thread) {
        exit(EXIT_SUCCESS);
    }
    // if (current->thread == main_thread)
    // {
    //     // if main thread, swap context (will come back here when all threads are finished)
    //     // block_sigprof();
    //     assert(nb_blocks == 1);
    //     swapcontext(&current->uc, &next_executed_thread->uc);
    //     assert(nb_blocks == 1);
    //     unblock_sigprof();
    //     exit(EXIT_SUCCESS);
    // } else {
    //     // block_sigprof();
    //     assert(nb_blocks == 1);
    //     setcontext(&next_executed_thread->uc); /* TODO: peut combiner les deux morceaux ? de toute façon les threads non principaux ne reprendrons jamais leur exécution */
    //     unblock_sigprof();
    // }
}

static void sigprof_handler(int signum, siginfo_t *nfo, void *context)
{
    (void)signum;
    assert(nb_blocks == 0);
    thread_yield();
    assert(nb_blocks == 0);
}

static int init_timer(void)
{
    /* Every 10 ms of thread execution, a SIGPROF signal is sent */
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
        {0, 10000}, // 10 000 microseconds = 10 ms
        {0, 1} // arms the timer as soon as possible
    };

    // Enable timer
    if (setitimer(ITIMER_PROF, &timer, NULL) == -1)
    {
        // printf("setitimer failed\n");
        if (errno == EFAULT)
        {
            printf("DEFAULT: new_value is not a valid pointer\n");
        }
        else if (errno == EINVAL)
        {
            printf("which is not one of ITIMER_REAL, ITIMER_VIRTUAL, or ITIMER_PROF; or one of the tv_usec fields in the structure pointed to by new_value contains a value outside the range 0 to 999999\n");
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
    // printf("block_sigprof\n");

    nb_blocks++;
    assert(nb_blocks == 1);
}

/**
 * Unblock reception of SIGPROF signal
 */
static void unblock_sigprof(void)
{
    nb_blocks--;
    assert(nb_blocks == 0);
    // printf("unblock sigprof\n");
    sigemptyset(&sigprof);
    sigaddset(&sigprof, SIGPROF);
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
        printf("freeing thread %p\n", current->thread);
        assert(current->thread != main_thread);
        SIMPLEQ_REMOVE_HEAD(&head_sleep_queue, entry);
        VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
        free(current->uc.uc_stack.ss_sp);
        free(current);
    }
}

__attribute__((__destructor__)) void my_end()
{
    /* free all the threads */
    assert(nb_blocks == 0);
    block_sigprof();
    assert(nb_blocks == 1);
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
    block_sigprof();
    mutex->status = 0;
    mutex->locker = NULL;
    unblock_sigprof();
    return EXIT_SUCCESS;
}

int mutex_yield(thread_mutex_t *mutex)
{
    if (len_run_queue() <= 1)
    {
        return EXIT_SUCCESS;
    }
    struct thread *current = get_first_run_queue_element();
    SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);
    SIMPLEQ_INSERT_TAIL(&head_run_queue, current, entry); // add the current thread at the beginning of the queue

    struct thread *next_executed_thread = mutex->locker;
    SIMPLEQ_REMOVE(&head_run_queue, next_executed_thread, thread, entry);
    SIMPLEQ_INSERT_HEAD(&head_run_queue, next_executed_thread, entry);
    assert(nb_blocks == 1);
    swapcontext(&current->uc, &next_executed_thread->uc);
    assert(nb_blocks == 1);
    return EXIT_SUCCESS;
}
