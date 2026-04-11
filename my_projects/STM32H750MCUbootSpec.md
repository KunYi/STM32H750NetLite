# STM32H750NetLite -- MCUboot Based Bootloader 與 OTA 實作規格書

**硬體平台：** STM32H750VBT6 + BY25Q32 SPI NOR Flash（4 MB）
**基準文件：** `STM32H750BootSpec.md` v0.3
**文件版本：** v0.1
**日期：** 2026-04-11
**狀態：** 規劃中
**共用狀態規格：** `STM32H750BootSharedStateSpec.md`
**低功耗模式規格：** `STM32H750LowPowerModeSpec.md`

---

## 1. 設計結論

本版規格採用 **MCUboot bootutil + RAM-load mode + external SPI NOR slots**：

- Bootloader 仍駐留 STM32H750 內部 Flash `0x0800_0000`，不透過 OTA 更新。
- Application image 存放於 BY25Q32 SPI NOR Flash 的兩個同級 slot。
- MCUboot 檢查 image header、版本與 trailer 狀態後，將候選 image 複製到 AXI SRAM `0x2400_0000`，再驗證 SHA hash / signature，最後跳轉執行。
- Application linker script 固定以 AXI SRAM 為執行位址。
- OTA 與 YMODEM 只負責把 **MCUboot signed image** 寫入 inactive slot，不再自訂 `AppHeader_t` 或 `FlashMetadata_t`。
- 回滾改用 MCUboot RAM-load revert/confirm 機制，不再使用自訂 `boot_count`。

本規格不要求導入 Zephyr。實作方向是裸機 STM32Cube/HAL boot app，port MCUboot 的 `boot/bootutil`，並提供必要的 `flash_area_*`、crypto、logging、jump glue。

Bootloader/Application 交換資訊、Update Service 入口、T-Flash/YMODEM fallback 與共用 flash layout 命名，參考 `STM32H750BootSharedStateSpec.md`。其中自製 `FlashMetadata_t` / `AppHeader_t` / `WRITING` 狀態不直接用於 MCUboot 版；MCUboot 版以 image trailer、test/confirm/revert 狀態對應。

---

## 2. 與原 v0.3 規格的差異

| 項目 | 原 v0.3 | MCUboot 版 |
|------|---------|------------|
| Image header | 自訂 `AppHeader_t` | MCUboot `struct image_header` |
| Image 驗證 | CRC32 | SHA + 簽章驗證 |
| Metadata | 自訂 4 KB 主/備份 sector | MCUboot image trailer |
| Active slot | `active_slot` metadata | MCUboot 依版本與 trailer 狀態選擇 |
| 回滾 | `boot_count > BOOT_TRIAL_MAX` | V1 使用 `MCUBOOT_RAM_LOAD_REVERT` + app confirm；V2 需沿用 MCUboot trailer 狀態但自訂分段載入 |
| 打包工具 | `mkfirmware.py` | MCUboot `imgtool.py sign` |
| OTA payload | 自訂 header + binary | signed + padded MCUboot image |
| Bootloader 硬體初始化 | 自訂 | 保留自訂 HAL 初始化 |
| 共用狀態 | `STM32H750BootSharedStateSpec.md` 的自製 metadata v2 | `STM32H750BootSharedStateSpec.md` 的 MCUboot 對應規則 |

取消：

- `FlashMetadata_t`
- `AppHeader_t`
- `OTA_REQUEST_FLAG`
- `Boot_NotifySuccess()` 的自訂 metadata 寫法
- 自訂 CRC32 作為安全驗證依據

新增：

- `mcuboot_config/mcuboot_config.h`
- BY25Q32 `flash_area` port
- MCUboot public key
- V1：`boot_go()` 呼叫與 `struct boot_rsp` 跳轉 glue
- V2：MCUboot image selection / trailer / signature 驗證 glue 與本專案 segmented loader
- application 端 `boot_set_confirmed()` 或等效 confirm API

---

## 3. 系統啟動流程

```
上電
 │
 ▼
Bootloader Minimal Init
（HSI 64MHz、GPIO、SPI1、CRC/crypto backend、Backup SRAM）
 │
 ├── KEY2（PC13）長按 3 秒
 │      或 Backup SRAM recovery flag ──► Update Service
 │                                      │
 │                                      ├── 嘗試 T-Flash 指定檔案
 │                                      │     → 載入 signed image → 寫入 inactive slot
 │                                      │     → 設定 test trailer → reboot
 │                                      │
 │                                      └── 找不到 T-Flash/指定檔案/檔案無效
 │                                            → UART YMODEM Recovery
 │                                            → 接收 signed image → 寫入 inactive slot
 │                                            → 設定 test trailer → reboot
 │
 └── 正常開機
        │
        ▼
   初始化 MCUboot platform layer
   - flash_map
   - BY25Q32 flash_area API
   - crypto backend
   - log / assert hooks
        │
        ▼
   boot_go(&rsp)
        │
        ├── 掃描 Slot A / Slot B 的 MCUboot image header
        ├── 依 image version 與 trailer 狀態選擇 candidate
        ├── 若兩個 slot 都無效 → Update Service
        ├── 若為未確認的新 image，標記 selected/copy_done 類狀態
        ├── 將 image payload 複製至 AXI SRAM 0x2400_0000
        ├── 在 RAM 中驗證 SHA + signature
        └── 回傳 boot_rsp
        │
        ▼
   Boot_JumpFromRsp(&rsp)
   - 關中斷
   - HAL_RCC_DeInit()
   - HAL_DeInit()
   - SysTick disable
   - SCB->VTOR = 0x24000000
   - MSP = vector[0]
   - branch vector[1]
```

Application 啟動成功後必須 confirm image：

```
HAL_Init()
SystemClock_Config()
MX_GPIO_Init()
MX_ETH_Init()
...
App_SelfTest()
boot_set_confirmed()
進入主程式
```

若新 image 沒有 confirm，下次重啟時 MCUboot RAM-load revert 會排除該 image，改選上一個有效 image。

---

## 4. 記憶體配置

### 4.1 MCU 內部記憶體

| 區域 | 位址範圍 | 大小 | 用途 |
|------|----------|------|------|
| 內部 Flash | `0x0800_0000` ~ `0x0801_FFFF` | 128 KB | MCUboot-based bootloader |
| ITCMRAM | `0x0000_0000` ~ `0x0000_FFFF` | 64 KB | Application 可選：確定性/低延遲 code |
| DTCMRAM | `0x2000_0000` ~ `0x2001_FFFF` | 128 KB | Bootloader runtime 工作 RAM；跳轉後交還 Application |
| AXI SRAM | `0x2400_0000` ~ `0x2407_FFFF` | 512 KB | MCUboot RAM-load 執行區；Application image 主體 |
| RAM_D2 | `0x3000_0000` ~ `0x3004_7FFF` | 288 KB | Application ETH / SDMMC DMA buffer、可選 heap |
| RAM_D3 | `0x3800_0000` ~ `0x3800_FFFF` | 64 KB | Application 可選低速資料 / 低功耗域暫存 |
| Backup SRAM | `0x3880_0000` ~ `0x3880_0FFF` | 4 KB | 前 32 bytes 作 Bootloader/Application 交換資訊 |

Bootloader 不應永久佔用一般 SRAM。除 Backup SRAM 交換區外，bootloader 的 stack、heap、SPI buffer、MCUboot 工作 buffer 都只在跳轉前有效；成功跳轉後，Application 可以重新配置 ITCMRAM、DTCMRAM、AXI SRAM、RAM_D2、RAM_D3。

### 4.2 Bootloader runtime RAM 預算

Bootloader 初版建議只使用 DTCMRAM 作 runtime 工作 RAM，避免與 AXI SRAM 中的 RAM-load image 互相覆蓋：

| 項目 | 建議大小 | 位置 | 備註 |
|------|----------|------|------|
| Bootloader stack | 8 KB | DTCMRAM top | YMODEM、MCUboot call chain、HAL driver 使用 |
| Bootloader `.data/.bss` | 8-16 KB | DTCMRAM | 目標值，實際以 map file 確認 |
| SPI page buffer | 256-512 B | DTCMRAM | Page Program / Fast Read 暫存 |
| YMODEM buffer | 1 KB | DTCMRAM | 128/1024 byte packet |
| T-Flash read buffer | 4-8 KB | DTCMRAM | Update Service 才配置/使用 |
| FATFS work area | 2-8 KB | DTCMRAM | 可選，若 Bootloader 支援 T-Flash update |
| MCUboot work buffer | 4-16 KB | DTCMRAM | crypto/hash/backend 依設定變動 |
| Optional log buffer | 0-1 KB | DTCMRAM | 量產可關閉 |
| AXI SRAM RAM-load image | 最多 512 KB | AXI SRAM | 由 MCUboot copy image payload 後驗證 |

