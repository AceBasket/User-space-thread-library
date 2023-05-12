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

enum status {
    RUNNING,
    FINISHED
};
enum m_status {
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
    enum status status;
    int valgrind_stackid;
};

typedef struct {
    thread_mutex_t mutex;
    volatile int lock_flag;
} extended_mutex;

thread_t main_thread; // id of the main thread

int locker = 0;

typedef SIMPLEQ_HEAD(thread_queue_t, thread) head_t;
head_t head_run_queue;
head_t head_sleep_queue;

sigset_t sigprof;
static int init_timer(void);
static void block_sigprof(void);
static void unblock_sigprof(void);

__attribute__((__constructor__)) void my_init() {
    head_t head_run_queue_tmp = SIMPLEQ_HEAD_INITIALIZER(head_run_queue);
    head_run_queue = head_run_queue_tmp;
    head_t head_sleep_queue_tmp = SIMPLEQ_HEAD_INITIALIZER(head_sleep_queue);
    head_sleep_queue = head_sleep_queue_tmp;
    thread_create(&main_thread, NULL, NULL);
    if (init_timer() == -1) {
        // free(new_thread_s);
        printf("init_timer failed\n");
        return;
    }
}

void thread_debug(void) {
    struct thread *t;
    printf("[%p]DEBUGGING\n", thread_self());
    SIMPLEQ_FOREACH(t, &head_run_queue, entry) {
        // printf("[%p] %p \n", thread_self(), t->thread);
        printf("[%p]%p running ? %s\n", thread_self(), t->thread, t->status == RUNNING ? "yes" : "no");
    }
    printf("[%p]END DEBUGGING\n\n", thread_self());
}

int len_run_queue(void) {
    /* get the length of the run queue */
    // printf("[%p]len_run_queue\n", thread_self());
    struct thread *t;
    int len = 0;
    SIMPLEQ_FOREACH(t, &head_run_queue, entry) {
        if (t->status == RUNNING) { // only count the running threads
            len++;
        }
    }

    return len;
}

struct thread *go_back_to_main_thread(void) {
    /* Assumes that main thread is still in queue */
    // printf("[%p]go_back_to_main_thread\n", thread_self());
    // block_sigprof();
    struct thread *main_thread = SIMPLEQ_FIRST(&head_sleep_queue);
    SIMPLEQ_REMOVE_HEAD(&head_sleep_queue, entry);            // remove main thread from the sleep queue
    SIMPLEQ_INSERT_HEAD(&head_run_queue, main_thread, entry); // insert it into the run queue
    main_thread->status = RUNNING;                            // mark it as running
    // unblock_sigprof();
    return main_thread;
}

struct thread *get_first_run_queue_element(void) {
    /* get the first element that is running in the queue, all of the finished threads go back to the beginning of the queue */
    // block_sigprof();
    struct thread *first = SIMPLEQ_FIRST(&head_run_queue);
    while (first->status == FINISHED) {
        SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry); // remove finished thread from the run queue
        if (first->thread == main_thread) {
            // if the main thread is finished, we need to keep it in an accessible place (--> end of sleep queue)
            SIMPLEQ_INSERT_HEAD(&head_sleep_queue, first, entry);
        } else {
            SIMPLEQ_INSERT_TAIL(&head_sleep_queue, first, entry); // insert it into the sleep queue
        }
        first = SIMPLEQ_FIRST(&head_run_queue); // get the new first element of the run queue
    }
    // unblock_sigprof();

    return first;
}

extern thread_t thread_self(void) {
    // block_sigprof();
    struct thread *first = SIMPLEQ_FIRST(&head_run_queue);
    // unblock_sigprof();
    return first->thread;
}

void meta_func(void *(*func)(void *), void *args, struct thread *current) {
    /* function that is called by makecontext */
    unblock_sigprof();
    if (init_timer() == -1) {
        // free(new_thread_s);
        printf("init_timer failed\n");
        return;
    }
    current->retval = func(args);
    block_sigprof();

    // printf("[%p] meta_func\n", thread_self());

    // should only go here when the thread returns without using thread_exit
    if (len_run_queue() != 1) {
        current->status = FINISHED;
        struct thread *next_executed_thread = get_first_run_queue_element();
        // block_sigprof();
        setcontext(&next_executed_thread->uc);
        // unblock_sigprof();
        unblock_sigprof();
        exit(EXIT_SUCCESS);
    }
    if (len_run_queue() == 1) {
        // if only one thread left in queue, exit
        if (thread_self() != main_thread) {
            // if that thread is not the main thread, return to the context of the main thread (just before exit(EXIT_SUCCESS)) in thread_exit
            struct thread *main_thread = go_back_to_main_thread();
            // block_sigprof();
            setcontext(&main_thread->uc);
        }
        unblock_sigprof();
        exit(EXIT_SUCCESS);
    }
}

