/**
 * @file thread_pool.h
 * @brief Thread Pool library header
 *
 */

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <signal.h>

/* 
 * @brief Requires user-defined global for thread sync shutdown
 */
extern volatile sig_atomic_t running;

/* ======= STRUCTS ======= */

typedef struct tpool_t tpool_t;
typedef void *(*job_f) (void *);

/* ===== DEFINITIONS ===== */

#define MAX_WORKERS 25

/* ===== DECLARATIONS ===== */

/**
 * @brief Creates threadpool with
 * range of 1 through MAX_WORKERS
 * @param workers number of workers
 * @return tpool_t* thread pool
 */
tpool_t *tpool_create(size_t workers);

/**
 * @brief add job to queue
 *
 * @param p_pool Thread pool to add jobs to
 * @param job same function prototype as pthread_create
 * @param p_arg arguement used by job
 * @return 0 on success \
 * @return -1 on error
 */
int tpool_add_job(tpool_t * p_pool, job_f job, void * p_arg);

/**
 * @brief Blocks until all threads are joined
 *
 * @param p_pool thread pool
 */
void tpool_wait(tpool_t * p_pool);

/**
 * @brief Destroy thread pool - cancels all active threads and joins them
 *
 * @param p_pool thread pool
 */
void tpool_destroy(tpool_t ** pp_pool);

#endif /* THREAD_POOL_H */

/* END OF FILE */
