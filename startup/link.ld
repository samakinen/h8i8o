/* SPDX-License-Identifier: GPL-3.0-or-later */
/* H8i8o (GD32F103CB) memory map (docs/firmware.md).
 *
 * Flash: 120 KiB for the image, then 8 KiB for persistence (FlashLog, two
 * banks of four 1 KiB pages at 0x0801E000; bsp/gd32_fmc.h). The image cannot
 * grow into the persistence region: the FLASH region ends where it starts.
 *
 * RAM: the main stack at the bottom, then .data (with .ramfunc, the flash
 * erase wait), .bss and the heap (locked once the stack has started) up to the
 * end. The stack grows down towards the start of RAM: a stack that overflows
 * its reserve runs into the reserved addresses below 0x20000000 and faults,
 * and the watchdog resets the device, instead of overwriting the heap.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 120K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

ENTRY(Reset_Handler)
_Stack_Size = 3072;

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4);
        __preinit_array_start = .; KEEP(*(.preinit_array)) __preinit_array_end = .;
        __init_array_start = .;    KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
        __fini_array_start = .;    KEEP(*(.fini_array)) KEEP(*(SORT(.fini_array.*))) __fini_array_end = .;
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }

    .stack (NOLOAD) : {
        _sstack = .;
        . += _Stack_Size;
        . = ALIGN(8);
        _estack = .;
    } > RAM

    _sidata = LOADADDR(.data);
    .data : {
        . = ALIGN(4); _sdata = .;
        *(.ramfunc .ramfunc.*)
        *(.data .data.*)
        . = ALIGN(4); _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        . = ALIGN(4); _sbss = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(4); _ebss = .;
    } > RAM

    .heap (NOLOAD) : {
        . = ALIGN(8); _heap_start = .;
    } > RAM

    _heap_limit = ORIGIN(RAM) + LENGTH(RAM);

    ASSERT(_sstack == ORIGIN(RAM), "the stack must start at the bottom of RAM")
    ASSERT(_heap_start <= _heap_limit, "static RAM leaves no room for the heap")
}