int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg) {
    // if (func != NULL)
    //     printf("[%p] thread_create\n", thread_self());
        // thread_debug();
    block_sigprof(); // Je pense qu'on doit blocker le signal pendant toute la création du thread
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


    // add the thread to the queue

    SIMPLEQ_INSERT_TAIL(&head_run_queue, new_thread_s, entry);
    unblock_sigprof();
    return EXIT_SUCCESS;
}

// Current thread placed at the beginning of the run queue (--> FIFO)
extern int thread_yield(void) {
    // printf("[%p] thread_yield\n", thread_self());
    block_sigprof();
    if (SIMPLEQ_EMPTY(&head_run_queue)) {
        return -1;
    }

    // block_sigprof(); // should be unblocked when context swaping
    // get the current thread
    struct thread *current = get_first_run_queue_element();
    // unblock_sigprof();
    if (len_run_queue() == 1) {
        // no need to yield if only one running thread in queue
        return EXIT_SUCCESS;
    }

    // remove the current thread  the queue
    SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);

    if (SIMPLEQ_EMPTY(&head_run_queue)) {
        // error if the queue becomes empty
        return -1;
    }

    SIMPLEQ_INSERT_TAIL(&head_run_queue, current, entry); // add the current thread at the beginning of the queue


    // swap context with the next thread in the queue
    struct thread *next_executed_thread = get_first_run_queue_element();

    swapcontext(&current->uc, &next_executed_thread->uc);
    unblock_sigprof();


    return EXIT_SUCCESS;
}

extern int thread_join(thread_t thread, void **retval) {
    // printf("[%p] thread_join\n", thread_self());
    block_sigprof();
    struct thread *current = get_first_run_queue_element();
    thread_t current_thread = current->thread;

    if (current_thread == thread) {
        printf("can't wait for itself\n");
        // can't wait for itself
        unblock_sigprof();
        return -1;
    }

    struct thread *elm;
    int elm_found_bool = 0;
    // look for the thread in the run queue
    SIMPLEQ_FOREACH(elm, &head_run_queue, entry) {
        if (elm->thread == thread) {
            elm_found_bool = 1;
            break;
        }
    }

    // look for the thread in the sleep queue
    if (elm_found_bool == 0) {
        SIMPLEQ_FOREACH(elm, &head_sleep_queue, entry) {
            if (elm->thread == thread) {
                elm_found_bool = 1;
                break;
            }
        }
    }
    if (elm_found_bool == 0) {
        printf("thread not found\n");
        unblock_sigprof();
        // thread not found
        return -1;
    }

    while (elm->status != FINISHED) {
        // waiting for the thread to finish
        // block_sigprof();
        unblock_sigprof();
        assert(!thread_yield());
        block_sigprof();
        // unblock_sigprof();
    }

    if (retval != NULL) {
        // store return value
        // block_sigprof();
        *retval = elm->retval;
        // unblock_sigprof();
    }
    unblock_sigprof();
    return EXIT_SUCCESS;
}

