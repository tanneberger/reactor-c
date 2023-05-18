#include "scheduler_instance.h"
#include "reactor_common.h"
#include "lf_types.h"


bool init_sched_instance(
    environment_t * env,
    _lf_sched_instance_t** instance,
    size_t number_of_workers,
    sched_params_t* params) {
    // FIXME: env pointer in the sched instance must be set by now
    // Check if the instance is already initialized
    lf_critical_section_enter(env);
    if (*instance != NULL) {
        // Already initialized
        lf_critical_section_exit(env);
        return false;
    } else {
        *instance =
            (_lf_sched_instance_t*)calloc(1, sizeof(_lf_sched_instance_t));
    }
    lf_mutex_unlock(&env->mutex);

    if (params == NULL || params->num_reactions_per_level_size == 0) {
        (*instance)->max_reaction_level = DEFAULT_MAX_REACTION_LEVEL;
    }

    if (params != NULL) {
        if (params->num_reactions_per_level != NULL) {
            (*instance)->max_reaction_level =
                params->num_reactions_per_level_size - 1;
        }
    }

    (*instance)->_lf_sched_semaphore = lf_semaphore_new(0);
    (*instance)->_lf_sched_number_of_workers = number_of_workers;
    (*instance)->_lf_sched_next_reaction_level = 1;

    (*instance)->_lf_sched_should_stop = false;
    (*instance)->env = env;

    return true;
}