驗收條件：

- Bootloader linker map 必須列出 DTCMRAM 實際使用量。
- Bootloader 不使用 ITCMRAM，除非後續量測證明必要。
- Bootloader 不把 heap 設在 AXI SRAM，避免覆蓋待跳轉 application image。
- Bootloader normal boot path 只初始化 GPIO、SPI1、CRC/crypto backend、Backup SRAM 與 MCUboot 必要 platform layer。
- Bootloader 只有進入 Update Service 時才初始化 USART1、SDMMC/FATFS、USB CDC 與其所需 PLL；正常開機不掛載 T-Flash，不啟 USB，不啟 USART recovery。
- `boot_go()` 開始載入 image 前，AXI SRAM 可被完整覆寫。
- 跳轉前不需要顯式「release」RAM；只要 bootloader 不再執行且 Application startup 重新初始化 `.data/.bss/heap/stack`，這些 RAM 即由 Application 接管。

### 4.3 Backup SRAM 交換區

Backup SRAM 前 32 bytes 保留為 Bootloader 與 Application 的交換資訊。此區域可跨 software reset 保留，用於傳遞 boot reason、update result、recovery request 等小量狀態。

低功耗 Standby / VBAT 的持久 resume state 優先使用 RTC backup registers。Bootloader wakeup 後驗證 RTC backup registers 的 `RtcBackupResumeState_t`，或 Backup SRAM extended resume state，再把最後決定的 `boot_reason` 寫入 `BootExchange_t`。若 128-byte RTC backup registers 不足，才啟用 Backup SRAM retention；細節以 `STM32H750LowPowerModeSpec.md` 為準。

```c
#define BOOT_EXCHANGE_ADDR       0x38800000UL
#define BOOT_EXCHANGE_SIZE       32U
#define BOOT_EXCHANGE_MAGIC      0x42455843UL  /* "BEXC" */
#define BOOT_EXCHANGE_VERSION    1U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  boot_slot;
    uint8_t  boot_reason;
    uint8_t  flags;
    uint32_t last_error;
    uint32_t reset_count;
    uint32_t image_version;
    uint32_t reserved0;
    uint32_t crc32;
    uint32_t reserved1;
} BootExchange_t;

_Static_assert(sizeof(BootExchange_t) == BOOT_EXCHANGE_SIZE,
               "BootExchange_t must be 32 bytes");
```

使用規則：

- Bootloader 啟動時先啟用 backup domain / Backup SRAM 存取，再讀取 `BootExchange_t`。
- 寫入前更新 `crc32`；讀取時 magic、version、crc32 都必須有效。
- Application confirm 成功後可更新 `boot_slot`、`image_version`、`last_error = 0`。
- Application 要強制進入 YMODEM recovery 時，可設定 `flags`，再 `HAL_NVIC_SystemReset()`。
- 此 32-byte 區域不可放指標、RTOS handle、mutex 或大資料；其餘 Backup SRAM 先保留，後續可再分配。
- 交換區不是安全信任根，不能用來取代 MCUboot signature / trailer 狀態。
- Standby / VBAT resume 不應呼叫 `boot_set_pending()`、不修改 confirm/revert trailer；MCUboot 只正常選 slot、驗證、載入 AXI SRAM 並跳轉。

### 4.4 SPI NOR Flash 分區

BY25Q32：4 MB，Sector 4 KB，Block 64 KB。

```
SPI NOR Flash 4MB（BY25Q32）
┌─────────────────────────────────┐ 0x00_0000
│  保留 / manufacturing data       │ 16 KB
├─────────────────────────────────┤ 0x00_4000
│                                 │
│  MCUboot Slot A（1 MB）          │
│  - MCUboot image header          │
│  - image payload                 │
│  - TLV                           │
│  - image trailer                 │
│                                 │
├─────────────────────────────────┤ 0x10_4000
│                                 │
│  MCUboot Slot B（1 MB）          │
│  - MCUboot image header          │
│  - image payload                 │
│  - TLV                           │
│  - image trailer                 │
│                                 │
├─────────────────────────────────┤ 0x20_4000
│  未使用保留區                    │ 約 1.73 MB
├─────────────────────────────────┤ 0x3C_0000
│  使用者資料區                    │ 256 KB
└─────────────────────────────────┘ 0x3F_FFFF
```

分區常數：

```c
#define FLASH_AREA_BOOTLOADER        0
#define FLASH_AREA_IMAGE_PRIMARY     1
#define FLASH_AREA_IMAGE_SECONDARY   2

#define FLASH_ADDR_RESERVED          0x000000UL
#define FLASH_ADDR_SLOT_A            0x004000UL
#define FLASH_ADDR_SLOT_B            0x104000UL
#define FLASH_ADDR_USERDATA          0x3C0000UL

#define FLASH_RESERVED_SIZE          0x004000UL
#define FLASH_SLOT_SIZE              0x100000UL
#define FLASH_USERDATA_SIZE          0x040000UL

#define IMAGE_EXECUTABLE_RAM_START   0x24000000UL
#define IMAGE_EXECUTABLE_RAM_SIZE    0x00080000UL

#define BOOT_EXCHANGE_ADDR           0x38800000UL
#define BOOT_EXCHANGE_SIZE           32U
```

注意：

- `FLASH_SLOT_SIZE` 是儲存 signed image 的最大 slot 尺寸。
- V1 標準 MCUboot RAM-load 的 load image 不可超過 `IMAGE_EXECUTABLE_RAM_SIZE = 512 KB`；Application runtime 仍可在 startup 後使用 ITCMRAM / DTCMRAM / RAM_D2。若 initialized code/data 需要突破 512 KB，改走 V2 segmented loader。
- Signed image 還包含 MCUboot header、TLV、signature、trailer，因此 slot 大小必須由 `imgtool --slot-size` 檢查；RAM-load image 大小必須由 linker `ASSERT` 與 bootloader runtime range check 檢查。
- 若後續使用更多 user data，可把 slot 壓到 768 KB；但 v0.1 先維持 1 MB slot，降低 layout 變動風險。

---

## 5. MCUboot Porting 規格

### 5.1 需要整合的 MCUboot 元件

目錄建議：

```
my_projects/
├── bootloader_mcuboot/
│   ├── CMakeLists.txt
│   ├── bootloader_mcuboot.ld
│   ├── mcuboot_config/
│   │   └── mcuboot_config.h
│   ├── keys/
│   │   └── mcuboot_pubkey.c
│   └── Core/Src/
│       ├── main.c
│       ├── clock.c
│       ├── spi_flash_by25q32.c
│       ├── flash_map_backend.c
│       ├── mcuboot_platform.c
│       ├── boot_jump.c
│       └── ymodem_update.c
├── application/
│   ├── application.ld
│   └── Core/Src/
│       ├── main.c
│       ├── ota_mcuboot.c
│       ├── image_confirm.c
│       └── spi_flash_by25q32.c
└── tools/
    ├── sign_mcuboot_image.sh
    └── ota_send.py
```

MCUboot source 可用 git submodule：

```
external/mcuboot/
```

Bootloader 需編入：

- `external/mcuboot/boot/bootutil/src/*.c`
- `external/mcuboot/boot/bootutil/include`
- selected crypto backend source
- local `mcuboot_config/mcuboot_config.h`
- local platform glue

### 5.2 `mcuboot_config.h` 初始方向

實際 macro 名稱需以選定 MCUboot 版本為準。以下為 V1 標準 RAM-load 初版方向：

```c
#pragma once

#define MCUBOOT_SIGN_EC256
#define MCUBOOT_VALIDATE_PRIMARY_SLOT
#define MCUBOOT_RAM_LOAD
#define MCUBOOT_RAM_LOAD_REVERT

#define MCUBOOT_LOG_LEVEL            MCUBOOT_LOG_LEVEL_INFO
#define MCUBOOT_MAX_IMG_SECTORS      256
#define MCUBOOT_FLASH_ALIGN          1

#define IMAGE_EXECUTABLE_RAM_START   0x24000000UL
#define IMAGE_EXECUTABLE_RAM_SIZE    0x00080000UL

#define MCUBOOT_HAVE_ASSERT_H
#define MCUBOOT_USE_TINYCRYPT
```

