# STM32H750 AXI SRAM Demo Application

This is the Phase 4 RAM-load demo application for the bootloader YMODEM path.

It is linked to run from AXI SRAM at `0x24000000`. The demo initializes the
copied CubeMX UART/DMA setup, starts `uart_stdio_async`, prints the current
VTOR, toggles the LED, and emits one heartbeat per second.

Build:

```sh
cmake --preset Release
cmake --build --preset Release
```

Outputs:

```text
build/Release/STM32H750NetLiteApp.elf
build/Release/STM32H750NetLiteApp.bin
build/Release/STM32H750NetLiteApp.signed.bin
```

The CMake `sign` target wraps the raw binary with an MCUboot header using:

```sh
python3 ../mcuboot/scripts/imgtool.py sign \
  --key ../keys/root.pem \
  --version 1.0.0+0 \
  --header-size 0x200 \
  --slot-size 0x80000 \
  --align 1 \
  --pad-header \
  --load-addr 0x24000000 \
  build/Release/STM32H750NetLiteApp.bin \
  build/Release/STM32H750NetLiteApp.signed.bin
```

Do not add `--pad` for the AXI SRAM transfer. Padding to a 1 MB SPI NOR slot
would exceed the 512 KB AXI SRAM staging area. The SPI NOR slots are 1 MB
storage slots, but the executable RAM-load image must fit in the 512 KB AXI
SRAM runtime window; the current `--slot-size 0x80000` setting keeps imgtool's
size check aligned with that runtime limit. Phase 6B still keeps the image
unencrypted; the bootloader has a no-op stream filter hook reserved for future
decrypt/decompress work and rejects unexpected MCUboot encrypted-image flags.

The app includes backup-SRAM `BootExchange` wrappers for Phase 6B confirm/test
requests. Automatic confirm is disabled by default; define
`APP_BOOT_AUTO_CONFIRM_AFTER_MS` to queue a confirm request after a demo
self-test delay.

VS Code / OpenOCD:

Open this `App/` directory as the VS Code workspace when debugging the AXI SRAM
demo directly. The App-local `.vscode` tasks build the RAM-linked ELF and the
App-local OpenOCD wrapper reuses the bootloader board/probe scripts.

- `CMake: Build Debug` builds `build/Debug/STM32H750NetLiteApp.elf`.
- `CMake: Build & Sign Release` builds and signs
  `build/Release/STM32H750NetLiteApp.signed.bin`.
- `Debug: AXI SRAM App` loads the Debug ELF into AXI SRAM through OpenOCD, sets
  SP/PC from the vector table at `0x24000000`, breaks at `main`, then runs.
