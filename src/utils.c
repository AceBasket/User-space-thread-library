#include <ucontext.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <valgrind/valgrind.h>
#include <assert.h>
#include <signal.h>
#include <sys/time.h>

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

#include "utils.h"

int nb_run_queue_threads = 0;
#ifdef PREEMPTION
int nb_blocks = 0;
#endif

thread_t main_thread; // id of the main thread

head_t head_run_queue;
head_t head_sleep_queue;

sigset_t sigprof;

void log_message(const enum ERROR_TYPE type, const char *message, ...)
{
    va_list args;
    va_start(args, message);
    // FILE *file = fopen("server.log", "a");
    char *error_message = "Info";
    switch (type)
    {
    case DEBUGGING:
    {
        error_message = "Debug";
        fprintf(stderr, ANSI_COLOR_MAGENTA);
        break;
    }
    case WARN:
    {
        error_message = "Warning";
        fprintf(stderr, ANSI_COLOR_YELLOW);
        break;
    }
    case CRITIC:
    {
        error_message = "Critic";
        fprintf(stderr, ANSI_COLOR_RED);
        break;
    }
    case SUCCESS:
    {
        error_message = "Success";
        fprintf(stderr, ANSI_COLOR_GREEN);
        break;
    }
    case INFO:
    {
        fprintf(stderr, ANSI_COLOR_BLUE);
        break;
    }
    default:
        fprintf(stderr, ANSI_COLOR_RESET);
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%d-%m-%Y %H:%M:%S", t);

    fprintf(stderr, "%s %s", timestamp, error_message);

    fprintf(stderr, ANSI_COLOR_RESET);
    fprintf(stderr, " : ");

    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void exit_if(int condition, const char *prefix)
{
    if (condition)
    {
        if (errno != 0)
        {
            log_message(CRITIC, "%s -> %s", prefix, strerror(errno));
            perror(prefix);
        }
        else
        {
            log_message(CRITIC, prefix);
        }
        exit(EXIT_FAILURE);
    }
}

int len_run_queue(void)
{
    return nb_run_queue_threads;
}

struct thread *go_back_to_main_thread(void)
{
    /* Assumes that there is only one thread in run queue (not main) and it is finished */
    struct thread *last_thread = SIMPLEQ_FIRST(&head_run_queue);
    assert(last_thread->thread != main_thread);
    assert(last_thread->status == FINISHED);
    remove_head_run_queue(); // remove finished thread from the run queue

    SIMPLEQ_INSERT_TAIL(&head_sleep_queue, last_thread, entry); // insert it into the sleep queue

    /* Get back main thread */
    struct thread *main_thread_s = SIMPLEQ_FIRST(&head_sleep_queue);
    SIMPLEQ_REMOVE_HEAD(&head_sleep_queue, entry); // remove main thread from the sleep queue
    insert_head_run_queue(main_thread_s);          // insert it into the run queue
    main_thread_s->status = RUNNING;               // mark it as running
    return main_thread_s;
}

struct thread *get_first_run_queue_element(void)
{
    /* get the first element that is running in the queue, all of the finished threads go back to the beginning of the queue */

    struct thread *first = SIMPLEQ_FIRST(&head_run_queue);
    while (first->status == FINISHED)
    {
        SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry); // remove finished thread from the run queue
        nb_run_queue_threads--;
        if (first->thread == main_thread)
        {
            // if the main thread is finished, we need to keep it in an accessible place (--> end of sleep queue)
            SIMPLEQ_INSERT_HEAD(&head_sleep_queue, first, entry);
        }
        else
        {
            SIMPLEQ_INSERT_TAIL(&head_sleep_queue, first, entry); // insert it into the sleep queue
        }
        first = SIMPLEQ_FIRST(&head_run_queue); // get the new first element of the run queue
    }

    return first;
}
#ifdef PREEMPTION
void sigprof_handler(int signum, siginfo_t *nfo, void *context)
{
    (void)signum;
    assert(nb_blocks == 0);
    block_sigprof();
    internal_thread_yield();
    unblock_sigprof();
    assert(nb_blocks == 0);
}

int init_timer(void)
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
        .it_interval = {
            .tv_sec = 0,
            .tv_usec = 10000},
        .it_value = {.tv_sec = 0, .tv_usec = 10000}};

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