建議簽章演算法：

- 開發期：ECDSA P-256，整合成本與簽章大小平衡。
- 量產期：可評估 Ed25519 或 ECDSA P-256；以 MCUboot 當前版本與 crypto backend 支援度決定。
- 不建議：只用 unsigned image 或 CRC32 作為正式驗證。

### 5.3 Flash map / `flash_area_*` API

Bootloader 必須提供 MCUboot 要求的 flash area API。BY25Q32 對 MCUboot 呈現為單一 flash device：

```c
struct flash_area {
    uint8_t  fa_id;
    uint8_t  fa_device_id;
    uint16_t pad16;
    uint32_t fa_off;
    uint32_t fa_size;
};
```

初始 mapping：

```c
static const struct flash_area boot_flash_map[] = {
    { FLASH_AREA_IMAGE_PRIMARY,   0, 0, FLASH_ADDR_SLOT_A, FLASH_SLOT_SIZE },
    { FLASH_AREA_IMAGE_SECONDARY, 0, 0, FLASH_ADDR_SLOT_B, FLASH_SLOT_SIZE },
};
```

必要 API：

```c
int flash_area_open(uint8_t id, const struct flash_area **fa);
void flash_area_close(const struct flash_area *fa);
int flash_area_read(const struct flash_area *fa, uint32_t off, void *dst, uint32_t len);
int flash_area_write(const struct flash_area *fa, uint32_t off, const void *src, uint32_t len);
int flash_area_erase(const struct flash_area *fa, uint32_t off, uint32_t len);
uint32_t flash_area_align(const struct flash_area *fa);
uint8_t flash_area_erased_val(const struct flash_area *fa);
int flash_area_get_sectors(int fa_id, uint32_t *count, struct flash_sector *sectors);
int flash_area_id_from_multi_image_slot(int image_index, int slot);
int flash_area_id_to_multi_image_slot(int image_index, int area_id);
```

實作要求：

- `flash_area_read()` 正常 path 可使用 HSI 低速設定；若啟用高速 path，使用 BY25Q32 Fast Read `0x0B`，bootloader SPI1 SCK 60 MHz。
- `flash_area_write()` 需處理 256-byte page program 邊界切分。
- `flash_area_erase()` 只接受 4 KB sector 對齊，必要時可用 64 KB block erase 優化，但初版建議先固定 sector erase。
- `flash_area_get_sectors()` 回傳 256 個 4 KB sector 給每個 1 MB slot。
- `flash_area_align()` 初版回傳 `1`；若 MCUboot crypto/trailer 實測要求較高，再調整為 `4` 或 `8` 並同步 `imgtool --align`。
- `flash_area_close()` 不關閉實體 SPI；MCUboot 可能 nested open，維持 no-op 或 reference count。

### 5.4 SRAM ECC / RAMECC Policy

STM32H750 的 SRAM ECC controller 由硬體 always enabled，提供 SECDED：single-bit error correction 與 double-bit error detection。RAMECC 是監控/診斷/中斷事件收集單元，不是 ECC correction enable switch。

V1 MCUboot-based Bootloader policy：

- 不初始化 RAMECC peripheral。
- 不啟用 RAMECC interrupt。
- Bootloader 依賴 always-on SRAM ECC correction。
- MCUboot RAM-load 將 image 複製到 AXI SRAM 後，驗證對象必須是 AXI SRAM 中即將執行的 image。
- 若後續要保留診斷資料，可在 debug / recovery build 加入 RAMECC polling，但不得讓 RAMECC IRQ 狀態跨到 Application。

若發現 double-bit ECC error，Bootloader 不得跳轉該 image，應進入 Update Service 或 reset/fault 策略。

---

## 6. Image 格式與簽署流程

### 6.1 載入策略選擇

本專案定義兩種 Application 載入策略：

| 策略 | Bootloader 行為 | Application 行為 | 優點 | 限制 |
|------|-----------------|------------------|------|------|
| V1：標準 MCUboot RAM-load | MCUboot 將單一 image payload 複製到 AXI SRAM `0x24000000`，驗證後跳轉 | startup 自行搬 `.itcm_vector` / `.itcm_isr` / `.itcm_text` / `.dtcm_data`，清 `.bss` / `.dtcm_bss` | 最接近 MCUboot 原生模型，bootloader 簡單穩定，仍可把關鍵 ISR 放 ITCM | signed load image 必須放得進 AXI SRAM 512 KB |
| V2：Segmented RAM-load | Bootloader 驗證 flash slot 內的 signed image 後，依 signed segment manifest 搬移 AXI/ITCM/DTCM 多段 | startup 只做一般 C runtime 初始化，必要時可少搬或不搬 ITCM/DTCM | 可使用 AXI + ITCM + DTCM 承載 initialized code/data，開發者負擔較低 | 不能直接使用 stock MCUboot RAM-load copy path，需要本專案 loader 擴展 |

建議里程碑：

- Phase 1 使用 V1，先驗證 MCUboot signature、revert、confirm、OTA、YMODEM。
- 若 application initialized code/data 接近或超過 AXI SRAM 512 KB，再升級到 V2。
- V2 不允許在 MCUboot image 外放未簽章自訂 metadata。segment manifest 必須位於 MCUboot hash/signature 保護範圍內。
- V2 是 build-time mode，不能直接沿用 stock `MCUBOOT_RAM_LOAD` 對整個 image payload 做一次性 AXI SRAM 複製，否則仍會被 512 KB 限制。

### 6.2 V1：Application startup 搬移 ITCM/DTCM

Application 的 image 入口與主體仍以 AXI SRAM 為基準；需要確定性或低延遲的 vector table、ISR、code/data 可由 startup 從 AXI SRAM load image 複製到 ITCMRAM / DTCMRAM。V1 中 Bootloader 初始跳轉仍使用 AXI SRAM `0x24000000` 的 boot vector；Application early startup 複製 `.itcm_vector` 後，再把 `SCB->VTOR` 改到 ITCM `0x00000000`。

```ld
MEMORY
{
    ITCM   (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    AXIRAM (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
    RAM_D2 (rw)  : ORIGIN = 0x30000000, LENGTH = 288K
    BKPSRAM(rw)  : ORIGIN = 0x38800020, LENGTH = 4064
}

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > AXIRAM
    .text       : { *(.text*) *(.rodata*) } > AXIRAM
    .data       : { *(.data*) } > AXIRAM
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > AXIRAM

    .itcm_vector :
    {
        . = ALIGN(1024);
        _sitcm_vector = .;
        KEEP(*(.itcm_vector))
        . = ALIGN(1024);
        _eitcm_vector = .;
    } > ITCM AT> AXIRAM
    _sitcm_vector_load = LOADADDR(.itcm_vector);

    .itcm_text :
    {
        . = ALIGN(4);
        _sitcm_text = .;
        *(.itcm_isr*)
        *(.itcm_text*)
        . = ALIGN(4);
        _eitcm_text = .;
    } > ITCM AT> AXIRAM
    _sitcm_text_load = LOADADDR(.itcm_text);

    .dtcm_data :
    {
        . = ALIGN(4);
        _sdtcm_data = .;
        *(.dtcm_data*)
        . = ALIGN(4);
        _edtcm_data = .;
    } > DTCM AT> AXIRAM
    _sdtcm_data_load = LOADADDR(.dtcm_data);

    .dtcm_bss (NOLOAD) : { *(.dtcm_bss*) } > DTCM
    .dma_buffer (NOLOAD) : { *(.dma_buffer) } > RAM_D2
    .bkpsram (NOLOAD) : { *(.bkpsram*) } > BKPSRAM

    __image_end__ = LOADADDR(.dtcm_data) + SIZEOF(.dtcm_data);

    ._stack (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + 0x2000;
        _estack = .;
    } > DTCM

    ASSERT(__image_end__ <= ORIGIN(AXIRAM) + LENGTH(AXIRAM), "Application load image exceeds AXI SRAM");
}
```

實作時可用專案既有 end symbol 取代 `__image_end__`；重點是檢查最後一個 loadable section 的 LMA 不超過 AXI SRAM。若使用 `.itcm_vector` / `.itcm_isr` / `.itcm_text` / `.dtcm_data`，startup 必須在 `main()` 前從 `*_load` 複製到對應 VMA，並清除 `.dtcm_bss`。若啟用 `.itcm_vector`，必須在複製完成後、任何 IRQ enable 前設定 `SCB->VTOR = 0x00000000`，並執行 `__DSB(); __ISB();`。`BKPSRAM` 起點從 `0x38800020` 開始，避免覆蓋前 32-byte boot exchange block。

