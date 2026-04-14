set pagination off
set print pretty on

set architecture armv7e-m
set endian little
target extended-remote :3333
monitor reset halt

define hf
  echo \n=== registers ===\n
  info registers

  echo \n=== backtrace ===\n
  bt

  echo \n=== fault registers ===\n
  printf "VTOR = 0x%08x\n", *(unsigned int*)0xE000ED08
  printf "CFSR = 0x%08x\n", *(unsigned int*)0xE000ED28
  printf "HFSR = 0x%08x\n", *(unsigned int*)0xE000ED2C
  printf "DFSR = 0x%08x\n", *(unsigned int*)0xE000ED30
  printf "MMFAR = 0x%08x\n", *(unsigned int*)0xE000ED34
  printf "BFAR = 0x%08x\n", *(unsigned int*)0xE000ED38

  echo \n=== pc disassembly ===\n
  x/8i $pc

  echo \n=== lr disassembly, if valid ===\n
  x/8i $lr
end

document hf
  Dump registers, backtrace, ARM fault status registers, and PC/LR disassembly.
end

define ramvec
  set $app_sp = *(unsigned int*)0x24000000
  set $app_pc = *(unsigned int*)0x24000004

  echo \n=== AXI SRAM app vector ===\n
  printf "APP_VTOR = 0x24000000\n"
  printf "APP_SP   = 0x%08x\n", $app_sp
  printf "APP_PC   = 0x%08x\n", $app_pc

  echo \n=== vector words ===\n
  x/8wx 0x24000000

  echo \n=== app reset handler disassembly ===\n
  x/8i ($app_pc & ~1)
end

document ramvec
  Dump the RAM-load app vector table at 0x24000000 and disassemble its reset handler.
end

define wait_ram_enter
  tbreak boot_ram_image_enter
  continue

  echo \n=== RAM image final entry ===\n
  printf "entry SP = 0x%08x\n", $r0
  printf "entry PC = 0x%08x\n", $r1
  printf "VTOR     = 0x%08x\n", *(unsigned int*)0xE000ED08
  ramvec
end

document wait_ram_enter
  Continue until the bootloader reaches the final RAM app entry helper, then dump entry registers and vector data.
end

define wait_hf
  break HardFault_Handler
  continue
  hf
end

document wait_hf
  Break at HardFault_Handler, continue, then dump the hardfault context.
end