extern void thread_exit(void *retval) {
    // printf("[%p] thread_exit\n", thread_self());
    /* Mark the thread as finished and switch context to newt thread */
    block_sigprof();
    struct thread *current = get_first_run_queue_element();
    current->retval = retval;
    // unblock_sigprof();
    current->status = FINISHED;

    struct thread *next_executed_thread = get_first_run_queue_element();
    if (current->thread == main_thread) {
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

int thread_mutex_init(thread_mutex_t *mutex) {
    mutex->dummy = UNLOCK;
    return EXIT_SUCCESS;
}

int thread_mutex_destroy(thread_mutex_t *mutex) {
    return EXIT_SUCCESS;
}

int thread_mutex_lock(thread_mutex_t *mutex) {
    while (mutex->dummy == 1) {
        thread_yield();
    }
    mutex->dummy = 1;
    return EXIT_SUCCESS;
}

int thread_mutex_unlock(thread_mutex_t *mutex) {
    mutex->dummy = 0;
    return EXIT_SUCCESS;
}

static void sigprof_handler(int signum, siginfo_t *nfo, void *context) {
    (void)signum;
    // printf("[%p] SIGPROF\n", thread_self());
    block_sigprof();
    // printf("[%p] SIGPROF\n", thread_self());
    // puts("SIGPROF");

    // This code can be useful to change thread context with the context given by signal handler

        // Backup the current context
    struct thread *current = get_first_run_queue_element();
    // ucontext_t *stored_context = &current->uc;
    ucontext_t *updated = (ucontext_t *)context;

    current->uc.uc_flags = updated->uc_flags;
    current->uc.uc_link = updated->uc_link;
    current->uc.uc_mcontext = updated->uc_mcontext;
    current->uc.uc_sigmask = updated->uc_sigmask;

    // setcontext(&current->uc);
    // thread_yield();

    /* Let's do equivalent of thread_yield */
    if (SIMPLEQ_EMPTY(&head_run_queue)) {
        return;
    }

    if (SIMPLEQ_FIRST(&head_run_queue) == SIMPLEQ_LAST(&head_run_queue, thread, entry)) {
        // only one thread in queue
        return;
    }

    // struct thread *current = get_first_run_queue_element();
    SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);
    SIMPLEQ_INSERT_TAIL(&head_run_queue, current, entry);
    struct thread *next_executed_thread = get_first_run_queue_element();
    swapcontext(&current->uc, &next_executed_thread->uc);
    unblock_sigprof();

}

static int init_timer(void) {
    /* Every 10 ms of thread execution, a SIGPROF signal is sent */
    // printf("[%p] init_timer\n", thread_self());
    sigset_t all;
    sigfillset(&all);

    struct sigaction sa_alarm = {
            .sa_sigaction = sigprof_handler,
            .sa_mask = all,
            .sa_flags = SA_SIGINFO | SA_RESTART
    };

    struct sigaction old_sigaction;
    if (sigaction(SIGPROF, &sa_alarm, &old_sigaction) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }
    struct itimerval timer = {
        {0, 1000},
        // {0, 10000}, // 10 000 microseconds = 10 ms
        {0, 1} // arms the timer as soon as possible
    };

    // Enable timer
    if (setitimer(ITIMER_PROF, &timer, NULL) == -1) {
        printf("setitimer failed\n");
        if (errno == EFAULT) {
            printf("EFAULT: new_value is not a valid pointer\n");
        } else if (errno == EINVAL) {
            printf("which is not one of ITIMER_REAL, ITIMER_VIRTUAL, or ITIMER_PROF; or one of the tv_usec fields in the structure pointed to by new_value contains a value outside the range 0 to 999999\n");
        }
        if (sigaction(SIGPROF, &old_sigaction, NULL) == -1) {
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
static void block_sigprof(void) {
    sigemptyset(&sigprof);
    sigaddset(&sigprof, SIGPROF);
    if (sigprocmask(SIG_BLOCK, &sigprof, NULL) == -1) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    // puts("block");
}

/**
 * Unblock reception of SIGPROF signal
 */
static void unblock_sigprof(void) {
    sigemptyset(&sigprof);
    sigaddset(&sigprof, SIGPROF);
    // puts("unblock");
    if (sigprocmask(SIG_UNBLOCK, &sigprof, NULL) == -1) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }
}

void free_sleep_queue() {
    while (!SIMPLEQ_EMPTY(&head_sleep_queue)) {
        struct thread *current = SIMPLEQ_FIRST(&head_sleep_queue);
        SIMPLEQ_REMOVE_HEAD(&head_sleep_queue, entry);
        VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
        free(current->uc.uc_stack.ss_sp);
        free(current);
    }
}

__attribute__((__destructor__)) void my_end() {
    /* free all the threads */
    block_sigprof();
    free_sleep_queue();
    if (SIMPLEQ_EMPTY(&head_run_queue)) {
        return;
    }
    while (!SIMPLEQ_EMPTY(&head_run_queue)) {
        // remove first thread from queue
        struct thread *current = SIMPLEQ_FIRST(&head_run_queue);
        SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);
        if (current->thread != main_thread) {
            // if not main thread, free the stack
            VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
            free(current->uc.uc_stack.ss_sp);
        }
        // free the thread structure
        free(current);
    }
    // unblock_sigprof();
}