### 6.3 V1 `imgtool` 簽署命令

開發金鑰：

```sh
python3 external/mcuboot/scripts/imgtool.py keygen \
  -k keys/dev_ecdsa_p256.pem \
  -t ecdsa-p256
```

產生 public key C source：

```sh
python3 external/mcuboot/scripts/imgtool.py getpub \
  -k keys/dev_ecdsa_p256.pem \
  > bootloader_mcuboot/keys/mcuboot_pubkey.c
```

簽署 RAM-load image：

```sh
python3 external/mcuboot/scripts/imgtool.py sign \
  --key keys/dev_ecdsa_p256.pem \
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

說明：

- `--load-addr 0x24000000` 是 RAM-load 必要參數，會在 MCUboot image header 設定 RAM load flag 與 load address。
- `--slot-size 0x100000` 讓 `imgtool` 檢查 image 不會覆蓋 trailer。
- `--pad` 會加入 trailer magic；RAM-load revert 模式需要 signed image 帶 trailer。
- 若要把出廠 image 預先標為 confirmed，可用 `--confirm`；OTA 測試更新則用未 confirmed 的 `--pad` image。
- 初版不啟用 image encryption；若後續要加入加密，需重新確認所選 MCUboot 版本、`imgtool` 參數與 RAM-load 模式的支援狀態。
- `--slot-size 0x100000` 不代表 AXI SRAM 放得下；bootloader 必須在 `boot_go()` 後再次檢查 RAM-load image 起點、大小與 end address 不超過 `IMAGE_EXECUTABLE_RAM_START + IMAGE_EXECUTABLE_RAM_SIZE`。

### 6.4 V2：Segmented RAM-load 擴展

當 Application 不希望被 512 KB AXI SRAM load image 限制，或希望開發者能自然使用 ITCM/DTCM 放 initialized code/data，可升級為 V2 segmented loader。

V2 原則：

- 仍使用 MCUboot image header / TLV / signature / trailer。
- Bootloader 使用 MCUboot image selection / trailer 狀態與 signature 驗證邏輯，但 V2 不走 stock `boot_go()` RAM-load copy path。
- Bootloader 必須先驗證 flash slot 內的 signed image，再解析 segment manifest。
- V2 初版固定把 segment manifest 放在 image payload 起點，且必須被 MCUboot hash/signature 覆蓋；protected TLV 版本只作未來擴展。
- Bootloader 依 manifest 將各段從 SPI NOR slot 複製到目標 RAM：
  - boot vector + normal text/data → AXI SRAM
  - runtime vector table + deterministic ISR/code → ITCMRAM
  - critical data / RTOS kernel data → DTCMRAM
  - zero-init data → 由 bootloader 或 application startup 清零
- V2 初版仍建議跳轉前 `SCB->VTOR` 指向 AXI SRAM `0x24000000`，Reset_Handler 位於 AXI SRAM；Application early startup 再切換 runtime VTOR 到 ITCM。若要讓 Bootloader 直接跳 ITCM vector，manifest 必須額外標記 `vector_addr`、`msp_addr`、`reset_handler`，並加強 range check。
- 不允許 segment 目標覆蓋 Backup SRAM exchange block `0x38800000` ~ `0x3880001F`。

建議 manifest 結構：

```c
#define BOOT_SEGMENT_MAGIC      0x5345474DUL  /* "SEGM" */
#define BOOT_SEGMENT_VERSION    1U
#define BOOT_SEGMENT_MAX_COUNT  8U

typedef enum {
    BOOT_SEGMENT_LOAD = 0x01,  /* Copy file_size bytes from signed image payload to dst_addr. */
    BOOT_SEGMENT_ZERO = 0x02,  /* Zero-fill dst_addr + file_size through mem_size, used for BSS-like regions. */
    BOOT_SEGMENT_EXEC = 0x04,  /* Segment contains executable code or vector data; range/alignment checks are stricter. */
} BootSegmentFlags_t;

typedef struct __attribute__((packed)) {
    uint32_t src_offset;    /* offset from MCUboot image payload start */
    uint32_t dst_addr;      /* AXI/ITCM/DTCM destination */
    uint32_t file_size;     /* bytes to copy from signed image */
    uint32_t mem_size;      /* bytes to reserve/zero at destination */
    uint32_t flags;
} BootSegmentEntry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t segment_count;
    uint32_t entry_addr;    /* expected Reset_Handler address or 0 */
    BootSegmentEntry_t segment[BOOT_SEGMENT_MAX_COUNT];
    uint32_t crc32;         /* manifest structural check only; signature is MCUboot */
} BootSegmentManifest_t;
```

#### 6.4.1 Segment packer 工具

V2 需要新增一個 host-side segment packer，例如 `tools/mksegimage.py`。正式輸入應使用 Application ELF，不建議從 `.hex` 或 raw `.bin` 反推：

- ELF 保留 section name、VMA、LMA、size、alignment、entry point 與 symbol，適合辨識 `.itcm_vector`、`.itcm_isr`、`.itcm_text`、`.dtcm_data`、`.data` 等區段。
- Intel HEX / SREC 只保留 address + data，section name 與原始用途已遺失，仍可燒錄但不適合自動判斷 ISR/code/data 類型。
- raw `.bin` 幾乎只剩連續 bytes，沒有 address / section / alignment 資訊，不適合作為 segmented image 的來源。

建議工具流程：

```
application.elf
 │
 ├── 讀取 ELF section / program header / entry point
 ├── 選出 SHF_ALLOC 且需要初始化的區段
 ├── 依 VMA 判斷目標 RAM：AXI / ITCM / DTCM / RAM_D2
 ├── 依 section name 與 flags 設定 BOOT_SEGMENT_LOAD / ZERO / EXEC
 ├── 產生 BootSegmentManifest_t
 ├── 依 manifest 順序輸出 segment blobs
 ▼
application.segmented.payload = [BootSegmentManifest_t][segment blobs...]
 │
 ▼
imgtool.py sign --header-size 0x200 --slot-size 0x100000 --pad-header --pad \
  application.segmented.payload application.signed.bin
```

V2 payload 規則：

- V2 signed image 不使用 `--load-addr 0x24000000`，避免 MCUboot 設定 RAM-load flag 後嘗試把整個 segmented payload 複製到 AXI SRAM。
- `BootSegmentManifest_t` 固定放在 MCUboot image payload 起點，且必須被 MCUboot hash/signature 覆蓋。
- `BootSegmentEntry_t.src_offset` 以 MCUboot image payload 起點為 0；因此第一個 blob 的 offset 必須大於等於 `sizeof(BootSegmentManifest_t)`。
- `file_size > 0` 的 section 由 packer 從 ELF section content 抽出，Bootloader 依 `src_offset` 從 signed image payload 複製到 `dst_addr`。
- `.bss` / `.dtcm_bss` 這類 `SHT_NOBITS` section 不輸出 blob，`file_size = 0`、`mem_size > 0`、flags 包含 `BOOT_SEGMENT_ZERO`。
- `.itcm_vector`、`.itcm_isr`、`.itcm_text` 目標位址必須落在 ITCMRAM；flags 必須包含 `BOOT_SEGMENT_EXEC`。
- `.dtcm_data` 目標位址必須落在 DTCMRAM；若是 initialized data，flags 包含 `BOOT_SEGMENT_LOAD`。
- boot vector 與 Reset_Handler 初版仍應保留在 AXI SRAM `0x24000000` image 入口，讓 Bootloader 用標準 MSP/PC 檢查跳轉；ITCM vector 屬於 Application runtime 切換。
- packer 必須產生 map/report，列出每個 segment 的 section name、VMA、file_size、mem_size、flags、src_offset，並在 CI 中保存。

正式實作建議使用 Python + `pyelftools` 解析 ELF；linker map 只能作人工 review 或 report 輔助，不作唯一真相來源。

V2 boot flow：

```
MCUboot image selection / trailer state check
 │
 ▼
驗證 flash slot 內 image header + hash + signature + trailer
 │
 ▼
解析 signed segment manifest
 │
 ├── 檢查 segment_count / address range / overlap / alignment
 ├── 檢查 dst_addr 只能落在 AXI、ITCM、DTCM、RAM_D2（如允許）
 └── 禁止覆蓋 boot exchange block
 │
 ▼
依 segment 複製或清零
 │
 ▼
