/*
 * Copyright 2026 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 * @file
 * This file implements BSP specific shims for accessing flash, for the mmconfig partition. See
 * bsp/GD32F470VG_FLASH.ld for where the CONFIG region is located.
 *
 * GD32F470VG has 1024KB (1MB) of flash, entirely in bank 0 (bank 0 is always exactly 1MB on
 * GD32F4xx parts with >=1MB flash -- see the GD32F4xx User Manual; there is no bank 1 on this
 * part since total flash == bank 0's fixed size). The bank 0 sector map (confirmed against
 * GigaDevice's real gd32f4xx_fmc.h/.c, and standard across the whole GD32F4xx/STM32F4 family) is:
 * sectors 0-3 are 16K, sector 4 is 64K, and sectors 5-11 are 128K each, running contiguously from
 * 0x08000000 to the end of flash at 0x08100000. The CONFIG region in the linker script is sized to
 * land exactly on sector 1 (16K), which is the only range fmc_bank0_sector_of_address() below
 * needs to resolve.
 */

#include <string.h>

#include "mmhal_flash.h"
#include "mmosal.h"
#include "main.h"

/** Base address of GD32F470VG flash bank 0 (the only bank on this part). */
#define FMC_BANK0_BASE (0x08000000UL)

/**
 * Map a flash address within bank 0 to its FMC sector number (CTL_SECTOR_NUMBER_* /
 * CTL_SN()-encoded, as expected by fmc_sector_erase()), or UINT32_MAX if the address does not
 * fall on a bank-0 sector boundary this platform knows how to erase.
 */
static uint32_t fmc_bank0_sector_of_address(uint32_t address)
{
    static const uint32_t sector_starts[] =
    {
        FMC_BANK0_BASE + 0x00000U, /* sector 0:  16K */
        FMC_BANK0_BASE + 0x04000U, /* sector 1:  16K */
        FMC_BANK0_BASE + 0x08000U, /* sector 2:  16K */
        FMC_BANK0_BASE + 0x0C000U, /* sector 3:  16K */
        FMC_BANK0_BASE + 0x10000U, /* sector 4:  64K */
        FMC_BANK0_BASE + 0x20000U, /* sector 5: 128K */
        FMC_BANK0_BASE + 0x40000U, /* sector 6: 128K */
        FMC_BANK0_BASE + 0x60000U, /* sector 7: 128K */
        FMC_BANK0_BASE + 0x80000U, /* sector 8: 128K */
        FMC_BANK0_BASE + 0xA0000U, /* sector 9: 128K */
        FMC_BANK0_BASE + 0xC0000U, /* sector 10: 128K */
        FMC_BANK0_BASE + 0xE0000U, /* sector 11: 128K */
    };
    static const uint32_t sector_numbers[] =
    {
        CTL_SECTOR_NUMBER_0, CTL_SECTOR_NUMBER_1, CTL_SECTOR_NUMBER_2, CTL_SECTOR_NUMBER_3,
        CTL_SECTOR_NUMBER_4, CTL_SECTOR_NUMBER_5, CTL_SECTOR_NUMBER_6, CTL_SECTOR_NUMBER_7,
        CTL_SECTOR_NUMBER_8, CTL_SECTOR_NUMBER_9, CTL_SECTOR_NUMBER_10, CTL_SECTOR_NUMBER_11,
    };
    size_t ii;

    for (ii = 0; ii < sizeof(sector_starts) / sizeof(sector_starts[0]); ii++)
    {
        if (address == sector_starts[ii])
        {
            return sector_numbers[ii];
        }
    }

    return UINT32_MAX;
}

const struct mmhal_flash_partition_config *mmhal_get_mmconfig_partition(void)
{
    /** Start of MMCONFIG region in flash. */
    extern uint8_t mmconfig_start;

    /** End of MMCONFIG region in flash. */
    extern uint8_t mmconfig_end;

    static struct mmhal_flash_partition_config mmconfig_partition =
        MMHAL_FLASH_PARTITION_CONFIG_DEFAULT;

    mmconfig_partition.partition_start = (uint32_t)&mmconfig_start;
    mmconfig_partition.partition_size = (uint32_t)(&mmconfig_end - &mmconfig_start);
    mmconfig_partition.not_memory_mapped = false;

    return &mmconfig_partition;
}

uint32_t mmhal_flash_getblocksize(uint32_t block_address)
{
    uint32_t sector = fmc_bank0_sector_of_address(block_address);
    if (sector == UINT32_MAX)
    {
        return 0;
    }

    /* mmconfig only ever erases whole sectors starting at block_address, and the CONFIG region in
     * the linker script (see bsp/GD32F470VG_FLASH.ld) is exactly one 16K sector (sector 1), so
     * hard-code 16K here rather than building a full sector-size lookup. */
    return 16 * 1024;
}

int mmhal_flash_erase(uint32_t block_address)
{
    uint32_t sector = fmc_bank0_sector_of_address(block_address);
    if (sector == UINT32_MAX)
    {
        return -1;
    }

    fmc_unlock();
    fmc_state_enum state = fmc_sector_erase(sector);
    fmc_lock();

    return (state == FMC_READY) ? 0 : -1;
}

int mmhal_flash_read(uint32_t read_address, uint8_t *buf, size_t size)
{
    /* Flash is memory mapped, so a simple memcpy suffices. */
    memcpy(buf, (void *)read_address, size);
    return 0;
}

int mmhal_flash_write(uint32_t write_address, const uint8_t *data, size_t size)
{
    int retval = 0;

    fmc_unlock();

    /* GD32F4xx FMC (like STM32F4) programs a word (4 bytes) at a time. Handle any unaligned
     * leading/trailing bytes by doing a read-modify-write of the containing word. */
    if (write_address & 0x3U)
    {
        uint32_t word_address = write_address & ~0x3U;
        uint32_t word_data;
        memcpy(&word_data, (void *)word_address, 4);

        uint8_t *word_bytes = (uint8_t *)&word_data;
        uint32_t byte_offset = write_address & 0x3U;

        while (byte_offset < 4U && size > 0U)
        {
            word_bytes[byte_offset++] = *data++;
            size--;
            write_address++;
        }

        if (fmc_word_program(word_address, word_data) != FMC_READY)
        {
            retval = -1;
        }
    }

    while (retval == 0 && size >= 4U)
    {
        uint32_t word_data;
        memcpy(&word_data, data, 4);
        if (fmc_word_program(write_address, word_data) != FMC_READY)
        {
            retval = -1;
            break;
        }
        write_address += 4;
        data += 4;
        size -= 4;
    }

    if (retval == 0 && size != 0U)
    {
        uint32_t word_data;
        memcpy(&word_data, (void *)write_address, 4);

        uint8_t *word_bytes = (uint8_t *)&word_data;
        size_t i;
        for (i = 0; i < size; i++)
        {
            word_bytes[i] = *data++;
        }

        if (fmc_word_program(write_address, word_data) != FMC_READY)
        {
            retval = -1;
        }
    }

    fmc_lock();

    return retval;
}