int disarm_timer(void)
{
    struct itimerval timer = {
        .it_interval = {
            .tv_sec = 0,
            .tv_usec = 0},
        .it_value = {.tv_sec = 0, .tv_usec = 0}};

    // Disable timer
    if (setitimer(ITIMER_PROF, &timer, NULL) == -1)
    {
        perror("setitimer");
        return -1;
    }
    return EXIT_SUCCESS;
}

void block_sigprof(void)
{
    if (sigprocmask(SIG_BLOCK, &sigprof, NULL) == -1)
    {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    nb_blocks++;
    assert(nb_blocks == 1);
}

void unblock_sigprof(void)
{
    nb_blocks--;
    assert(nb_blocks == 0);

    if (sigprocmask(SIG_UNBLOCK, &sigprof, NULL) == -1)
    {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }
}
#endif

void free_sleep_queue(void)
{
    while (!SIMPLEQ_EMPTY(&head_sleep_queue))
    {
        struct thread *current = SIMPLEQ_FIRST(&head_sleep_queue);
#ifdef DEBUG
        log_message(DEBUGGING, "freeing thread %p\n", current->thread);
#endif
        assert(current->thread != main_thread);
        SIMPLEQ_REMOVE_HEAD(&head_sleep_queue, entry);
        VALGRIND_STACK_DEREGISTER(current->valgrind_stackid);
        free(current->uc.uc_stack.ss_sp);
        free(current);
    }
}

int mutex_yield(thread_mutex_t *mutex)
{
    if (len_run_queue() <= 1)
    {
        return EXIT_SUCCESS;
    }
    struct thread *current = get_first_run_queue_element();
    remove_head_run_queue();
    insert_tail_run_queue(current);

    struct thread *next_executed_thread = mutex->locker;
    remove_run_queue(next_executed_thread);
    insert_head_run_queue(next_executed_thread);

#ifdef PREEMPTION
    assert(nb_blocks == 1);
#endif
    swapcontext(&current->uc, &next_executed_thread->uc);
#ifdef PREEMPTION
    assert(nb_blocks == 1);
#endif
    return EXIT_SUCCESS;
}

int internal_thread_yield(void)
{
    if (SIMPLEQ_EMPTY(&head_run_queue))
    {
        return -1;
    }

    // get the current thread
    struct thread *current = get_first_run_queue_element();
    if (len_run_queue() == 1)
    {
        // no need to yield if only one running thread in queue
        return EXIT_SUCCESS;
    }

    // remove the current thread from the queue
    remove_head_run_queue();

    if (SIMPLEQ_EMPTY(&head_run_queue))
    {
        // error if the queue becomes empty
        return -1;
    }

    insert_tail_run_queue(current);

    // swap context with the next thread in the queue
    struct thread *next_executed_thread = get_first_run_queue_element();

#ifdef PREEMPTION
    assert(nb_blocks == 1);
#endif
    swapcontext(&current->uc, &next_executed_thread->uc);
#ifdef PREEMPTION
    assert(nb_blocks == 1);
#endif

    return EXIT_SUCCESS;
}

void insert_tail_run_queue(struct thread *thread)
{
    SIMPLEQ_INSERT_TAIL(&head_run_queue, thread, entry);
    nb_run_queue_threads++;
}

void insert_head_run_queue(struct thread *thread)
{
    SIMPLEQ_INSERT_HEAD(&head_run_queue, thread, entry);
    nb_run_queue_threads++;
}

void remove_head_run_queue(void)
{
    SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);
    nb_run_queue_threads--;
}

void remove_run_queue(struct thread *thread_to_remove)
{
    SIMPLEQ_REMOVE(&head_run_queue, thread_to_remove, thread, entry);
    nb_run_queue_threads--;
}