clean/invalidate cache，設定 VTOR/MSP/PC
 │
 ▼
跳轉 Application
```

V2 風險：

- 這不是 MCUboot 標準 RAM-load 的最小路徑，需要維護本專案自己的 loader 擴展。
- 若誤用 stock `boot_go()` RAM-load copy path，整包 payload 仍會被複製到 AXI SRAM，V2 會退化並再次受到 512 KB 限制。
- 必須固定 MCUboot commit，避免 protected TLV 或 bootutil 驗證流程升級時破壞相容性。
- manifest 生成工具必須以 ELF 為真相來源，linker map 只作 review/report 輔助；manifest 不能手寫，否則開發者負擔與出錯率會更高。
- Bootloader 要加入 segment address/overlap 檢查，否則壞 image 可能覆蓋 bootloader runtime RAM 或 Backup SRAM。

---

## 7. OTA 更新流程

### 7.1 TCP OTA

Application 透過 LwIP 接收 `application.signed.bin`：

```
Application running
 │
 ▼
讀取 MCUboot trailer / active selection 狀態，決定 inactive slot
 │
 ▼
抹除 inactive slot 全部 1 MB
 │
 ▼
逐頁寫入 signed image
 │
 ▼
讀回並做基本完整性檢查
 - 長度 <= FLASH_SLOT_SIZE
 - image magic 正確
 - erased tail 為 0xFF
 │
 ▼
呼叫 Image_RequestTestUpgrade() 將新 image 標記為 test upgrade
 │
 ▼
回報成功，等待使用者重啟或自動 HAL_NVIC_SystemReset()
```

若使用 MCUboot RAM-load equal-slot selection，OTA client 必須確保新 image version 高於目前 confirmed image。版本不增加時，bootloader 可能不會選擇新 image。

### 7.2 Bootloader Update Service

Bootloader update mode 可由兩種方式觸發：

- KEY2（PC13）上電長按 3 秒。
- Application 在 Backup SRAM `BootExchange_t.flags` 設定 recovery/update request 後 software reset。
- MCUboot 找不到任何有效可啟動 slot。

Update Service 的優先順序：

```
進入 Update Service
 │
 ▼
初始化 SDMMC1 + FATFS（T-Flash）
 │
 ├── 掛載失敗 / 無卡 / 初始化 timeout
 │        │
 │        ▼
 │   進入 UART YMODEM Recovery
 │
 └── 掛載成功
          │
          ▼
     搜尋指定檔案
     - /STM32H750_UPDATE.BIN
     - /stm32h750_update.bin
          │
          ├── 找不到指定檔案 ──► 進入 UART YMODEM Recovery
          │
          ▼
     檢查檔案大小與 MCUboot image magic
          │
          ├── 無效 ──► 進入 UART YMODEM Recovery
          │
          ▼
     寫入 inactive slot
     讀回基本檢查
     設定 test trailer
     可選：將檔案改名為 *.DONE 或 *.BAD
     reboot
```

T-Flash 檔案規則：

- 檔案必須是 `imgtool.py sign` 產生的 MCUboot signed image，不接受 raw application `.bin`。
- 檔名初版固定為 `/STM32H750_UPDATE.BIN`；為了相容 FAT 大小寫，也接受 `/stm32h750_update.bin`。
- 檔案大小必須 `> MCUboot header size` 且 `<= FLASH_SLOT_SIZE`。
- Bootloader 只做快速結構檢查：image magic、大小、slot 邊界、讀寫錯誤。完整 signature 驗證交給後續 MCUboot boot flow。
- 若檔案無效，不應覆蓋目前 confirmed slot；最多只寫 inactive slot，且失敗時不設定 test trailer。
- 若更新成功，可把檔案改名為 `/STM32H750_UPDATE.DONE`；若寫入或檢查失敗，可改名為 `/STM32H750_UPDATE.BAD`。若 FATFS rename 失敗，不影響安全性。

Bootloader SDMMC/FATFS 約束：

- 正常開機路徑不初始化 SDMMC/FATFS，只在 Update Service 中使用。
- SDMMC clock 可採低速保守設定，穩定優先，不追求最高讀取速度。
- T-Flash read buffer 使用 DTCMRAM 4-8 KB；不得放在 AXI SRAM，以免覆蓋 RAM-load image。
- FATFS 只需 read-only + rename 能力；若要最小化 bootloader，可先不做 rename。
- T-Flash update 失敗時 fallback 到 UART YMODEM，不直接進入正常 boot，避免使用者以為離線更新已執行。

Update Service clock / peripheral 原則：

- `bootloader.ioc` 可包含 USART1、SDMMC1、USB OTG FS 等 recovery 外設，但 `main()` 正常路徑不得呼叫其 init。
- USB CDC Recovery 若啟用，僅在 Update Service 中啟動 USB clock；建議使用 HSE 25 MHz → PLL3Q 48 MHz，或 HSI48 + CRS。
- SDMMC1 / FATFS 僅在 T-Flash update path 啟動；clock 採低速保守設定。
- UART YMODEM 僅在 T-Flash / USB CDC 不可用或 timeout 時啟動。
- 正常 boot path 的 SPI1 可先用 HSI 64 MHz 下的保守分頻；若 copy time 不足，再加入可選 PLL2P 120 MHz → SPI1 60 MHz fast path，並保留低速 fallback。

### 7.3 UART YMODEM Recovery

YMODEM 仍在 bootloader 內：

```
KEY2 長按 3 秒
 │
 ▼
Bootloader 進入 Update Service
 │
 ├── T-Flash 無卡/無指定檔案/檔案無效
 │
 ▼
Bootloader 進入 UART YMODEM
 │
 ▼
接收 signed image
 │
 ▼
選擇可寫入 slot
 - 若只有一個有效 confirmed image：寫另一個 slot
 - 若兩個都無效：寫 Slot A
 - 若兩個都有效：寫非目前選定 image 的 slot
 │
 ▼
抹除 → 寫入 → 基本檢查 → 設定 test trailer → reboot
```

YMODEM 不解析自訂 OTA header；PC 端傳送檔案就是 `application.signed.bin`。

---

## 8. Application Confirm 流程

Application 在完成最小自檢後才 confirm：

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_ETH_Init();
    MX_SPI1_Init();
    MX_FDCAN1_Init();
    MX_USART1_UART_Init();
    MX_SDMMC1_SD_Init();
    MX_USB_OTG_FS_PCD_Init();
    MX_RTC_Init();

    if (App_SelfTest() == APP_SELFTEST_OK) {
        Image_ConfirmCurrent();
    }

    while (1) {
        App_Run();
    }
}
```

`Image_ConfirmCurrent()` 可採兩種實作策略：

1. 連結 MCUboot bootutil 的 public API，呼叫等效 `boot_set_confirmed()`。
2. 若不想讓 Application 連結完整 bootutil，實作最小 trailer writer，但必須嚴格使用 MCUboot 版本對應的 trailer layout，不建議手寫到無法追蹤版本。

建議採策略 1，避免 trailer layout 與 MCUboot 版本脫節。

共用 wrapper 必須固定在 shared API，避免各 RTOS 各自操作 MCUboot trailer：

```c
int Image_RequestTestUpgrade(void)
{
    return boot_set_pending(false);  /* test upgrade, not permanent */
}

int Image_ConfirmCurrent(void)
{
    return boot_set_confirmed();
}
```

若選定 MCUboot 版本的 API 名稱不同，wrapper 內部可調整，但 Application 其他模組只允許呼叫 `Image_RequestTestUpgrade()` / `Image_ConfirmCurrent()`。

---

## 9. Application RTOS 整合策略

### 9.1 共通原則

Bootloader 與 Application OS 必須解耦：

