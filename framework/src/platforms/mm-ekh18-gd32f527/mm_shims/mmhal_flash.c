/*
 * Copyright 2026 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * mmhal_flash.c for mm-ekh18-gd32f527.
 *
 * GD32F527ZMT7 has 4096KB of flash in a single bank. Bank 0 uses the standard GD32F4xx/STM32F4
 * sector layout for the first 1MB: sectors 0-3 are 16K, sector 4 is 64K, sectors 5-11 are
 * 128K each (identical to GD32F470). The CONFIG region (see bsp/GD32F527ZM_FLASH.ld) spans
 * sectors 1-2 (32K total, two independently-erasable 16K sectors).
 */

#include <string.h>

#include "mmhal_flash.h"
#include "mmosal.h"
#include "main.h"

#define FMC_BANK0_BASE (0x08000000UL)

struct fmc_bank0_sector
{
    uint32_t start;
    uint32_t size;
    uint32_t number;
};

static const struct fmc_bank0_sector *fmc_bank0_sector_of_address(uint32_t address)
{
    static const struct fmc_bank0_sector sectors[] =
    {
        { FMC_BANK0_BASE + 0x00000U, 16 * 1024,  CTL_SECTOR_NUMBER_0  }, /* sector 0:  16K */
        { FMC_BANK0_BASE + 0x04000U, 16 * 1024,  CTL_SECTOR_NUMBER_1  }, /* sector 1:  16K */
        { FMC_BANK0_BASE + 0x08000U, 16 * 1024,  CTL_SECTOR_NUMBER_2  }, /* sector 2:  16K */
        { FMC_BANK0_BASE + 0x0C000U, 16 * 1024,  CTL_SECTOR_NUMBER_3  }, /* sector 3:  16K */
        { FMC_BANK0_BASE + 0x10000U, 64 * 1024,  CTL_SECTOR_NUMBER_4  }, /* sector 4:  64K */
        { FMC_BANK0_BASE + 0x20000U, 128 * 1024, CTL_SECTOR_NUMBER_5  }, /* sector 5: 128K */
        { FMC_BANK0_BASE + 0x40000U, 128 * 1024, CTL_SECTOR_NUMBER_6  }, /* sector 6: 128K */
        { FMC_BANK0_BASE + 0x60000U, 128 * 1024, CTL_SECTOR_NUMBER_7  }, /* sector 7: 128K */
        { FMC_BANK0_BASE + 0x80000U, 128 * 1024, CTL_SECTOR_NUMBER_8  }, /* sector 8: 128K */
        { FMC_BANK0_BASE + 0xA0000U, 128 * 1024, CTL_SECTOR_NUMBER_9  }, /* sector 9: 128K */
        { FMC_BANK0_BASE + 0xC0000U, 128 * 1024, CTL_SECTOR_NUMBER_10 }, /* sector 10: 128K */
        { FMC_BANK0_BASE + 0xE0000U, 128 * 1024, CTL_SECTOR_NUMBER_11 }, /* sector 11: 128K */
    };
    size_t ii;

    for (ii = 0; ii < sizeof(sectors) / sizeof(sectors[0]); ii++)
    {
        uint32_t sector_end = sectors[ii].start + sectors[ii].size;
        if (address >= sectors[ii].start && address < sector_end)
        {
            return &sectors[ii];
        }
    }

    return NULL;
}

const struct mmhal_flash_partition_config *mmhal_get_mmconfig_partition(void)
{
    extern uint8_t mmconfig_start;
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
    const struct fmc_bank0_sector *sector = fmc_bank0_sector_of_address(block_address);
    if (sector == NULL)
    {
        return 0;
    }
    return sector->size;
}

int mmhal_flash_erase(uint32_t block_address)
{
    const struct fmc_bank0_sector *sector = fmc_bank0_sector_of_address(block_address);
    if (sector == NULL)
    {
        return -1;
    }

    fmc_unlock();
    fmc_state_enum state = fmc_sector_erase(sector->number);
    fmc_lock();

    return (state == FMC_READY) ? 0 : -1;
}

int mmhal_flash_read(uint32_t read_address, uint8_t *buf, size_t size)
{
    memcpy(buf, (void *)read_address, size);
    return 0;
}

int mmhal_flash_write(uint32_t write_address, const uint8_t *data, size_t size)
{
    int retval = 0;

    fmc_unlock();

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
