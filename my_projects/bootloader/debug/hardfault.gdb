set pagination off
set print pretty on

set architecture armv7e-m
set endian little
target extended-remote :3333
monitor reset halt

define hf
  echo \n================ HARDFAULT ANALYSIS ================\n
  echo \n=== core registers ===\n
  info registers

  echo \n=== backtrace ===\n
  bt

  echo \n=== fault registers ===\n
  printf "VTOR  = 0x%08x\n", *(unsigned int*)0xE000ED08
  printf "CFSR  = 0x%08x\n", *(unsigned int*)0xE000ED28
  printf "HFSR  = 0x%08x\n", *(unsigned int*)0xE000ED2C
  printf "DFSR  = 0x%08x\n", *(unsigned int*)0xE000ED30
  printf "MMFAR = 0x%08x\n", *(unsigned int*)0xE000ED34
  printf "BFAR  = 0x%08x\n", *(unsigned int*)0xE000ED38

  echo \n=== exception stack frame ===\n
  x/8wx hardfault_args

  echo \n=== stacked registers ===\n
  printf "R0   = 0x%08x\n", ((unsigned int*)hardfault_args)[0]
  printf "R1   = 0x%08x\n", ((unsigned int*)hardfault_args)[1]
  printf "R2   = 0x%08x\n", ((unsigned int*)hardfault_args)[2]
  printf "R3   = 0x%08x\n", ((unsigned int*)hardfault_args)[3]
  printf "R12  = 0x%08x\n", ((unsigned int*)hardfault_args)[4]
  printf "LR   = 0x%08x\n", ((unsigned int*)hardfault_args)[5]
  printf "PC   = 0x%08x\n", ((unsigned int*)hardfault_args)[6]
  printf "xPSR = 0x%08x\n", ((unsigned int*)hardfault_args)[7]

  echo \n=== stacked PC disassembly, if valid ===\n
  set $stack_pc = ((unsigned int*)hardfault_args)[6]
  if (($stack_pc >= 0x08000000 && $stack_pc < 0x08200000) || \
      ($stack_pc >= 0x00000000 && $stack_pc < 0x00010000) || \
      ($stack_pc >= 0x24000000 && $stack_pc < 0x24080000) || \
      ($stack_pc >= 0x30000000 && $stack_pc < 0x30040000))
    x/8i ($stack_pc & ~1)
  else
    echo stacked PC not in a known executable region\n
  end

  echo \n=== stacked LR disassembly, if valid ===\n
  set $stack_lr = ((unsigned int*)hardfault_args)[5]
  if (($stack_lr & 0xFF000000) == 0xFF000000)
    echo stacked LR looks like EXC_RETURN\n
  else
    if (($stack_lr >= 0x08000000 && $stack_lr < 0x08200000) || \
        ($stack_lr >= 0x00000000 && $stack_lr < 0x00010000) || \
        ($stack_lr >= 0x24000000 && $stack_lr < 0x24080000) || \
        ($stack_lr >= 0x30000000 && $stack_lr < 0x30040000))
      x/8i ($stack_lr & ~1)
    else
      echo stacked LR not in a known executable region\n
    end
  end

  echo \n=== EXC_RETURN decode ===\n
  printf "EXC_RETURN = 0x%08x\n", $lr

  if (($lr & 4) == 0)
    echo Stack used: MSP\n
  else
    echo Stack used: PSP\n
  end

  if (($lr & 0x10) == 0)
    echo Extended FP stack frame present\n
  else
    echo Basic stack frame (no FP context)\n
  end

  echo \n=== handler PC (current) ===\n
  x/6i $pc

  echo \n====================================================\n
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
  break hard_fault_handler_c
  continue
  hf
end

document wait_hf
  Break at HardFault_Handler, continue, then dump the hardfault context.
end