- Bootloader 不連結、不初始化、不依賴 Application 使用的 RTOS。
- Application 無論是 bare-metal、Zephyr、ThreadX、FreeRTOS、RT-Thread 或 NuttX，都必須被建成 **RAM-load MCUboot image**。
- Application boot vector table 起點必須是 `0x24000000`，Reset_Handler 由 bootloader 直接 branch 進入。
- Application 必須在自己的 startup / early init 階段先設定 `SCB->VTOR = 0x24000000`，避免 RTOS 或 HAL 重新初始化時覆蓋中斷向量。
- 若使用 ITCM runtime vector table，Application 必須先複製 `.itcm_vector` 與 `.itcm_isr`，再於任何 IRQ enable 前設定 `SCB->VTOR = 0x00000000`。
- Application linker script 必須讓 boot vector table 與 RAM-load image 主體從 `0x24000000` 開始；時間確定性要求高的 vector/ISR/code/data 可由 startup 搬到 ITCMRAM / DTCMRAM；ETH / SDMMC DMA buffer 仍放 RAM_D2。
- Bootloader 的 DTCMRAM stack/heap/buffer 在跳轉後不保留，Application 可重新把 DTCMRAM 規劃為 stack、heap、控制迴圈資料或 RTOS kernel 重要資料。
- Backup SRAM 前 32 bytes 是 boot exchange block，Application 不可拿來當一般 `.bss` 或 heap。
- Application 必須提供「啟動成功確認」流程，完成最小自檢後才 confirm image。
- Application 的 OTA writer 必須寫入 inactive slot 的 MCUboot signed image，不寫自訂 `AppHeader_t` 或 `FlashMetadata_t`。
- 若 RTOS 會同時存取 SPI NOR，OTA / confirm / user data 需由同一個 flash driver lock 保護，避免 task 之間交錯 erase/write。

共用 early init 建議：

```c
void SystemInit(void)
{
    /* Boot vector used immediately after Bootloader jump. */
    SCB->VTOR = 0x24000000UL;
    __DSB();
    __ISB();

    /*
     * Optional deterministic interrupt path:
     * 1. Copy .itcm_vector / .itcm_isr from AXI LMA to ITCM VMA.
     * 2. Set SCB->VTOR = 0x00000000UL.
     * 3. Enable IRQ only after DSB/ISB.
     */

    /* RTOS/HAL vendor SystemInit 其餘時脈或 FPU 設定照原流程執行。 */
}
```

共用 confirm 時機：

```c
static void App_ConfirmAfterSelfTest(void)
{
    if (App_SelfTest() != APP_SELFTEST_OK) {
        return;
    }

    Image_ConfirmCurrent();
}
```

### 9.2 Zephyr

Zephyr 有既有 MCUboot integration 與 image management API，可採兩種策略：

| 策略 | 說明 | 適用情境 |
|------|------|----------|
| Zephyr-native MCUboot | 用 Zephyr sysbuild / partition manager / flash map 產生 bootloader 與 app | 願意整體改成 Zephyr 管理 build 與 flash layout |
| 本規格 custom MCUboot | Bootloader 仍是 STM32Cube/HAL；Zephyr 只作為 application，輸出 signed RAM-load image | 想保留本專案 bootloader 硬體初始化與 YMODEM recovery |

本規格採第二種，Zephyr application 需處理：

- Board / DTS 要描述 BY25Q32 或提供 application 端 SPI NOR driver，供 OTA writer 與 confirm API 存取 trailer。
- Zephyr linker / devicetree memory region 必須把 image 建到 `0x24000000`，不可使用內部 Flash 作為 code region。
- V1 build 後使用 `west sign` 或 MCUboot `imgtool.py sign`，簽署時帶 `--load-addr 0x24000000`。
- V2 build 不使用 `--load-addr`；需先由 `mksegimage.py` 從 ELF 產生 segmented payload，再交給 `imgtool.py sign`。
- application 啟用 MCUboot image control API 後，可用 Zephyr API：

```c
#include <zephyr/dfu/mcuboot.h>

int Image_ConfirmCurrent(void)
{
    return boot_write_img_confirmed();
}

int Image_RequestTestUpgrade(void)
{
    return boot_request_upgrade(BOOT_UPGRADE_TEST);
}
```

注意：

- `boot_write_img_confirmed()` 應在網路、儲存、主要任務最小自檢成功後呼叫。
- 若 Zephyr flash map 無法直接對應本規格的 external SPI NOR slot，先不要用 Zephyr DFU subsystem 自動寫 slot，改由專案自己的 OTA writer 寫 BY25Q32，再呼叫相容的 request/confirm 流程。
- Zephyr thread 啟動後才跑 OTA，flash driver 必須 thread-safe；寫入期間禁止其他 task 存取同一顆 SPI NOR。

### 9.3 ThreadX

ThreadX application 建議走「一般 Cortex-M bare-metal startup + ThreadX kernel」模式：

- startup code 的 boot vector table 放在 `0x24000000`。
- `SystemInit()` 先設定 `SCB->VTOR = 0x24000000`；若啟用 ITCM runtime vector，再於複製 `.itcm_vector` / `.itcm_isr` 後切到 `0x00000000`。
- `main()` 完成 HAL、clock、driver 初始化後呼叫 `tx_kernel_enter()`。
- `Image_ConfirmCurrent()` 可放在 `main()` 進入 kernel 前，或放在第一個 application thread 中。

建議流程：

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    App_DriverInit();

    if (App_MinimalSelfTest() == APP_SELFTEST_OK) {
        Image_ConfirmCurrent();
    }

    tx_kernel_enter();
}
```

若使用 NetX Duo / FileX：

- OTA task 下載完成後，需暫停其他可能存取 SPI NOR 的 file/log task。
- 寫入 inactive slot 前先取得 global flash mutex。
- confirm 或 request upgrade 的 trailer write 必須與 OTA slot write 使用同一把 mutex。

### 9.4 FreeRTOS

FreeRTOS application 同樣視為一般 RAM-resident Cortex-M firmware：

- boot vector table 與 linker base 固定 `0x24000000`；可選 ITCM runtime vector 必須在 `vTaskStartScheduler()` 前完成複製與 `SCB->VTOR` 切換。
- `vTaskStartScheduler()` 前必須完成 HAL/clock/init。
- confirm 可在 scheduler 啟動前執行，也可由第一個 high-priority init task 執行。
- 若使用 FreeRTOS+TCP、coreMQTT、AWS IoT OTA Agent，OTA Agent 只負責下載 payload；寫入格式仍必須是 `application.signed.bin`。

建議流程：

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    App_DriverInit();

    xTaskCreate(AppInitTask, "init", 1024, NULL, tskIDLE_PRIORITY + 3, NULL);
    vTaskStartScheduler();

    while (1) {
    }
}

static void AppInitTask(void *arg)
{
    if (App_MinimalSelfTest() == APP_SELFTEST_OK) {
        Image_ConfirmCurrent();
    }

    vTaskDelete(NULL);
}
```

FreeRTOS 注意事項：

- SPI NOR driver 必須用 mutex 保護，且 erase/write 期間不可在 ISR 內操作。
- OTA task 寫 flash 時要餵 watchdog，或拆成小區塊讓系統維持排程。
- 若 D-cache 開啟，網路 DMA buffer 放 RAM_D2，OTA 接收 buffer 進入 flash write 前需處理 cache coherency。

### 9.5 RT-Thread

RT-Thread application 可用兩種方式整合：

| 方式 | 說明 |
|------|------|
| RT-Thread 作純 application | Bootloader 依本規格；RT-Thread 只需輸出 RAM-load signed image |
| RT-Thread OTA framework 整合 | RT-Thread OTA/download 組件只負責傳輸，底層 writer 改寫 inactive MCUboot slot |

RT-Thread 需處理：

- linker script / scatter file 以 `0x24000000` 為 image base。
- startup 早期先設定 `SCB->VTOR = 0x24000000`；若啟用 ITCM runtime vector，必須在 RT-Thread 中斷初始化前切到 `0x00000000`。
- `rt_hw_board_init()` 後完成基本 driver 檢查，再 confirm image。
- 若使用 DFS / SFUD / SPI flash device，同一顆 BY25Q32 的 user data 與 OTA slot 必須有分區邊界與鎖保護。

建議：

```c
int main(void)
{
    if (App_MinimalSelfTest() == APP_SELFTEST_OK) {
        Image_ConfirmCurrent();
    }

    App_StartThreads();
    return 0;
}
```

RT-Thread 注意事項：

- 若 SFUD 已管理 BY25Q32，MCUboot trailer writer 應透過同一個 SFUD device 或統一底層 SPI lock，不要另開一套無鎖 SPI driver。
- OTA package 不使用 RT-Thread 自訂 boot header，除非外層封包最後會剝成 MCUboot signed image 再寫入 slot。

### 9.6 NuttX

NuttX 已有官方 MCUboot port。若要完整 NuttX-native，可使用 NuttX MTD partition、BCH/FTL 與 `CONFIG_MCUBOOT_BOOTLOADER`；但本規格仍建議先採 custom bootloader + NuttX application：

