#if defined(PLATFORM_RIOT)
/*************
Copyright (c) 2024, TUD Dresden University of Technology

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************/
/**
 * @brief Zephyr support for the C target of Lingua Franca.
 *
 * @author{Erling Jellum <erling.r.jellum@ntnu.no>}
 * @author{Marten Lohstroh <marten@berkeley.edu>}
 */
#include <time.h>
#include <errno.h>

#include "platform/lf_riot_support.h"
#include "low_level_platform.h"
#include "tag.h"

#include <riot/irq.h>
#include <riot/ztimer.h>

// Keep track of nested critical sections
static uint32_t num_nested_critical_sections = 0;
// Keep track of IRQ mask when entering critical section so we can enable again after
static volatile unsigned irq_mask = 0;

int lf_sleep(interval_t sleep_duration) { 
  ztimer_t timeout = {};
  ztimer_set(ZTIMER_SEC, &timeout, 2);
 
  ztimer_sleep(ZTIMER_USEC, sleep_duration);
  return 0;
}

int lf_nanosleep(interval_t sleep_duration) {
  return lf_sleep(sleep_duration); 
}

int lf_disable_interrupts_nested() {
  if (num_nested_critical_sections++ == 0) {
    irq_disable();
  }
  return 0;
}

int lf_enable_interrupts_nested() {
  if (num_nested_critical_sections <= 0) {
    return 1;
  }

  if (--num_nested_critical_sections == 0) {
    irq_enable();
  }
  return 0;
}


int lf_mutex_init(lf_mutex_t* mutex) { 
  return mutex_init(mutex); 
}

int lf_mutex_lock(lf_mutex_t* mutex) {
  mutex_lock(mutex);
  return 1;
}

int lf_mutex_unlock(lf_mutex_t* mutex) {
  k_mutex_unlock(mutex);
  return 1;
}

#endif
