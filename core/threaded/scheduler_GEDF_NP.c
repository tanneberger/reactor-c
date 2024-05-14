/**
 * @file
 * @author{Soroush Bateni <soroush@utdallas.edu>}
 * @author{Edward A. Lee <eal@berkeley.edu>}
 * @author{Marten Lohstroh <marten@berkeley.edu>}
 * @copyright (c) 2020-2024, The University of California at Berkeley.
 * License: <a href="https://github.com/lf-lang/reactor-c/blob/main/LICENSE.md">BSD 2-clause</a>
 * @brief Global Earliest Deadline First (GEDF) non-preemptive scheduler for the
 * threaded runtime of the C target of Lingua Franca.
 */
#include "lf_types.h"

#if SCHEDULER == SCHED_GEDF_NP

#ifndef NUMBER_OF_WORKERS
#define NUMBER_OF_WORKERS 1
#endif // NUMBER_OF_WORKERS

#include <assert.h>

#include "low_level_platform.h"
#include "environment.h"
#include "pqueue.h"
#include "reactor_threaded.h"
#include "scheduler_instance.h"
#include "scheduler_sync_tag_advance.h"
#include "scheduler.h"
#include "lf_semaphore.h"
#include "tracepoint.h"
#include "util.h"

/////////////////// Scheduler Private API /////////////////////////
/**
 * @brief Insert 'reaction' into scheduler->triggered_reactions, the reaction queue.
 * @param reaction The reaction to insert.
 */
static inline void _lf_sched_insert_reaction(lf_scheduler_t* scheduler, reaction_t* reaction) {
  LF_PRINT_DEBUG("Scheduler: Locking mutex for reaction queue.");
  LF_MUTEX_LOCK(&scheduler->array_of_mutexes[0]);
  LF_PRINT_DEBUG("Scheduler: Locked mutex for reaction queue.");
  pqueue_insert(((pqueue_t**)scheduler->triggered_reactions)[0], (void*)reaction);
  LF_MUTEX_UNLOCK(&scheduler->array_of_mutexes[0]);
}

/**
 * @brief Distribute any reaction that is ready to execute to idle worker
 * thread(s).
 *
 * @return Number of reactions that were successfully distributed to worker
 * threads.
 */
int _lf_sched_distribute_ready_reactions(lf_scheduler_t* scheduler) {
  pqueue_t* tmp_queue = NULL;
  // Note: All the worker threads are idle, which means that they are done inserting
  // reactions. Therefore, the reaction queue can be accessed without locking
  // a mutex.

  while (scheduler->next_reaction_level <= scheduler->max_reaction_level) {
    LF_PRINT_DEBUG("Waiting with curr_reaction_level %zu.", scheduler->next_reaction_level);
    try_advance_level(scheduler->env, &scheduler->next_reaction_level);

    tmp_queue = ((pqueue_t**)scheduler->triggered_reactions)[scheduler->next_reaction_level - 1];
    size_t reactions_to_execute = pqueue_size(tmp_queue);

    if (reactions_to_execute) {
      scheduler->executing_reactions = tmp_queue;
      return reactions_to_execute;
    }
  }

  return 0;
}

/**
 * @brief If there is work to be done, notify workers individually.
 *
 * This assumes that the caller is not holding any thread mutexes.
 */
void _lf_sched_notify_workers(lf_scheduler_t* scheduler) {
  // Note: All threads are idle. Therefore, there is no need to lock the mutex
  // while accessing the executing queue (which is pointing to one of the
  // reaction queues).
  size_t workers_to_awaken =
      LF_MIN(scheduler->number_of_idle_workers, pqueue_size((pqueue_t*)scheduler->executing_reactions));
  LF_PRINT_DEBUG("Scheduler: Notifying %zu workers.", workers_to_awaken);
  scheduler->number_of_idle_workers -= workers_to_awaken;
  LF_PRINT_DEBUG("Scheduler: New number of idle workers: %zu.", scheduler->number_of_idle_workers);
  if (workers_to_awaken > 1) {
    // Notify all the workers except the worker thread that has called this
    // function.
    lf_semaphore_release(scheduler->semaphore, (workers_to_awaken - 1));
  }
}

/**
 * @brief Signal all worker threads that it is time to stop.
 *
 */
void _lf_sched_signal_stop(lf_scheduler_t* scheduler) {
  scheduler->should_stop = true;
  lf_semaphore_release(scheduler->semaphore, (scheduler->number_of_workers - 1));
}