- NuttX application image 建到 `0x24000000`。
- board-specific startup 先設定 `SCB->VTOR = 0x24000000`；若啟用 ITCM runtime vector，必須在 NuttX IRQ attach/enable 前完成複製與 VTOR 切換。
- BY25Q32 可由 NuttX MTD driver 管理；OTA writer 寫 inactive slot 對應的 MTD partition。
- confirm 可透過 NuttX MCUboot example/API 風格實作，或由本專案提供小型 `Image_ConfirmCurrent()` wrapper。

若改採 NuttX-native MCUboot port，需重新定義：

- `CONFIG_MCUBOOT_PRIMARY_SLOT_PATH`
- `CONFIG_MCUBOOT_SECONDARY_SLOT_PATH`
- `CONFIG_MCUBOOT_SCRATCH_PATH` 或等效 RAM-load/equal-slot 配置
- `board_boot_image()`，負責 deinit peripherals、設定 MSP/PC、跳轉 image

注意：

- NuttX 官方文件提醒其 MCUboot flash map backend 不是任意 multitasking-safe；本專案 OTA 與 confirm 應集中在單一 task，或加 board-level lock。
- 若使用 NuttX POSIX file descriptor 操作 MTD slot，不要跨 task 共用同一個 `flash_area`/file descriptor 狀態。

### 9.7 OS 選型建議

| Application OS | 建議整合方式 | 主要工作 |
|----------------|--------------|----------|
| Bare-metal / HAL | 最簡單基準 | linker、VTOR、confirm wrapper、OTA writer |
| Zephyr | 可行，但要小心 Zephyr flash map 與本 custom bootloader layout 對齊 | devicetree/linker、`boot_write_img_confirmed()`、OTA writer |
| ThreadX | 可行，接近 bare-metal | startup/linker、mutex、NetX/FileX OTA task |
| FreeRTOS | 可行，接近 bare-metal | startup/linker、flash mutex、OTA task/watchdog |
| RT-Thread | 可行，需統一 SFUD/flash device 存取 | startup/linker、SFUD lock、OTA package 轉 MCUboot image |
| NuttX | 可 custom app 或 NuttX-native MCUboot | MTD partition、board boot glue、task-safety |

第一階段建議先用 **bare-metal/HAL application** 驗證 MCUboot RAM-load、sign、confirm、revert。確認 boot chain 穩定後，再導入目標 RTOS。這樣可以把問題切開：先證明 bootloader 與 image 格式正確，再處理 OS driver 與 OTA task。

---

## 10. Bootloader 跳轉規格

V1 使用 `boot_go()` 後，bootloader 依 `boot_rsp` 跳轉；RAM-load 模式下，選定 image 已在 AXI SRAM 的 load address。V2 不使用 stock `boot_go()` RAM-load copy path；Bootloader 驗證 flash slot 內 signed image 後，依 manifest 把 segment 分散搬到 AXI/ITCM/DTCM。初版仍讓 boot vector 與 Reset_Handler 固定在 AXI SRAM `0x24000000` 起跳；Application early startup 可再把 runtime vector table 切到 ITCM。

```c
static void Boot_JumpToRamImage(uint32_t image_base)
{
    uint32_t *vectors = (uint32_t *)image_base;
    uint32_t app_sp = vectors[0];
    uint32_t app_entry = vectors[1];

    bool sp_in_dtcm = (app_sp >= 0x20000000UL && app_sp <= 0x20020000UL);
    bool sp_in_axi = (app_sp >= 0x24000000UL && app_sp <= 0x24080000UL);
    if (!sp_in_dtcm && !sp_in_axi) {
        return;
    }
    if (app_entry < 0x24000000UL || app_entry >= 0x24080000UL) {
        return;
    }

    __disable_irq();

    HAL_RCC_DeInit();
    HAL_DeInit();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    SCB->VTOR = image_base;
    __DSB();
    __ISB();

    __set_MSP(app_sp);
    ((void (*)(void))app_entry)();

    while (1) {
    }
}
```

V1 `boot_go()` 後必須做 RAM-load 範圍檢查：

```c
static bool Boot_ValidateRamLoadRange(const struct boot_rsp *rsp)
{
    uint32_t start = rsp->br_image_addr;
    uint32_t size = rsp->br_hdr->ih_img_size;
    uint32_t end = start + size;

    if (start < IMAGE_EXECUTABLE_RAM_START) {
        return false;
    }
    if (end < start) {
        return false;
    }
    if (end > IMAGE_EXECUTABLE_RAM_START + IMAGE_EXECUTABLE_RAM_SIZE) {
        return false;
    }

    return true;
}
```

實際欄位名稱需以選定 MCUboot 版本的 `struct boot_rsp` / image header 為準；重點是 V1 runtime check 不能只依賴 linker `ASSERT` 或 `imgtool --slot-size`。V2 則必須對每個 manifest segment 分別檢查 range / overlap / alignment。

實作注意：

- 若 bootloader 啟用 I-cache / D-cache，跳轉前需依實際 cache policy 做 clean/invalidate。
- 複製到 AXI SRAM 後，若 D-cache 開啟，必須確保 vector table 與 text 可被 CPU 正確取指。
- Application 重新設定 MCO1 給 LAN8720A REF_CLK；bootloader 跳轉前不啟用 ETH。

---

## 11. 建構系統

### 11.1 Bootloader CMake 方向

```cmake
set(CPU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")

add_executable(bootloader_mcuboot
    Core/Src/main.c
    Core/Src/clock.c          # HSI64 minimal clock + optional PLL2/PLL3 recovery clocks
    Core/Src/spi_flash_by25q32.c
    Core/Src/flash_map_backend.c
    Core/Src/mcuboot_platform.c
    Core/Src/boot_jump.c
    Core/Src/tflash_update.c  # optional
    Core/Src/usb_cdc_update.c # optional
    Core/Src/ymodem_update.c
    keys/mcuboot_pubkey.c
    # external/mcuboot/boot/bootutil/src/*.c
)

target_include_directories(bootloader_mcuboot PRIVATE
    mcuboot_config
    external/mcuboot/boot/bootutil/include
    external/mcuboot/boot/bootutil/src
)

target_compile_options(bootloader_mcuboot PRIVATE
    ${CPU_FLAGS}
    -Os
    -ffunction-sections
    -fdata-sections
    -Wall
)

target_link_options(bootloader_mcuboot PRIVATE
    ${CPU_FLAGS}
    -Tbootloader_mcuboot.ld
    -Wl,--gc-sections
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,-Map=bootloader_mcuboot.map
)
```

### 11.2 大小預算

| 項目 | 目標 |
|------|------|
| Bootloader 內部 Flash | `< 128 KB`，目標 `< 96 KB` |
| Bootloader DTCMRAM runtime | 目標 `< 32 KB`，上限 `< 64 KB` |
| Application AXI SRAM load image | `< 512 KB`，含 vector/text/rodata/data 與 ITCM/DTCM initialized load image |
| V2 segmented initialized image | 可超過 512 KB，但每個 segment 必須落在對應 RAM 範圍 |
| Application ITCMRAM | 最多 64 KB，可選 |
| Application DTCMRAM | 最多 128 KB，跳轉後可完整接管 |
| 每個 SPI NOR slot | `1 MB` |
| YMODEM buffer | `1 KB` |
| T-Flash read buffer | `4-8 KB`，僅 Update Service 使用 |
| FATFS work area | `2-8 KB`，僅啟用 T-Flash update 時使用 |
| SPI temporary buffer | `256 B` 或 `512 B` |
| Boot exchange block | Backup SRAM 前 32 bytes |

如果 ECDSA + Mbed TLS 讓 bootloader 超過 128 KB，優先調整：

- 使用 TinyCrypt + 必要 Mbed TLS 子集。
- 關閉不使用的 MCUboot feature。
- 關閉 image encryption、multi-image、serial recovery。
- 若超過大小預算，T-Flash update 可設為 build-time option，保留 UART YMODEM 作最小救援路徑。
- USB CDC Recovery 也必須設為 build-time option；若超過大小預算，優先保留 UART YMODEM。
- 保留本專案自訂 YMODEM，不啟用 MCUboot serial recovery。

---

## 12. 測試計畫

### Phase 1 -- MCUboot 最小 port

- [ ] 建立 `external/mcuboot` submodule 或固定 source drop。
- [ ] 建立 `bootloader_mcuboot` 專案與 linker script。
- [ ] 實作 HSI 64 MHz minimal boot clock，正常 path 不啟 USART/SDMMC/USB。
- [ ] 實作 BY25Q32 JEDEC ID、Fast Read、Page Program、Sector Erase。
- [ ] 實作 `flash_area_*` API。
- [ ] 編入 bootutil，讓 `boot_go()` 可掃描空 slot 並回報無 image。

