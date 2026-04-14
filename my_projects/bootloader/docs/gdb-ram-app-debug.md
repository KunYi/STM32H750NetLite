# GDB RAM App Jump Debug

This note describes how to use `debug/hardfault.gdb` to inspect the Phase 5
AXI SRAM RAM-load application jump.

## Prerequisites

- Build a debug-capable bootloader image:

```sh
cmake --preset Debug
cmake --build --preset Debug
```

- Run OpenOCD or another GDB server on port `3333`.
- Flash the bootloader before starting the transfer test.
- Keep the UART/YMODEM terminal ready for sending the signed RAM-load app.

For verbose bootloader checkpoints, enable the RAM image diagnostic log:

```sh
cmake --preset Debug -DBOOT_RAM_IMAGE_DIAGNOSTIC_LOG=ON
cmake --build --preset Debug
```

Disable it again after debugging:

```sh
cmake --preset Debug -DBOOT_RAM_IMAGE_DIAGNOSTIC_LOG=OFF
cmake --build --preset Debug
```

## Load The Script

Start GDB with the bootloader ELF and source the helper script:

```sh
arm-none-eabi-gdb build/Debug/STM32H750NetLiteBoot.elf -x debug/hardfault.gdb
```

The script configures:

- ARMv7E-M architecture
- little-endian target mode
- `target extended-remote :3333`
- `monitor reset halt`
- helper commands for HardFault and RAM app vector inspection

After loading, the target is halted near reset. Use `continue` to let the
bootloader run, then send the signed image by YMODEM from the UART terminal.

## RAM Jump Check

To stop at the final bootloader-to-app entry point:

```gdb
wait_ram_enter
```

This sets a temporary breakpoint on `boot_ram_image_enter`, continues execution,
and prints:

- `entry SP`: the stack pointer passed in `r0`
- `entry PC`: the reset handler passed in `r1`
- `VTOR`: the current vector table register
- the first words at `0x24000000`
- disassembly of the app reset handler

Expected values for the current AXI SRAM demo app look like:

```text
entry SP = 0x20020000
entry PC = 0x240005BD
VTOR     = 0x24000000
```

At this point the bootloader has already validated the image, relocated the
payload over the MCUboot header, checked the vector table, handled cache policy,
disabled/cleared bootloader interrupts, set `VTOR`, and is about to branch to
the app reset handler.

You can also inspect the RAM app vector table at any halt point after relocation:

```gdb
ramvec
```

Use this when diagnostic UART logging is enabled and UART shows:

```text
RAM image vector check OK
```

or when stopped at `boot_ram_image_jump` / `boot_ram_image_enter`.

## HardFault Check

To continue until a HardFault and dump the fault context:

```gdb
wait_hf
```

If the target is already stuck in `HardFault_Handler`, interrupt it with
`Ctrl-C`, then run:

```gdb
hf
```

The `hf` command prints:

- CPU registers
- backtrace
- `VTOR`
- `CFSR`
- `HFSR`
- `DFSR`
- `MMFAR`
- `BFAR`
- disassembly at `PC`
- disassembly at `LR`, when `LR` is a real code address

For exception returns such as `LR=0xffffffe9`, `x/8i $lr` cannot disassemble
memory. That is expected.

## Interpreting The Cache Fault

The confirmed Phase 5 cache fault looked like this:

```text
SCB_CleanDCache_by_Addr(...)
CFSR = 0x00000400
HFSR = 0x40000000
VTOR = 0x08000000
```

`CFSR=0x400` is an imprecise BusFault. In this project it happened because the
bootloader called CMSIS D-cache maintenance APIs while D-cache was disabled.
The current handoff code checks `SCB->CCR & SCB_CCR_DC_Msk` before calling
`SCB_CleanDCache_by_Addr()`.

When D-cache is disabled, the diagnostic log should say:

```text
RAM image D-cache clean skipped: D-cache disabled
```

The expected successful jump then continues with:

```text
RAM image final jump: VTOR=0x24000000 SP=0x20020000 PC=0x240005BD
AXI SRAM UART demo app start
VTOR=0x24000000
AXI SRAM app alive: tick=...
```

## Typical Session

Start GDB from the shell:

```sh
arm-none-eabi-gdb build/Debug/STM32H750NetLiteBoot.elf -x debug/hardfault.gdb
```

Then let the target run:

```gdb
continue
```

Send the signed app by YMODEM. If you want to stop at the exact final entry
point instead of running through it:

```gdb
monitor reset halt
wait_ram_enter
```

`wait_ram_enter` leaves the target running until the final handoff breakpoint is
hit. Send the signed app from the UART/YMODEM terminal after the bootloader
enters the recovery path.

If the target faults:

```gdb
hf
ramvec
```

If the app starts but does not log, stop the target and inspect:

```gdb
info registers
p/x *(unsigned int*)0xE000ED08
ramvec
bt
```
