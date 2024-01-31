/**
 * @file
 * @author Benjamin Asch
 * @author Edward A. Lee
 * @author Erling Jellum
 * @copyright (c) 2023, The University of California at Berkeley.
 * License: <a href="https://github.com/lf-lang/reactor-c/blob/main/LICENSE.md">BSD 2-clause</a>
 * @brief Definitions for watchdogs.
 */

#include <assert.h>
#include "watchdog.h"
#include "environment.h"
#include "util.h"

/**
 * @brief Initialize watchdog mutexes.
 * For any reactor with one or more watchdogs, the self struct should have a non-NULL
 * `reactor_mutex` field which points to an instance of `lf_mutex_t`.
 * This function initializes those mutexes. It also initializes the condition
 * variable which enables the safe termination of a running watchdog.
 */
void _lf_initialize_watchdogs(environment_t *env) {
    int ret;            
    for (int i = 0; i < env->watchdogs_size; i++) {
        watchdog_t *watchdog = env->watchdogs[i];
        if (watchdog->base->reactor_mutex != NULL) {
            ret = lf_mutex_init((lf_mutex_t*)(watchdog->base->reactor_mutex));
            LF_ASSERTN(ret, "lf_mutex_init failed");
        }
        ret = lf_cond_init(&watchdog->cond, watchdog->base->reactor_mutex);
        LF_ASSERTN(ret, "lf_cond_init failed");
    }
}

/**
 * @brief Terminate all watchdog threads. 
 */
void _lf_watchdog_terminate(environment_t *env) {
    void *thread_return;
    int ret;
    for (int i = 0; i < env->watchdogs_size; i++) {
        watchdog_t *watchdog = env->watchdogs[i];
        ret = lf_mutex_lock(watchdog->base->reactor_mutex);
        LF_ASSERTN(ret, "lf_mutex_lock failed");
        lf_watchdog_stop(watchdog);
        ret = lf_mutex_unlock(watchdog->base->reactor_mutex);
        LF_ASSERTN(ret, "lf_mutex_unlock failed");
        ret = lf_thread_join(watchdog->thread_id, &thread_return);
        LF_ASSERTN(ret, "lf_thread_join failed");
    }
}

void watchdog_wait(watchdog_t *watchdog) {
    watchdog->active = true;
    instant_t physical_time = lf_time_physical();
    while ( watchdog->expiration != NEVER && 
            physical_time < watchdog->expiration &&
            !watchdog->terminate) {
        // Wait for expiration, or a signal to terminate
        lf_cond_timedwait(&watchdog->cond, watchdog->expiration);
        physical_time = lf_time_physical();
    }
}

/**
 * @brief Thread function for watchdog.
 * Each watchdog has a thread which sleeps until one out of two scenarios:
 * 1) The watchdog timeout expires and there has not been a renewal of the watchdog budget.
 * 2) The watchdog is signaled to wake up and terminate.
 * In normal usage, the expiration time is incremented while the thread is
 * sleeping, so when the thread wakes up, it can go back to sleep again.
 * If the watchdog does expire. It will execute the watchdog handler and the 
 * thread will terminate. To stop the watchdog, another thread will signal the
 * condition variable, in that case the watchdog thread will terminate directly.
 * The expiration field of the watchdog is used to protect against race conditions.
 * It is set to NEVER when the watchdog is terminated.
 * 
 * @param arg A pointer to the watchdog struct
 * @return NULL
 */
void* watchdog_thread_main(void* arg) {
    int ret;
    watchdog_t* watchdog = (watchdog_t*)arg;
    self_base_t* base = watchdog->base;
    LF_PRINT_DEBUG("Starting Watchdog %p", watchdog);
    LF_ASSERT(base->reactor_mutex, "reactor-mutex not alloc'ed but has watchdogs.");

    // Grab reactor-mutex and start infinite loop.
    ret = lf_mutex_lock((lf_mutex_t*)(base->reactor_mutex));
    LF_ASSERTN(ret, "lf_mutex_lock failed");
    while (true) {

        // Step 1: Wait for a timeout to start watching for.
        
        // Edge case 1: We have already gotten a signal to terminate.
        if (watchdog->terminate) {
            goto terminate;
        }

        // Edge case 2: We have already received a timeout.
        if(watchdog->expiration != NEVER) {
            watchdog_wait(watchdog);
        } else {
            // Wait for a signal that we have a timeout to wait for on the cond-var.
            do {
                lf_cond_wait(watchdog->cond);
            } while (watchdog->expiration == NEVER && !watchdog->terminate);
            
            // Check whether we actually got a termination signal.
            if (watchdog->terminate) {
                goto terminate;
            }

            // Finally go wait for that timeout.
            watchdog_wait(watchdog);
        }

        // At this point we have returned from the watchdog wait. But it could
        // be that it was to terminate the watchdog.
        if (watchdog->terminate) {
            goto terminate;
        }

        // It could also be that the watchdog was just stopped
        if (watchdog->expirateion == NEVER) {
            continue;
        }

        // Finally. The watchdog actually timed out. Handle it.
        LF_PRINT_DEBUG("Watchdog %p timed out", watchdog);
        watchdog_function_t watchdog_func = watchdog->watchdog_function;
        (*watchdog_func)(base);

        watchdog->active = false;
    }

terminate:
    // Here the thread terminates. 
    watchdog->active = false;
    ret = lf_mutex_unlock(base->reactor_mutex);
    LF_ASSERTN(ret, "lf_mutex_unlock failed");
    return NULL;
}

void lf_watchdog_start(watchdog_t* watchdog, interval_t additional_timeout) {
    // Assumes reactor mutex is already held.
    self_base_t* base = watchdog->base;
    watchdog->terminate = false;
    watchdog->expiration = base->environment->current_tag.time + watchdog->min_expiration + additional_timeout;

    // If the watchdog is inactive, signal it to start waiting.
    if (!watchdog->active) {
        ret = lf_cond_signal(watchdog->cond);
        LF_ASSERTN(ret, "lf_conf_signal failed");
    } 
}

void lf_watchdog_stop(watchdog_t* watchdog) {
    int ret;
    // If the watchdog isnt active, then it is no reason to stop it.
    if (!watchdog->active) {
        return;
    }

    // Assumes reactor mutex is already held.
    watchdog->expiration = NEVER;
    ret = lf_cond_signal(&watchdog->cond);
    LF_ASSERTN(ret, "lf_conf_signal failed");
}


void _lf_watchdog_terminate(watchdog_t* watchdog) {
    watchdog->terminate = true;
    watchdog->expiration = NEVER;
    int ret = lf_cond_signal(watchdog->cond);
    LF_ASSERTN(ret, "lf_cond_signal failed");

}