#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "threadpool.h"

#define FLAG_OFF 0
#define FLAG_ON 1


/**
 * @author: Daniel Gabay
 * threadpool.c
 * --------------------------------------------------------------------------------
 * This file implements the functionality of threadpool.h
 * In order to use it's the pool,it's need to be initialized by calling create_threadpool() method.
 * on succsess it returns a pointer to threadpool.
 * The "pool" is implemented by a queue of jobs. To add new job, call dispatch() method with the needed params.
 * Each "new job" is added into the queue, and waits there until some thread is available to handel it.
 * Note: 1)Each work_t ojbect (what's iv'e mantiones as "job") contains an argument & a pointer to function.
 *         When the thread "handel" the job, it's actualy calls the function with the argument.
 *       2)In oreder to enalbe a clean working multithreaded program, each time a thread want's to get access
 *         to the queue/threadpool var's, it thread must get the mutex lock, o.w he need to wait.
 */

/**forward declerations*/
work_t *createWorkObj(threadpool *tp, dispatch_fn dispatch_to_here, void *arg);
void enqueue(threadpool *tp, work_t *job);
work_t *dequeue(threadpool *tp);
void free_queue(work_t *w_head);
void free_threadpool(threadpool *tp);


/**
 * create_threadpool creates a fixed-sized thread
 * pool.  If the function succeeds, it returns a (non-NULL)
 * "threadpool", else it returns NULL.
 * this function should:
 * 1. input sanity check
 * 2. initialize the threadpool structure
 * 3. initialized mutex and conditional variables
 * 4. create the threads, the thread init function is do_work and its argument is the initialized threadpool.
 */
threadpool *create_threadpool(int num_threads_in_pool) {
    /*input check*/
    if (num_threads_in_pool <= 0 || num_threads_in_pool > MAXT_IN_POOL)
        return NULL;
    threadpool *tp = (threadpool *) malloc(sizeof(threadpool));
    if (tp == NULL)
        return NULL;

    /*initializing vars*/
    tp->num_threads = num_threads_in_pool;
    tp->qsize = 0;

    /*both head and tail points to NULL*/
    tp->qhead = NULL;
    tp->qtail = NULL;

    pthread_mutex_init(&tp->qlock, NULL);
    pthread_cond_init(&tp->q_empty, NULL);
    pthread_cond_init(&tp->q_not_empty, NULL);
    tp->shutdown = FLAG_OFF;
    tp->dont_accept = FLAG_OFF;

    /*creating array of threades*/
    tp->threads = (pthread_t *) malloc(sizeof(pthread_t) * num_threads_in_pool);
    if (tp->threads == NULL) {
        free_threadpool(tp);
        return NULL;
    }
    /*initalizing all threads*/
    for (int i = 0; i < tp->num_threads; i++) {
        int rc = pthread_create(&tp->threads[i], NULL, do_work, (void *) tp);
        if (rc) {
            destroy_threadpool(tp);
            return NULL;
        }
    }

    return tp;
}

/**
 * The work function of the thread
 * this function should:
 * 1. lock mutex
 * 2. if the queue is empty, wait
 * 3. take the first element from the queue (work_t)
 * 4. unlock mutex
 * 5. call the thread routine
 *
 */
void *do_work(void *p) {
    if(p == NULL)
        return NULL;
    threadpool *tp = (threadpool *) p;
    while (1) {
        pthread_mutex_lock(&tp->qlock); //lock mutex
        if (tp->shutdown == FLAG_ON) {  //destroy is called -> unlock mutex and exit
            pthread_mutex_unlock(&tp->qlock);
            return NULL;
        }
        if (tp->qsize == 0) //if there are no jobs, wait until signal
            pthread_cond_wait(&tp->q_not_empty, &tp->qlock);

        if (tp->shutdown == FLAG_ON) { //check who wakes the thread destroy(if flag is on) or queue_not_empty(flag is off)
            pthread_mutex_unlock(&tp->qlock);
            return NULL;
        }

        work_t *w = dequeue(tp); //get the first job from queue
        if (tp->qsize == 0 && tp->dont_accept == FLAG_ON) //when dont_accept is on and qsize is 0, signal on q_empty to start destroy
            pthread_cond_signal(&tp->q_empty);
        pthread_mutex_unlock(&tp->qlock); //unlock mutex before call the routine
        if (w != NULL) { //probably not NULL..
            w->routine(w->arg);
            free(w);
        }
    }

}

