/* STM32F407VG linker script (silicon sizes, valid on both Renode and hardware).
 * Based on the layout of ST's STM32CubeF4 GCC templates.
 *   FLASH 1 MiB, SRAM1+2 128 KiB (DMA-capable), CCM 64 KiB (CPU only - NO DMA on silicon).
 * Sections:
 *   .noinit   SRAM, never zeroed: reset/crash record survives warm resets.
 *   .msp      CCM bottom, 2 KiB main/ISR stack (PRD §8.5). Overflow runs off 0x10000000 into
 *             unmapped space and faults instead of silently corrupting data.
 *   .ccm      CCM, NOLOAD, not zeroed: task stacks only (FreeRTOS fills them itself).
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM   (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
  CCM   (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

_Min_Stack_Size = 0x800;               /* 2 KiB MSP (main + ISR stack) */
__msp_limit = ORIGIN(CCM);
_estack = ORIGIN(CCM) + _Min_Stack_Size;

SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text) *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame)
    KEEP(*(.init)) KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH

  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } >FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } >FLASH

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data) *(.data*) *(.RamFunc) *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .; __bss_start__ = _sbss;
    *(.bss) *(.bss*) *(COMMON)
    . = ALIGN(4);
    _ebss = .; __bss_end__ = _ebss;
  } >RAM

  .noinit (NOLOAD) : { . = ALIGN(4); *(.noinit) *(.noinit*) . = ALIGN(4); } >RAM

  /* No heap: newlib's sbrk is replaced by a trapping stub (syscalls.c). */

  .msp (NOLOAD) : { . = . + _Min_Stack_Size; } >CCM
  .ccm (NOLOAD) : { . = ALIGN(8); *(.ccm) *(.ccm*) . = ALIGN(8); } >CCM

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
