/*************
Copyright (c) 2023, Norwegian University of Science and Technology.

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
 * @brief Zephyr support for reactor-c
 *
 * @author{Erling Jellum <erling.r.jellum@ntnu.no>}
 */

#ifndef LF_ZEPHYR_SUPPORT_H
#define LF_ZEPHYR_SUPPORT_H

#include <stdint.h> // For fixed-width integral types
#include <time.h>   // For CLOCK_MONOTONIC
#include <stdbool.h>
#include <stdlib.h> //malloc, calloc, free, realloc

#include <zephyr/kernel.h>

#define NO_TTY

#define PRINTF_TIME "%" PRIu64
#define PRINTF_MICROSTEP "%" PRIu32
#define PRINTF_TAG "(" PRINTF_TIME ", " PRINTF_MICROSTEP ")"
#define _LF_TIMEOUT 1

/**
 * Time instant. Both physical and logical times are represented
 * using this typedef.
 */
typedef int64_t _instant_t;

/**
 * Interval of time.
 */
typedef int64_t _interval_t;

/**
 * Microstep instant.
 */
typedef uint32_t _microstep_t;

#ifdef LF_THREADED
#warning "Threaded support on Zephyr is still experimental"

typedef struct k_mutex _lf_mutex_t;
typedef struct k_condvar _lf_cond_t;
typedef k_tid_t _lf_thread_t;

extern _lf_mutex_t mutex;
extern _lf_cond_t event_q_changed;

/**
 * @brief Add `value` to `*ptr` and return original value of `*ptr` 
 */
int _zephyr_atomic_fetch_add(int *ptr, int value);
/**
 * @brief Add `value` to `*ptr` and return new updated value of `*ptr`
 */
int _zephyr_atomic_add_fetch(int *ptr, int value);

/**
 * @brief Compare and swap for boolaen value.
 * If `*ptr` is equal to `value` then overwrite it 
 * with `newval`. If not do nothing. Retruns true on overwrite.
 */
bool _zephyr_bool_compare_and_swap(bool *ptr, bool value, bool newval);

/**
 * @brief Compare and swap for integers. If `*ptr` is equal
 * to `value`, it is updated to `newval`. The function returns
 * the original value of `*ptr`.
 */
int  _zephyr_val_compare_and_swap(int *ptr, int value, int newval);

#endif // LF_THREADED



#endif // LF_ZEPHYR_SUPPORT_H