/**
 * dispatch enter a "job" of type work_t into the queue.
 * when an available thread takes a job from the queue, it will
 * call the function "dispatch_to_here" with argument "arg".
 * this function should:
 * 1. create and init work_t element
 * 2. lock the mutex
 * 3. add the work_t element to the queue
 * 4. unlock mutex
 */
void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg) {
    if (from_me == NULL || dispatch_to_here == NULL)
        return;
    pthread_mutex_lock(&from_me->qlock);
    if (from_me->dont_accept == FLAG_ON) {
        pthread_mutex_unlock(&from_me->qlock);
        return;
    }
    work_t *job = createWorkObj(from_me, dispatch_to_here, arg);
    if (!job) {
        pthread_mutex_unlock(&from_me->qlock);
        return;
    }
    enqueue(from_me, job);
    pthread_cond_signal(&from_me->q_not_empty);
    pthread_mutex_unlock(&from_me->qlock);
}

/**
 * destroy_threadpool kills the threadpool, causing
 * all threads in it to commit suicide, and then
 * frees all the memory associated with the threadpool.
 */
void destroy_threadpool(threadpool *destroyme) {
    if (destroyme == NULL)
        return;
    pthread_mutex_lock(&destroyme->qlock);
    destroyme->dont_accept = FLAG_ON; //don't accept any more jobs.
    if (destroyme->qsize != 0)
        pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock); //wait until the queue is empty

    destroyme->shutdown = FLAG_ON; //set shutdown flag on after the queue is empty
    pthread_mutex_unlock(&destroyme->qlock);
    pthread_cond_broadcast(&destroyme->q_not_empty);

    for (int i = 0; i < destroyme->num_threads; i++)
        pthread_join(destroyme->threads[i], NULL); //join all threads
    free_threadpool(destroyme); //free all allocated memory and destroy mutex,CVs
}

/**
 * on succsess returns new work_t * object contains the parameters. o.w return NULL
 */
work_t *createWorkObj(threadpool *tp, dispatch_fn dispatch_to_here, void *arg) {
    if (tp == NULL || dispatch_to_here == NULL || arg == NULL)
        return NULL;
    work_t *job = (work_t *) malloc(sizeof(work_t));
    if (job == NULL) {
        free_threadpool(tp);
        return NULL;
    }
    job->routine = dispatch_to_here;
    job->arg = arg;
    job->next = NULL;
    return job;
}

/**
 * insert job into the queue and update qsize.
 */
void enqueue(threadpool *tp, work_t *job) {
    if(tp == NULL || job == NULL)
        return;
    tp->qsize++;
    if (tp->qhead == NULL) {
        /*both head and tail points to the new job*/
        tp->qhead = job;
        tp->qtail = job;
        return;
    }
    /*set current last job to the new job and promote qtail to point the new last job*/
    tp->qtail->next = job;
    tp->qtail = job;

}

/**
 * when queue is not empty, returns the first job from the queue and update qsize.
 */
work_t *dequeue(threadpool *tp) {
    if(tp == NULL)
        return NULL;
    if (tp->qhead == NULL)
        return NULL;
    work_t *temp = tp->qhead;
    tp->qhead = tp->qhead->next;
    if (tp->qhead == NULL) //means that both qhead && qtail point to the one and only job in queue.
        tp->qtail = NULL;
    tp->qsize--;
    return temp;
}

/**
 * free all jobs at the queue
 * */
void free_queue(work_t *w_head) {
    while (w_head != NULL) {
        work_t *curr = w_head->next;
        free(w_head);
        w_head = curr;
    }
}

/**
 * free all needed attributes of threadpool struct and the struct itself
 * */
void free_threadpool(threadpool *tp) {
    if (tp == NULL)
        return;
    if (tp->threads != NULL)
        free(tp->threads);
    pthread_mutex_destroy(&tp->qlock);
    pthread_cond_destroy(&tp->q_empty);
    pthread_cond_destroy(&tp->q_not_empty);
    free_queue(tp->qhead); //just in case of a problem, queue is supposed to be empty already
    free(tp);
}
