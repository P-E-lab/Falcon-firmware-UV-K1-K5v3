/*
******************************************************************************
**

**  File        : LinkerScript.ld
**
**  Author		: Puya_HC
**
**  Abstract    : Linker script for PY32F071xB series
**                128Kbytes FLASH and 16Kbytes RAM
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used.
**
**  Target      : Puya PY32
**
**  Distribution: The file is distributed “as is,” without any warranty
**                of any kind.
**
*****************************************************************************
** @attention
**
** <h2><center>&copy; COPYRIGHT(c) 2019 Puya Semiconductor</center></h2>
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**   1. Redistributions of source code must retain the above copyright notice,
**      this list of conditions and the following disclaimer.
**   2. Redistributions in binary form must reproduce the above copyright notice,
**      this list of conditions and the following disclaimer in the documentation
**      and/or other materials provided with the distribution.
**   3. Neither the name of Puya Semiconductor nor the names of its contributors
**      may be used to endorse or promote products derived from this software
**      without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
*****************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);    /* end of RAM */
/* Generate a link error if heap and stack don't fit into RAM */
/* ENABLE_NO_HEAP passes --defsym=__min_heap_size=0 to drop the reserved heap
 * (no malloc/sbrk is linked); default stays 0x200 otherwise. */
_Min_Heap_Size = DEFINED(__min_heap_size) ? __min_heap_size : 0x200;      /* required amount of heap  */
_Min_Stack_Size = 0x400; /* required amount of stack */

