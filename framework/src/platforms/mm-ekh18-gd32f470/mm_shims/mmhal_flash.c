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
 * 0x08000000 to the end of flash at 0x08100000. The CONFIG region in the linker script spans
 * sectors 1-2 (32K, two independently-erasable 16K sectors -- see bsp/GD32F470VG_FLASH.ld for why
 * this needs two whole sectors rather than one). fmc_bank0_sector_of_address() below resolves the
 * whole bank-0 sector map (not just sectors 1-2), since mmconfig_init() probes both the first AND
 * last byte of the CONFIG partition, and a proper range lookup is no more code than a partial one.
 */

#include <string.h>

#include "mmhal_flash.h"
#include "mmosal.h"
#include "main.h"

/** Base address of GD32F470VG flash bank 0 (the only bank on this part). */
#define FMC_BANK0_BASE (0x08000000UL)

/** Describes one bank-0 flash sector: its start address, size in bytes, and FMC sector number. */
struct fmc_bank0_sector
{
    uint32_t start;
    uint32_t size;
    uint32_t number;
};

/**
 * Find the bank-0 sector containing a given flash address (i.e. @p address falls anywhere within
 * [sector.start, sector.start + sector.size)), or NULL if the address is not in bank 0 at all.
 *
 * NOTE: this used to be an exact-match lookup against each sector's *start* address only, which
 * is wrong -- mmconfig_init() (framework/src/mmconfig/mmconfig.c) calls mmhal_flash_getblocksize()
 * on both the first byte of the mmconfig partition (which does land exactly on a sector start) and
 * the *last* byte of the partition (partition_start + partition_size - 1, which generally does
 * NOT land on a sector start -- e.g. the CONFIG region's last byte 0x0800BFFF falls in the middle
 * of sector 2, which starts at 0x08008000). The exact-match version returned UINT32_MAX for that
 * address, making mmhal_flash_getblocksize() incorrectly return 0 and tripping
 * MMOSAL_ASSERT(mmhal_flash_getblocksize(...) != 0) in mmconfig_init() -- confirmed via real
 * hardware testing (the "Validate BCF" test step in porting_assistant asserted at mmconfig.c:932).
 */
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
    const struct fmc_bank0_sector *sector = fmc_bank0_sector_of_address(block_address);
    if (sector == NULL)
    {
        return 0;
    }

    /* Return the real size of whichever sector contains block_address. mmconfig only ever calls
     * this to (a) probe that an address maps to a real, erasable sector at all (mmconfig_init()'s
     * validity checks -- including on the *last* byte of the partition, hence the range-based
     * lookup above rather than exact-start-address matching), and (b) determine how much to erase
     * starting at a sector-aligned block_address. The CONFIG region (see bsp/GD32F470VG_FLASH.ld)
     * spans sectors 1-2, both 16K, and mmconfig splits CONFIG into two software partitions of half
     * its size each (16K here), so this must equal exactly one physical sector -- otherwise erasing
     * one partition would spill into and corrupt the other (this was a real bug when CONFIG was a
     * single 16K sector split into two 8K partitions). */
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
