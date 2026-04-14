# MCUboot V1 Bootloader Plan

## Current Decision

Use MCUboot bootutil with RAM-load mode and external SPI NOR A/B slots.

Initial scope:

- Bootloader stays in internal flash at `0x08000000`.
- Application images live in BY25Q32 SPI NOR slots.
- MCUboot chooses the image, copies it to AXI SRAM at `0x24000000`, verifies it,
  then the bootloader jumps to the RAM image.
- OTA and recovery paths write MCUboot signed images only.
- Signature policy is EC256/P-256 ECDSA with SHA-256. Ed25519 is deferred
  because it did not fit the current bootloader flash budget.
- Use MCUboot trailer state for pending, confirm, and revert.
- Do not implement the V2 segmented loader until the application load image gets
  close to the AXI SRAM 512 KB limit.

## Size Baseline

Current picolibc toolchain result, Release:

```text
text    data     bss     dec
25628     36   10808   36472
```

Comparable newlib-nano baseline, Release:

```text
text    data     bss     dec
29032    120   10840   39992
```

Current Debug comparison:

```text
newlib-nano: text=53412 data=120 bss=10840 dec=64372
picolibc:    text=49848 data=36  bss=10808 dec=60692
```

Observed Release size attribution:

```text
HAL/startup/init path: about 19.8 KB
UART stdio async:     about 0.9 KB
main/CubeMX glue:     about 0.7 KB
compiler/libc small:  below 1 KB visible symbols
```

Current conclusion:

- picolibc is smaller than the newlib-nano baseline in this project.
- If size becomes tight later, optimize HAL/CubeMX init first, not libc.
- 64 KB ITCM concern is for code/loadable content; DTCM and D2 RAM buffers are
  not the limiting factor for that target.

## Memory Targets

Bootloader flash budget:

- Internal flash capacity: 128 KB.
- Practical short-term target: keep Release loadable content below 64 KB while
  MCUboot V1 is being integrated.

Runtime RAM:

- DTCM 128 KB is available for stack, `.data`, `.bss`, and work buffers.
- DMA buffers should remain in D2 RAM where appropriate.
- AXI SRAM at `0x24000000` is reserved for the RAM-load application image before
  jumping.

Application V1 limit:

- The signed RAM-load application payload must fit in AXI SRAM 512 KB.
- `imgtool --slot-size` only checks SPI NOR slot capacity; the bootloader still
  needs a runtime range check against AXI SRAM.

## Implementation Phases

### Phase 0: Keep The Current Toolchain Stable

- Use `--specs=picolibc.specs` in compile flags.
- Do not manually define `__PICOLIBC__`.
- Do not self-append CMake flags that include `picolibc.specs`; CMake may load
  the toolchain file more than once.
- Keep `sysmem.c` compatible with picolibc `__strong_reference`.

Done:

- GCC ARM toolchain switched to picolibc.
- Clean build verified with picolibc map entries.

### Phase 1: Local Shared Definitions

Create local shared headers for bootloader and application use:

- `boot_flash_layout.h`
- `boot_exchange.h`
- `boot_update_result.h`

Rules:

- MCUboot path does not use the old custom `AppHeader_t`.
- MCUboot path does not use the old custom `FlashMetadata_t`.
- Backup SRAM exchange block remains a handoff/status hint, not a trust root.

Initial constants:

```text
BY25Q32 total:       4 MB
slot A:              0x000000 .. 0x0FFFFF
slot B:              0x100000 .. 0x1FFFFF
MCUboot trailer:     0x200000 .. 0x20FFFF
userdata TLV:        0x210000 .. 0x21FFFF
reserved:            0x220000 .. 0x3FFFFF
slot size:           0x100000
RAM load address:    0x24000000
RAM load size:       0x00080000
BootExchange addr:   0x38800000
BootExchange size:   32 bytes
```

Notes:

- The userdata TLV area is project metadata, not the MCUboot signed-image TLV.
- MCUboot 2.x normally derives trailer state from the slot layout. Keep the
  separate trailer area as a project layout decision for now, but validate it
  against the selected MCUboot RAM-load and revert mode before wiring the
  flash map glue.

### Phase 2: BY25Q32 Driver Bring-Up

Status: completed.

Implemented and tested the SPI NOR driver before bringing in MCUboot:

- JEDEC ID read.
- Read and Fast Read.
- 256-byte Page Program handling page boundaries.
- 4 KB Sector Erase.
- Optional 64 KB Block Erase.
- Bring-up destructive write/erase/read verify against reserved sector
  `0x3FF000`.
- Async UART stdio for bring-up logging.
- Verify code split out of `main.c` into `source/verify/` modules.