/* Specify the memory areas */
MEMORY
{
RAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 16K
FLASH (rx)      : ORIGIN = 0x08002800, LENGTH = 118K
}

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into FLASH */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data goes into FLASH */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  /* Constant data goes into FLASH */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array     :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */

    *(.RamFunc)        /* functions that must execute from RAM (e.g. internal */
    *(.RamFunc*)       /* flash reprogramming); copied to RAM with .data      */

    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } >RAM AT> FLASH

  /* Explicit placement of .noncacheable (CherryUSB) + end-of-FLASH symbol */
  .noncacheable :
  {
    . = ALIGN(4);
    *(.noncacheable)
    *(.noncacheable*)
    . = ALIGN(4);
  } >RAM AT> FLASH

  /* Optional multiboot overlay. The restore stub is linked at the same RAM
     address as the 4 KiB PY25Q16 sector cache, but its load image remains in
     FLASH and is copied into the cache only immediately before reflashing. */
  ASSERT(. <= ORIGIN(RAM)+0x280, "Resident data overlaps H4 overlay VMA")
  OVERLAY (ORIGIN(RAM)+0x280) : NOCROSSREFS AT (LOADADDR(.noncacheable) + SIZEOF(.noncacheable))
  {
    .mb_ramfunc
    {
      . = ALIGN(4);
      __mb_ramfunc_start = .;
      KEEP(*(.MBRamFunc))
      KEEP(*(.MBRamFunc*))
      . = ALIGN(4);
      __mb_ramfunc_end = .;
    }

    .mb_workspace
    {
      . = ALIGN(4);
      __mb_workspace_start = .;
      KEEP(*(.bss.mb_workspace))
      KEEP(*(.bss.mb_workspace*))
      . = ALIGN(4);
      __mb_workspace_end = .;
    }
  } >RAM

  __mb_ramfunc_load_start = LOADADDR(.mb_ramfunc);
  __mb_ramfunc_size = SIZEOF(.mb_ramfunc);
  __mb_workspace_size = SIZEOF(.mb_workspace);

  ASSERT(__mb_ramfunc_size <= __mb_workspace_size,
         "Multiboot RAM stub does not fit in PY25Q16 sector-cache overlay")
  ASSERT((__mb_ramfunc_size == 0) || (__mb_workspace_size == 0x1000),
         "Multiboot overlay workspace is not the 4 KiB sector cache")
  ASSERT((__mb_ramfunc_size == 0) ||
         (ADDR(.mb_ramfunc) == ADDR(.mb_workspace)),
         "Multiboot RAM stub and workspace do not share the same VMA")
  ASSERT((__mb_ramfunc_size == 0) ||
         (LOADADDR(.mb_ramfunc) != ADDR(.mb_ramfunc)),
         "Multiboot RAM stub load and execution addresses must differ")
  ASSERT((LOADADDR(.mb_ramfunc) + SIZEOF(.mb_ramfunc)) <=
         (ORIGIN(FLASH) + LENGTH(FLASH)),
         "FLASH overflowed by multiboot RAM-stub load image")

  /* .mb_ramfunc is the final section with a FLASH load image. The workspace
     member is NOLOAD/BSS storage and therefore contributes RAM only. */
  _eflash_used = LOADADDR(.mb_ramfunc) + SIZEOF(.mb_ramfunc);
  _flash_used_percent100 = ((_eflash_used - ORIGIN(FLASH)) * 10000 + LENGTH(FLASH) / 2) / LENGTH(FLASH);

  
  OVERLAY (ORIGIN(RAM)+0x280) : NOCROSSREFS AT (0x90000000)
  {
    .native_menu { KEEP(*(.native_menu)) KEEP(*(.native_menu.const)) }
    .native_editor { KEEP(*(.native_editor)) KEEP(*(.native_editor.const)) }
    .native_rf { KEEP(*(.native_rf)) KEEP(*(.native_rf.const)) }
    .native_fm { KEEP(*(.native_fm)) KEEP(*(.native_fm.const)) }
    .native_vfo { KEEP(*(.native_vfo)) KEEP(*(.native_vfo.const)) }
    .native_scan { KEEP(*(.native_scan)) KEEP(*(.native_scan.const)) }
    .native_plot { KEEP(*(.native_plot)) KEEP(*(.native_plot.const)) }
    .native_lists { KEEP(*(.native_lists)) KEEP(*(.native_lists.const)) }
    .native_options { KEEP(*(.native_options)) KEEP(*(.native_options.const)) }
    .native_boot { KEEP(*(.native_boot)) KEEP(*(.native_boot.const)) }
    .native_menukeys { KEEP(*(.native_menukeys)) KEEP(*(.native_menukeys.const)) }
    .native_menuset { KEEP(*(.native_menuset)) KEEP(*(.native_menuset.const)) }
    .native_editkeys { KEEP(*(.native_editkeys)) KEEP(*(.native_editkeys.const)) }
    .native_messages { KEEP(*(.native_messages)) KEEP(*(.native_messages.const)) }
  } >RAM
  ASSERT(SIZEOF(.native_menu)<=0x1000,"menu module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_editor)<=0x1000,"editor module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_rf)<=0x1000,"rf module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_fm)<=0x1000,"fm module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_vfo)<=0x1000,"vfo module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_scan)<=0x1000,"scan module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_plot)<=0x1000,"plot module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_lists)<=0x1000,"lists module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_options)<=0x1000,"options module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_boot)<=0x1000,"boot module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_menukeys)<=0x1000,"menukeys module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_menuset)<=0x1000,"menuset module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_editkeys)<=0x1000,"editkeys module exceeds 4 KiB")
  ASSERT(SIZEOF(.native_messages)<=0x1000,"messages module exceeds 4 KiB")
  .fp_resources 0x91000000 : AT(0x91000000)
  { __fp_resources = .; KEEP(*(.fp_resources)) __fp_resources_end = .; }
  . = ORIGIN(RAM)+0x1280;

  /* Uninitialized data section */
  . = ALIGN(4);
  .bss (ORIGIN(RAM)+0x1280) (NOLOAD) :
  {
    /* This is used by the startup in order to initialize the .bss secion */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    /* Modal apps and scanning are mutually exclusive. These disposable scan
       tables are rebuilt at every scan entry; allow the app stack to borrow
       them, preserving History, channel attributes, VFOs and USB/UART state. */
    __app_stack_floor = .;
    KEEP(*(.app_scratch))
    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >RAM

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
