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
enum m_status {UNLOCK, LOCK};

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
head_t head_run_queue;
head_t head_sleep_queue;



__attribute__((__constructor__)) void my_init() {
    head_t head_run_queue_tmp = SIMPLEQ_HEAD_INITIALIZER(head_run_queue);
    head_run_queue = head_run_queue_tmp;
    head_t head_sleep_queue_tmp = SIMPLEQ_HEAD_INITIALIZER(head_sleep_queue);
    head_sleep_queue = head_sleep_queue_tmp;
    thread_create(&main_thread, NULL, NULL);
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
    struct thread *main_thread = SIMPLEQ_FIRST(&head_sleep_queue);
    SIMPLEQ_REMOVE_HEAD(&head_sleep_queue, entry); // remove main thread from the sleep queue
    SIMPLEQ_INSERT_HEAD(&head_run_queue, main_thread, entry); // insert it into the run queue
    main_thread->status = RUNNING; // mark it as running
    return main_thread;
}

struct thread *get_first_run_queue_element(void) {
    /* get the first element that is running in the queue, all of the finished threads go back to the beginning of the queue */
    struct thread *first = SIMPLEQ_FIRST(&head_run_queue);
    // printf("STATUS %d\n",first->status);
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
    return first;
}

extern thread_t thread_self(void) {
    struct thread *first = SIMPLEQ_FIRST(&head_run_queue);
    return first->thread;
}

// Current thread placed at the beginning of the run queue (--> FIFO)
extern int thread_yield(void) {
    if (SIMPLEQ_EMPTY(&head_run_queue)) {
        printf("NO HEAD FOR QUEUE\n");
        return -1;
    }

    // get the current thread
    struct thread *current = get_first_run_queue_element();
    if (len_run_queue() == 1) {
        // no need to yield if only one running thread in queue
        // printf("LEN=0");
        return EXIT_SUCCESS;
    }

    // remove the current thread from the queue
    SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);

    if (SIMPLEQ_EMPTY(&head_run_queue)) {
        // error if the queue becomes empty
        printf("EMPTY QUEUE\n");
        return -1;
    }
    SIMPLEQ_INSERT_TAIL(&head_run_queue, current, entry); // add the current thread at the beginning of the queue
    // printf("LEN  RUN  QUEUE IN YIELD %d\n",len_run_queue());


    // swap context with the next thread in the queue
    struct thread *next_executed_thread = get_first_run_queue_element();
    swapcontext(&current->uc, &next_executed_thread->uc);
    return EXIT_SUCCESS;
}

void meta_func(void *(*func)(void *), void *args, struct thread *current) {
    current->retval = func(args);
    // should only go here when the thread returns without using thread_exit
    if (len_run_queue() != 1) {
        current->status = FINISHED;
        struct thread *next_executed_thread = get_first_run_queue_element();
        setcontext(&next_executed_thread->uc);
    }
    if (len_run_queue() == 1) {
        // if only one thread left in queue, exit
        if (thread_self() != main_thread) {
            // if that thread is not the main thread, return to the context of the main thread (just before exit(EXIT_SUCCESS)) in thread_exit
            struct thread *main_thread = go_back_to_main_thread();
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
    SIMPLEQ_INSERT_TAIL(&head_run_queue, new_thread_s, entry);
    return EXIT_SUCCESS;
}

extern int thread_join(thread_t thread, void **retval) {
    
    struct thread *current = get_first_run_queue_element();
    thread_t current_thread = current->thread;

    if (current_thread == thread) {
        printf("can't wait for itself\n");
        // can't wait for itself
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
    struct thread *current = get_first_run_queue_element();
    current->retval = retval;

    current->status = FINISHED;
    struct thread *next_executed_thread = get_first_run_queue_element();
    if (current->thread == main_thread) {
        // if main thread, swap context (will come back here when all threads are finished)
        swapcontext(&current->uc, &next_executed_thread->uc);
        exit(EXIT_SUCCESS);
    }
    setcontext(&next_executed_thread->uc);
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
}

head_t head_mutex;

int len_mutex_queue(void) {
    struct thread *t;
    int len = 0;
    SIMPLEQ_FOREACH(t, &head_mutex, entry) {
        if (t->status == RUNNING) { // only count the running threads
            len++;
        }
    }
    return len;
}

int thread_mutex_init(thread_mutex_t *mutex){
    head_t head_mutex_tmp = SIMPLEQ_HEAD_INITIALIZER(head_mutex);
    head_mutex=head_mutex_tmp;
    mutex->locker=NULL;
    mutex->status=UNLOCK;
    return EXIT_SUCCESS;
}

int thread_mutex_destroy(thread_mutex_t *mutex){
    return EXIT_SUCCESS;
}

int mutex_yield(thread_mutex_t * mutex);

int thread_mutex_lock(thread_mutex_t *mutex){
    while(mutex->status==1){
        mutex_yield(mutex);
    }
    mutex->status=1;
    mutex->locker=get_first_run_queue_element();
    // thread_debug();
    return EXIT_SUCCESS;
}


int thread_mutex_unlock(thread_mutex_t *mutex){
    mutex->status=0;
    mutex->locker=NULL;
    return EXIT_SUCCESS;
}


int mutex_yield(thread_mutex_t * mutex) {

    if (len_run_queue() <= 1) {
        return EXIT_SUCCESS;
    }
    struct thread *current = get_first_run_queue_element();
    SIMPLEQ_REMOVE_HEAD(&head_run_queue, entry);
    SIMPLEQ_INSERT_TAIL(&head_run_queue, current, entry); // add the current thread at the beginning of the queue

    struct thread *next_executed_thread = mutex->locker;
    SIMPLEQ_REMOVE(&head_run_queue,next_executed_thread, thread, entry);
    SIMPLEQ_INSERT_HEAD(&head_run_queue,next_executed_thread,entry);
    swapcontext(&current->uc, &next_executed_thread->uc);
    return EXIT_SUCCESS;
}