/**
 * @brief Advance tag or distribute reactions to worker threads.
 *
 * Advance tag if there are no reactions on the reaction queue. If
 * there are such reactions, distribute them to worker threads.
 *
 * This function assumes the caller does not hold the 'mutex' lock.
 */
void _lf_scheduler_try_advance_tag_and_distribute(lf_scheduler_t* scheduler) {
  environment_t* env = scheduler->env;

  // Executing queue must be empty when this is called.
  assert(pqueue_size((pqueue_t*)scheduler->executing_reactions) == 0);

  // Loop until it's time to stop or work has been distributed
  while (true) {
    if (scheduler->next_reaction_level == (scheduler->max_reaction_level + 1)) {
      scheduler->next_reaction_level = 0;
      LF_MUTEX_LOCK(&env->mutex);
      // Nothing more happening at this tag.
      LF_PRINT_DEBUG("Scheduler: Advancing tag.");
      // This worker thread will take charge of advancing tag.
      if (_lf_sched_advance_tag_locked(scheduler)) {
        LF_PRINT_DEBUG("Scheduler: Reached stop tag.");
        _lf_sched_signal_stop(scheduler);
        LF_MUTEX_UNLOCK(&env->mutex);
        break;
      }
      LF_MUTEX_UNLOCK(&env->mutex);
    }

    if (_lf_sched_distribute_ready_reactions(scheduler) > 0) {
      _lf_sched_notify_workers(scheduler);
      break;
    }
  }
}

/**
 * @brief Wait until the scheduler assigns work.
 *
 * If the calling worker thread is the last to become idle, it will call on the
 * scheduler to distribute work. Otherwise, it will wait on
 * 'scheduler->semaphore'.
 *
 * @param worker_number The worker number of the worker thread asking for work
 * to be assigned to it.
 */
void _lf_sched_wait_for_work(lf_scheduler_t* scheduler, size_t worker_number) {
  // Increment the number of idle workers by 1 and check if this is the last
  // worker thread to become idle.
  if (((size_t)lf_atomic_add_fetch32((int32_t*)&scheduler->number_of_idle_workers, 1)) ==
      scheduler->number_of_workers) {
    // Last thread to go idle
    LF_PRINT_DEBUG("Scheduler: Worker %zu is the last idle thread.", worker_number);
    // Call on the scheduler to distribute work or advance tag.
    _lf_scheduler_try_advance_tag_and_distribute(scheduler);
  } else {
    // Not the last thread to become idle.
    // Wait for work to be released.
    LF_PRINT_DEBUG("Scheduler: Worker %zu is trying to acquire the scheduling "
                   "semaphore.",
                   worker_number);
    lf_semaphore_acquire(scheduler->semaphore);
    LF_PRINT_DEBUG("Scheduler: Worker %zu acquired the scheduling semaphore.", worker_number);
  }
}

///////////////////// Scheduler Init and Destroy API /////////////////////////
/**
 * @brief Initialize the scheduler.
 *
 * This has to be called before other functions of the scheduler can be used.
 * If the scheduler is already initialized, this will be a no-op.
 *
 * @param env Environment within which we are executing.
 * @param number_of_workers Indicate how many workers this scheduler will be
 *  managing.
 * @param option Pointer to a `sched_params_t` struct containing additional
 *  scheduler parameters.
 */
void lf_sched_init(environment_t* env, size_t number_of_workers, sched_params_t* params) {
  assert(env != GLOBAL_ENVIRONMENT);

  LF_PRINT_DEBUG("Scheduler: Initializing with %zu workers", number_of_workers);
  if (!init_sched_instance(env, &env->scheduler, number_of_workers, params)) {
    // Already initialized
    return;
  }
  lf_scheduler_t* scheduler = env->scheduler;

  // Just one reaction queue and mutex for each environment.
  scheduler->triggered_reactions = calloc(1, sizeof(pqueue_t*));
  scheduler->array_of_mutexes = (lf_mutex_t*)calloc(1, sizeof(lf_mutex_t));

  // Initialize the reaction queue.
  ((pqueue_t**)scheduler->triggered_reactions)[0] =
      pqueue_init(queue_size, in_reverse_order, get_reaction_index, get_reaction_position, set_reaction_position,
                  reaction_matches, print_reaction);
  // Initialize the mutexes for the reaction queues
  LF_MUTEX_INIT(&scheduler->array_of_mutexes[0]);

  scheduler->executing_reactions = ((pqueue_t**)scheduler->triggered_reactions)[0];
}