Current test control:

- `BOOT_FLASH_SELF_TEST=ON` compiles and runs boot verify modules.
- `BOOT_FLASH_SELF_TEST=OFF` does not compile `source/verify/` sources.
- `BOOT_FLASH_DESTRUCTIVE_TEST=ON` enables the reserved-sector destructive
  erase/program/read verify.

Notes:

- Keep software GPIO CS for BY25Q32ES for now. Hardware NSS can be revisited
  later, but SPI NOR command/address/data phases currently need explicit CS
  control across multiple HAL calls.
- Do not introduce `HAL_SPI_TransmitReceive()` unless it clearly improves code
  size or transaction behavior; the current transmit/receive model is working
  on hardware and is easier to review.

### Phase 3: MCUboot Minimal Port

Status: implementation complete; hardware validation pending.

MCUboot is vendored into the repository as a fixed source dependency for the
current bring-up. Treat the repository commit as the source pin for now.

Implemented bring-up tasks:

MCUboot flash area glue:

- `flash_area_open`
- `flash_area_close`
- `flash_area_read`
- `flash_area_write`
- `flash_area_erase`
- `flash_area_align`
- `flash_area_erased_val`
- `flash_area_get_sectors`
- image slot id mapping helpers

Normal boot path should initialize only the minimum needed hardware:

- HSI 64 MHz.
- GPIO needed for SPI and boot key.
- SPI1.
- Backup SRAM access.
- MCUboot platform glue.

Do not initialize these on normal boot path:

- USART recovery.
- Ethernet.
- Full application clocks.

Bring in:

- `boot/bootutil` sources.
- `mcuboot_config.h`.
- public key source.
- EC256 signature verification, meaning ECDSA P-256 with SHA-256.
- TinyCrypt backend for the EC256 path, avoiding Mbed TLS crypto in the first
  port.
- Keep the minimal `mbedtls-asn1` parser path for ECDSA P-256 public key DER
  `SubjectPublicKeyInfo` validation during bring-up. If flash space becomes
  tight, consider switching to MCUboot's ASN.1 bypass path and directly using
  the raw P-256 public key from a controlled key-generation flow.
- logging/assert/platform glue.

First validation goal:

- With empty slots, `boot_go()` should fail cleanly and route to the
  recovery/update handler.
- No application jump yet.

Current implementation status:

- `boot_go()` validation-only path is wired through `BootMcuboot_RunValidationOnly()`.
- Success logs the selected-image case but does not jump yet.
- Failure routes through `BootUpdate_RunRecovery()`.
- The recovery/update handler is intentionally YMODEM-only for the 64 KB target;
  the transport is still disabled until the Phase 4 implementation.
- Debug and Release builds pass with the EC256 key generated from `keys/root.pem`.
- Hardware validation with erased/empty external slots is still required before
  closing Phase 3.

Crypto notes:

- Use `imgtool` EC256/P-256 keys and signed images for the Phase 3 port.
- `keys/root.pem` is a repository bring-up/test key. It may be force-added so
  developers can reproduce signed-image tests, but product or deployment users
  must replace it with their own private root key and regenerate
  `source/boot_keys.c` from that key before shipping.
- Pin the MCUboot commit before wiring the crypto files, then verify that the
  selected commit contains the TinyCrypt ECDSA P-256 verification path.
- Keep RSA disabled. Ed25519 is deferred because the code size did not fit the
  current bootloader flash budget.
- ASN.1 is only used to parse and validate the public key container format
  before extracting the P-256 public key.

### Phase 4: YMODEM Download To AXI SRAM

Add the bootloader download path before SPI NOR slot/update validation. This
phase proves the UART transfer and RAM application execution path with the
fewest moving parts.

Rules:

- UART YMODEM is the only bootloader recovery transport for the 64 KB target.
- Payload is always an MCUboot signed image.
- No raw application binary update path.
- YMODEM writes the received signed image into an AXI SRAM staging area for this
  phase.
- The AXI SRAM path is a bring-up path for download and jump validation. It must
  not become the final persistent update path.
- Update Service can initialize USART for YMODEM; normal boot path must not
  initialize USART recovery.
- Do not write SPI NOR slots or MCUboot trailer state in this phase.

Initial validation:

- With empty slots, `boot_go()` fails cleanly and enters `BootUpdate_RunRecovery()`.
- YMODEM receives a signed image and writes it to AXI SRAM.
- Failed or canceled YMODEM transfer leaves no accepted RAM image.
- Bootloader checks the downloaded image header and RAM-load address range
  before any jump attempt.

### Phase 5: Signed RAM-load Application From AXI SRAM

