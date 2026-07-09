/*
 * Copyright 2026 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * mmhal_core.c for mm-ekh18-gd32f470. Random number generation uses the GD32F4xx TRNG, via
 * GigaDevice's real gd32f4xx_trng driver (framework/src/bsps/gd32f4xx/Drivers/
 * GD32F4xx_standard_peripheral/{Include,Source}/gd32f4xx_trng.{h,c}).
 */

#include <stdatomic.h>

#include "mmhal_core.h"
#include "mm_hal_common.h"
#include "mmosal.h"

#include "main.h"

static volatile atomic_uint_fast32_t deep_sleep_vetos = 0;
static struct mmosal_mutex *rng_mutex = NULL;

void mmhal_random_init(void)
{
    MMOSAL_DEV_ASSERT(rng_mutex == NULL);
    if (rng_mutex != NULL)
    {
        return;
    }
    rng_mutex = mmosal_mutex_create("rng");
    MMOSAL_ASSERT(rng_mutex != NULL);
}

uint32_t mmhal_random_u32(uint32_t min, uint32_t max)
{
    mmosal_mutex_get(rng_mutex, UINT32_MAX);

    rcu_periph_clock_enable(RCU_TRNG);
    trng_enable();

    /* Wait until valid random data is available. */
    while (trng_flag_get(TRNG_FLAG_DRDY) == RESET)
    {
        /* Check if error occurs */
        if ((trng_flag_get(TRNG_FLAG_CECS) != RESET) ||
            (trng_flag_get(TRNG_FLAG_SECS) != RESET))
        {
            /* Clear error flags by disabling/re-enabling the TRNG. */
            trng_disable();
            trng_enable();
        }
    }

    uint32_t rand = trng_get_true_random_data();

    trng_disable();

    mmosal_mutex_release(rng_mutex);

    if (min != 0 || max != UINT32_MAX)
    {
        /* Warning: this does not guarantee uniformly distributed random numbers */
        rand = rand % (max - min + 1) + min;
    }

    return rand;
}

void mmhal_set_deep_sleep_veto(uint8_t veto_id)
{
    MMOSAL_ASSERT(veto_id < 32);
    atomic_fetch_or(&deep_sleep_vetos, 1ul << veto_id);
}

void mmhal_clear_deep_sleep_veto(uint8_t veto_id)
{
    MMOSAL_ASSERT(veto_id < 32);
    atomic_fetch_and(&deep_sleep_vetos, ~(1ul << veto_id));
}

uint32_t mmhal_get_deep_sleep_veto(void)
{
    return deep_sleep_vetos;
}