/**
 * @brief Free the memory used by the scheduler.
 *
 * This must be called when the scheduler is no longer needed.
 */
void lf_sched_free(lf_scheduler_t* scheduler) {
  // for (size_t j = 0; j <= scheduler->max_reaction_level; j++) {
  //     pqueue_free(scheduler->triggered_reactions[j]);
  //     FIXME: This is causing weird memory errors.
  // }
  pqueue_free((pqueue_t*)scheduler->executing_reactions);
  lf_semaphore_destroy(scheduler->semaphore);
}

///////////////////// Scheduler Worker API (public) /////////////////////////
/**
 * @brief Ask the scheduler for one more reaction.
 *
 * This function blocks until it can return a ready reaction for worker thread
 * 'worker_number' or it is time for the worker thread to stop and exit (where a
 * NULL value would be returned).
 *
 * @param worker_number
 * @return reaction_t* A reaction for the worker to execute. NULL if the calling
 * worker thread should exit.
 */
reaction_t* lf_sched_get_ready_reaction(lf_scheduler_t* scheduler, int worker_number) {
  // Iterate until the stop_tag is reached or reaction queue is empty
  while (!scheduler->should_stop) {
    // Need to lock the mutex for the current level
    size_t current_level = scheduler->next_reaction_level - 1;
    LF_PRINT_DEBUG("Scheduler: Worker %d locking reaction queue mutex.", worker_number);
    LF_MUTEX_LOCK(&scheduler->array_of_mutexes[0]);
    LF_PRINT_DEBUG("Scheduler: Worker %d locked reaction queue mutex.", worker_number);
    reaction_t* reaction_to_return = (reaction_t*)pqueue_pop((pqueue_t*)scheduler->executing_reactions);
    LF_MUTEX_UNLOCK(&scheduler->array_of_mutexes[0]);

    if (reaction_to_return != NULL) {
      // Got a reaction
      return reaction_to_return;
    }

    LF_PRINT_DEBUG("Worker %d is out of ready reactions.", worker_number);

    // Ask the scheduler for more work and wait
    tracepoint_worker_wait_starts(scheduler->env, worker_number);
    _lf_sched_wait_for_work(scheduler, worker_number);
    tracepoint_worker_wait_ends(scheduler->env, worker_number);
  }

  // It's time for the worker thread to stop and exit.
  return NULL;
}

/**
 * @brief Inform the scheduler that worker thread 'worker_number' is done
 * executing the 'done_reaction'.
 *
 * @param worker_number The worker number for the worker thread that has
 * finished executing 'done_reaction'.
 * @param done_reaction The reaction that is done.
 */
void lf_sched_done_with_reaction(size_t worker_number, reaction_t* done_reaction) {
  (void)worker_number;
  if (!lf_atomic_bool_compare_and_swap32((int32_t*)&done_reaction->status, queued, inactive)) {
    lf_print_error_and_exit("Unexpected reaction status: %d. Expected %d.", done_reaction->status, queued);
  }
}

/**
 * @brief Inform the scheduler that worker thread 'worker_number' would like to
 * trigger 'reaction' at the current tag.
 *
 * If a worker number is not available (e.g., this function is not called by a
 * worker thread), -1 should be passed as the 'worker_number'.
 *
 * The scheduler will ensure that the same reaction is not triggered twice in
 * the same tag.
 *
 * @param reaction The reaction to trigger at the current tag.
 * @param worker_number The ID of the worker that is making this call. 0 should
 * be used if there is only one worker (e.g., when the program is using the
 *  single-threaded C runtime). -1 is used for an anonymous call in a context where a
 *  worker number does not make sense (e.g., the caller is not a worker thread).
 */
void lf_scheduler_trigger_reaction(lf_scheduler_t* scheduler, reaction_t* reaction, int worker_number) {
  (void)worker_number;
  if (reaction == NULL || !lf_atomic_bool_compare_and_swap32((int32_t*)&reaction->status, inactive, queued)) {
    return;
  }
  LF_PRINT_DEBUG("Scheduler: Enqueueing reaction %s, which has level %lld.", reaction->name, LF_LEVEL(reaction->index));
  _lf_sched_insert_reaction(scheduler, reaction);
}
#endif // SCHEDULER == SCHED_GEDF_NP