### Phase 2 -- Signed RAM-load image

- [ ] 產生 ECDSA P-256 dev key。
- [ ] 產生 bootloader public key source。
- [ ] Application linker 改為 V1 AXI SRAM `0x24000000` load image。
- [ ] 確認 bootloader DTCMRAM runtime 使用量，並確認不使用 AXI SRAM heap。
- [ ] 實作 Backup SRAM 32-byte `BootExchange_t`，驗證 software reset 後內容保留。
- [ ] 驗證 Application 可在跳轉後重用 DTCMRAM stack/heap。
- [ ] 使用 `imgtool sign --load-addr 0x24000000 --pad` 產生 signed image。
- [ ] 手動燒入 Slot A。
- [ ] 驗證 MCUboot 可載入 AXI SRAM 並跳轉。

### Phase 2B -- Segmented RAM-load 評估（需要時）

- [ ] 確認 Application initialized load image 是否真的超過 512 KB。
- [ ] V2 初版固定 segment manifest 放在 signed payload 起點，不使用未簽章外部 metadata。
- [ ] 實作 ELF 解析工具，自動產生 `BootSegmentManifest_t` 與 segment blobs；linker map 只作 report/review 輔助。
- [ ] CI 檢查 `.itcm_vector` / `.itcm_isr` / `.itcm_text` / `.dtcm_data` 的 VMA、alignment、size 與工具輸出的 report 一致。
- [ ] Bootloader 驗證 signature 後解析 manifest，檢查 address range / overlap / alignment。
- [ ] Bootloader 依 segment 複製 AXI/ITCM/DTCM，並確認不覆蓋 Backup SRAM exchange block。
- [ ] 測試 corrupted manifest、越界 segment、overlap segment 都會拒絕啟動。

### Phase 3 -- Confirm / Revert

- [ ] Application 實作 `Image_ConfirmCurrent()`。
- [ ] 寫入新版本 Slot B，首次啟動不 confirm，確認下次 reboot 會回退。
- [ ] 寫入新版本 Slot B，啟動後 confirm，確認後續 reboot 維持新版本。
- [ ] 破壞 Slot B signature，確認 MCUboot 不啟動該 image。

### Phase 4 -- OTA / YMODEM

- [ ] TCP OTA 寫入 inactive slot。
- [ ] OTA 寫入中斷電，確認原 confirmed image 仍可啟動。
- [ ] KEY2 進入 Update Service，T-Flash 有 `/STM32H750_UPDATE.BIN` 時優先離線更新。
- [ ] T-Flash 無卡、掛載失敗、找不到指定檔名時 fallback 到 UART YMODEM。
- [ ] T-Flash 檔案非 MCUboot signed image 時拒絕寫入或不設定 test trailer。
- [ ] 若啟用 USB CDC Recovery，僅在 Update Service 啟用 PLL3Q 48 MHz 或 HSI48+CRS。
- [ ] YMODEM 接收 signed image 並設定 test trailer。
- [ ] 兩個 slot 都無效時進入 Update Service，先查 T-Flash，再 fallback YMODEM。

### Phase 5 -- 壓力與量產前檢查

- [ ] 連續 100 次 reboot 驗證 selected/confirmed 狀態穩定。
- [ ] SPI SCK 60 MHz 與 30 MHz A/B 比較，確認板級裕量。
- [ ] 正常 boot path 檢查：不得初始化 USART1 / SDMMC1 / USB OTG FS / ETH / FDCAN。
- [ ] Bootloader map 檢查，確認未超過 128 KB。
- [ ] Application map 檢查：V1 確認 image 不超過 AXI SRAM；V2 確認每個 segment 落在允許 RAM 範圍。
- [ ] 正常 boot path 不初始化 RAMECC、不啟用 RAMECC IRQ；V1 RAM-load runtime range check 或 V2 segment range check 必須通過才跳轉。
- [ ] dev key 換成 production key，確認 dev key 不進量產 bootloader。

---

## 13. 風險與待確認事項

| 風險 | 影響 | 對策 |
|------|------|------|
| MCUboot + crypto 超過 128 KB | Bootloader 放不下 | 精簡 feature、使用較小 crypto backend、目標先做 ECDSA P-256 |
| RAM-load image 超過 512 KB | 無法執行 | linker `ASSERT` + bootloader runtime range check；`imgtool --slot-size` 只檢查 slot，不等於 AXI SRAM 上限 |
| 手寫 trailer writer 出錯 | OTA 狀態損毀 | Application 盡量連結 bootutil API，不手刻 trailer layout |
| image version 未遞增 | 新 image 不被選中 | CI/打包工具強制版本遞增 |
| SPI NOR 斷電期間寫入 | inactive slot 損毀 | MCUboot 保留 confirmed image；OTA header/trailer 最後完成 |
| dev private key 外流 | 可簽署任意韌體 | dev key 僅開發使用，量產前更換並保護 private key |
| D-cache 對 RAM image 影響 | 跳轉後取指異常 | 初版 bootloader 可關 D-cache；若開啟須 clean/invalidate |
| T-Flash + FATFS 讓 bootloader 超過 128 KB | Bootloader 放不下 | T-Flash update 設為 build-time option；必要時保留 YMODEM-only minimal bootloader |
| T-Flash 掛載或讀檔 timeout | 進入 update mode 後卡住 | SDMMC/FATFS 初始化與讀檔設定 timeout，失敗 fallback YMODEM |
| T-Flash 檔案被替換 | 寫入惡意 image | 只接受 MCUboot signed image；完整 signature 由 MCUboot boot flow 驗證 |
| 正常 boot path 初始化太多外設 | 開機變慢、失敗面變大 | 正常 path 只開 GPIO/SPI1/CRC/crypto/Backup SRAM；USART/SDMMC/USB 只在 Update Service 初始化 |
| USB CDC clock 不準 | USB 枚舉失敗 | Recovery mode 使用 HSE→PLL3Q 48 MHz，或 HSI48+CRS；不要直接用 HSI64 供 USB |
| V1 浪費 ITCM/DTCM initialized 空間 | Application load image 仍被 512 KB 限制 | 先用 DTCM/ITCM 放 NOLOAD stack/heap/bss；需要時升級 V2 segmented loader |
| V2 segment manifest 未受簽章保護 | 惡意修改搬移目標 | V2 初版固定放在 signed payload 起點；protected TLV 只作未來擴展 |
| V2 segment overlap / 越界 | 覆蓋關鍵 RAM 或 exchange block | Bootloader 對每個 segment 做 range/overlap/alignment 檢查 |
| Bootloader 與 Application RAM ownership 不清 | Application 覆蓋 bootloader 工作區或浪費 DTCMRAM | 明確規定跳轉後除 Backup SRAM 32 bytes 外，其餘 RAM 由 Application 接管 |
| Backup SRAM 交換區被 Application 清零 | 跨重啟狀態遺失 | linker 保留 `0x38800000` ~ `0x3880001F`，startup 不初始化此區 |
| ITCM/DTCM section copy 漏做 | 關鍵函式或資料內容錯誤 | startup copy table 納入 `.itcm_vector` / `.itcm_isr` / `.itcm_text` / `.dtcm_data`，並加入 map 檢查 |
| ITCM runtime vector 切換太晚 | 中斷仍走 AXI vector，jitter 不符合工控需求 | 在任何 IRQ enable 前複製 `.itcm_vector` 並設定 `SCB->VTOR = 0x00000000` |
| 誤以為 RAMECC 是 ECC enable | Bootloader 初始化過度或留下 IRQ 狀態 | V1 不初始化 RAMECC、不啟 IRQ；依賴 always-on SRAM ECC，debug/recovery build 才 polling |
| MCUboot 版本升級造成 config/trailer 差異 | app confirm 失效 | 固定 MCUboot commit，升級時跑 confirm/revert 回歸測試 |

---

## 14. 官方參考

- MCUboot Design: https://docs.mcuboot.com/design.html
- MCUboot Porting how-to: https://docs.mcuboot.com/PORTING.html
- MCUboot imgtool: https://docs.mcuboot.com/imgtool.html
- Zephyr MCUboot image control API: https://docs.zephyrproject.org/latest/doxygen/html/group__mcuboot__api.html
- NuttX MCUboot port: https://nuttx.apache.org/docs/12.7.0/applications/boot/mcuboot/index.html
