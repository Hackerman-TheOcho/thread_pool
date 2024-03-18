/**
 * @file thread_pool.c
 * @brief Thread Pool library module
 *
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

#include "thread_pool.h"

/* ====== DEFINITIONS ====== */

#define DELAY 10000000

/* ====== STRUCTURES ======*/

typedef struct task_t
{
    job_f job;
    void *p_arg;
} task_t;

typedef struct node_t
{
    struct node_t *p_next;
    task_t        *p_job;
} node_t;

typedef struct queue_t
{
    node_t         *p_head;
    node_t         *p_tail;
    pthread_cond_t  cond;
    pthread_mutex_t lock;
} queue_t;

struct tpool_t
{
    size_t     workers;
    queue_t   *p_jobs;
    pthread_t *p_ids;
};

/* ===== DECLARATIONS ===== */

static queue_t *queue_create(void);
static void     queue_append(queue_t * queue, task_t * p_job);
static void     queue_destroy(queue_t ** pp_queue);
static void    *worker(void *);
static task_t  *queue_extract(queue_t * p_queue);

/* ====== FUNCTIONS ====== */

static void*
worker(void * p_data)
{
    queue_t        *p_queue = (queue_t *)p_data;
    task_t         *p_task = NULL;
    struct timespec delay = {0, 0};

    for (;;)
    {
        /* lock mutex */
        pthread_mutex_lock(&p_queue->lock);

        /* delay */
        clock_gettime(CLOCK_REALTIME, &delay);
        delay.tv_nsec += DELAY;
        pthread_cond_timedwait(&p_queue->cond, &p_queue->lock, &delay);

        /* get task */
        p_task = queue_extract(p_queue);

        /* unlock mutex */
        pthread_mutex_unlock(&p_queue->lock);

        if (p_task)
        {
            p_task->job(p_task->p_arg);
            free(p_task);
        }
        else if (!running)
        {
            break;
        }
        else
        {
            /* BARR-C Requirement */
        }
    }

    return NULL;
} /* worker */

tpool_t *
tpool_create(size_t workers)
{
    if (!workers || MAX_WORKERS < workers)
    {
        fprintf(stderr, "Num workers must be in range 1 - %d\n", MAX_WORKERS);
        goto error;
    }

    tpool_t *p_pool = calloc(1, sizeof(*p_pool));
    if (!p_pool)
    {
        perror("Thread pool creation failed\n");
        goto error;
    }

    p_pool->p_jobs = queue_create();
    if (!p_pool->p_jobs)
    {
        perror("Failed to create queue\n");
        free(p_pool);
        goto error;
    }

    p_pool->p_ids = calloc(workers, sizeof(pthread_t));
    if (!p_pool->p_ids)
    {
        perror("Failed to create threads\n");
        free(p_pool->p_jobs);
        free(p_pool);
        goto error;
    }

    p_pool->workers = workers;
    for (size_t id = 0; id < workers; id++)
    {
        pthread_create(p_pool->p_ids + id, NULL, worker, p_pool->p_jobs);
    }

error:
    p_pool = NULL;
    errno  = 0;

leave:
    return p_pool;
}/* tpool_create */

int
tpool_add_job(tpool_t * p_pool, job_f job, void * p_arg)
{
    int ret_val = 0;

    if (!p_pool || !job)
    {
        ret_val = -1;
    }

    task_t *p_task = calloc(1, sizeof(*p_task));
    if (!p_task)
    {
        perror("Failed to add job\n");
        errno   = 0;
        ret_val = -1;
    }

    p_task->job   = job;
    p_task->p_arg = p_arg;

    queue_append(p_pool->p_jobs, p_task);
    pthread_cond_signal(&p_pool->p_jobs->cond);

    return ret_val;
} /* tpool_add_job */

void
tpool_wait(tpool_t * p_pool)
{
    if (!p_pool)
    {
        return;
    }

    pthread_cond_broadcast(&p_pool->p_jobs->cond);
    for (size_t idx = 0; idx < p_pool->workers; idx++)
    {
        pthread_join(p_pool->p_ids[idx], NULL);
        p_pool->p_ids[idx] = 0;
    }

} /* tpool_wait */

void
tpool_destroy(tpool_t ** pp_pool)
{
    if (!pp_pool || !*pp_pool)
    {
        return;
    }

    pthread_cond_broadcast(&(*pp_pool)->p_jobs->cond);
    for (size_t idx = 0; idx < (*pp_pool)->workers; idx++)
    {
        if ((*pp_pool)->p_ids[idx])
        {
            pthread_cancel((*pp_pool)->p_ids[idx]);
            pthread_join((*pp_pool)->p_ids[idx], NULL);
        }
    }

    queue_destroy(&(*pp_pool)->p_jobs);
    free((*pp_pool)->p_jobs);
    free(*pp_pool);

    *pp_pool = NULL;
}/* tpool_destroy */


static queue_t*
queue_create(void)
{
    queue_t *p_queue = calloc(1, sizeof(*p_queue));
    if (p_queue)
    {
        pthread_mutex_init(&p_queue->lock, NULL);
        pthread_cond_init(&p_queue->cond, NULL);
    }

    return p_queue;
}/* queue_create */

static void
queue_destroy(queue_t ** pp_queue)
{
    node_t *p_node = (*pp_queue)->p_head;
    node_t *p_temp = NULL;

    /* lock */
    pthread_mutex_lock(&(*pp_queue)->lock);

    while (p_node) 
    {
        p_temp = p_node;
        p_node = p_node->p_next;

        free(p_temp->p_job);
        p_temp->p_next = NULL;
        free(p_temp);
    }

    /* unlock */
    pthread_mutex_unlock(&(*pp_queue)->lock);

    pthread_mutex_destroy(&(*pp_queue)->lock);
    pthread_cond_destroy(&(*pp_queue)->cond);

    (*pp_queue)->p_head = NULL;
    (*pp_queue)->p_tail = NULL;

    free(*pp_queue);

    *pp_queue = NULL;   
} /* queue_destroy */

static void
queue_append(queue_t * p_queue, task_t * p_job)
{
    struct node_t *p_node = calloc(1, sizeof(*p_node));
    if (!p_node) 
    {
        return;
    }

    p_node->p_job  = p_job;
    p_node->p_next = NULL;

    /* lock */
    pthread_mutex_lock(&p_queue->lock);

    if (p_queue->p_tail) 
    {
        p_queue->p_tail->p_next = p_node;
    } 
    else 
    {
        p_queue->p_head = p_node;
    }

    p_queue->p_tail = p_node;

    /* unlock */
    pthread_mutex_unlock(&p_queue->lock);
}/* queue_append */

static task_t*
queue_extract(queue_t * p_queue)
{
    task_t *p_job = NULL;

    if (p_queue->p_head) 
    {
        node_t *p_temp  = p_queue->p_head;
        p_job           = p_temp->p_job;
        p_queue->p_head = p_temp->p_next;

        if (!p_queue->p_head) 
        {
            p_queue->p_tail = NULL;
        }

        p_temp->p_next = NULL;
        p_temp->p_job  = NULL;

        free(p_temp);
    }

    return p_job;    
} /* queue_extract */

/* END OF FILE */