Create the smallest RAM-load application first.

Application constraints:

- Vector table starts at `0x24000000`.
- Reset handler is in AXI SRAM image.
- Startup sets `SCB->VTOR = 0x24000000` early.
- Optional ITCM/DTCM sections can be copied by application startup later.

Signing command shape:

Use the repository test key only for bring-up. Replace `keys/root.pem` with the
deployment root key and rebuild the bootloader public key source before shipping.

```sh
python3 external/mcuboot/scripts/imgtool.py sign \
  --key keys/root.pem \
  --version 1.0.0+0 \
  --header-size 0x200 \
  --slot-size 0x100000 \
  --align 1 \
  --pad-header \
  --pad \
  --load-addr 0x24000000 \
  build/application.bin \
  build/application.signed.bin
```

Validation:

- Use Phase 4 YMODEM recovery to write a signed image into AXI SRAM.
- Bootloader validates the signed image before jump. If full MCUboot validation
  cannot be reused directly from RAM, keep this path debug-only until the SPI
  NOR validation path in Phase 6 is active.
- Bootloader checks RAM-load address and end address.
- Bootloader jumps to AXI SRAM application.
- Application proves the reset handler, vector table, and minimum runtime path.

### Phase 6: SPI NOR Slot Update, Confirm, And Revert

Move the YMODEM write target from AXI SRAM staging to the MCUboot SPI NOR slot
layout, then validate the remaining persistent update behavior.

Application should expose stable wrappers:

```c
int Image_RequestTestUpgrade(void);
int Image_ConfirmCurrent(void);
```

Rules:

- YMODEM writes signed images to the inactive SPI NOR slot.
- MCUboot remains responsible for signature validation and slot selection.
- Application confirms only after minimum self-test passes.
- Do not hand-write MCUboot trailer layout across the codebase.
- If a minimal trailer writer is unavoidable, pin the MCUboot commit and add
  regression tests for the trailer layout.

Validation:

- Use YMODEM recovery to write new signed images into the inactive slot.
- `boot_go()` selects the signed image from SPI NOR.
- New image without confirm reverts on next boot.
- New image with confirm remains selected across reboot.
- Corrupted signature is rejected.
- Empty/corrupt both slots enters update/recovery mode.

### Phase 7: Size Optimization Only If Needed

Do not optimize HAL prematurely.

Trigger points:

- Release loadable content approaches 64 KB.
- MCUboot + crypto pushes the internal flash budget close to 128 KB.
- Normal boot latency is too high because SPI read/copy is too slow.

Optimization order:

1. Disable unused MCUboot features.
2. Keep recovery features behind build-time options.
3. Remove unused CubeMX init from the normal boot path.
4. Replace normal boot RCC/GPIO/SPI setup with LL or direct register writes.
5. Replace HAL UART/DMA/SPI paths only after the behavior is stable.

Direct register write policy:

- Only freeze register values after the HAL/CubeMX version is proven on hardware.
- Keep a comment or document reference for clock tree and pin assumptions.
- Preserve a low-speed SPI fallback if adding a high-speed PLL2P path.

## V2 Segmented Loader Deferral

Do not implement V2 now.

V2 becomes relevant only if the initialized application image no longer fits in
the AXI SRAM 512 KB RAM-load model.

If V2 becomes necessary:

- Manifest must be inside the signed MCUboot payload.
- Use ELF as the source of truth for segment generation.
- Add strict range, overlap, and alignment checks in the bootloader.
- Do not use stock `MCUBOOT_RAM_LOAD` copy path for the segmented payload.

## Open Decisions

- Exact MCUboot commit/version.
- Crypto backend: EC256/P-256 ECDSA with SHA-256 and TinyCrypt first. Ed25519
  is deferred because measured code size was too large for the current
  bootloader flash budget.
- Public key import mode: keep ASN.1 validation for now; allow
  `MCUBOOT_KEY_IMPORT_BYPASS_ASN` later if measured flash usage requires
  removing the parser.
- Whether normal boot should use HSI SPI only or add optional PLL2P 60 MHz SPI.
- Application RTOS timing: keep first MCUboot validation bare-metal/HAL, then
  integrate the target RTOS.

## Verification Checklist

- Clean Debug and Release build.
- Map confirms picolibc, not newlib-nano.
- Empty SPI slots enter update/recovery path.
- Signed Slot A boots.
- Signed Slot B boots when selected.
- Unconfirmed image reverts.
- Confirmed image stays selected.
- Corrupted signature is rejected.
- Power loss while writing inactive slot leaves previous confirmed image bootable.
- Normal boot path does not initialize USART recovery.
- RAM-load address and end address are checked before jump.
