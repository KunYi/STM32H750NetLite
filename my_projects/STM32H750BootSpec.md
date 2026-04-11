# STM32H750NetLite — Bootloader 與 OTA 實作規格書

**硬體平台：** STM32H750VBT6 + BY25Q32 SPI NOR Flash（4 MB）
**文件版本：** v0.4
**日期：** 2026-04-11
**狀態：** 規劃中
**共用狀態規格：** `STM32H750BootSharedStateSpec.md`
**低功耗模式規格：** `STM32H750LowPowerModeSpec.md`

---

## 目錄

1. [系統概覽](#1-系統概覽)
2. [時脈策略](#2-時脈策略)
3. [記憶體架構](#3-記憶體架構)
4. [SPI NOR Flash 分區規劃（A/B OTA）](#4-spi-nor-flash-分區規劃)
5. [Bootloader 規格](#5-bootloader-規格)
6. [Application 規格](#6-application-規格)
7. [OTA 更新機制](#7-ota-更新機制)
8. [韌體更新 Protocol](#8-韌體更新-protocol)
9. [Linker Script 設計](#9-linker-script-設計)
10. [建構系統設定](#10-建構系統設定)
11. [實作順序與里程碑](#11-實作順序與里程碑)
12. [風險與注意事項](#12-風險與注意事項)

---

## 1. 系統概覽

### 1.1 架構說明

本系統採用 **Bootloader + A/B Application Partition + OTA** 架構：

- **Bootloader** 永遠駐留於內部 Flash（128 KB），負責初始化、驗證、複製與跳轉，本身不透過 OTA 更新
- **SPI NOR Flash 4 MB** 劃分為兩個 Application slot（Slot A / Slot B，各 1 MB）以及 Metadata 與資料分區
- **Bootloader 讀取 Metadata** 決定 Active slot，複製至 AXI SRAM 後執行
- **OTA 更新** 由 Application 透過 LwIP 下載新 image，寫入非 Active slot，驗證後切換
- **回滾保護** 透過啟動計數機制，連續失敗自動切回前一個 slot
- **緊急更新** 透過 Update Service：優先檢查 T-Flash 指定檔案，失敗時再進入 USART1 + YMODEM，無論 Application 狀態如何皆可救援

### 1.2 設計原則

- **Bootloader 不可磚**：Bootloader 本身不透過 OTA 更新，確保系統永遠可救援
- **原子切換**：Active slot 由單一 Metadata sector 寫入決定，避免部分更新不一致
- **驗證後才切換**：新 image 寫入並通過 CRC32 驗證後，才修改 Metadata 指向新 slot
- **啟動計數保護**：每次啟動將 boot_count 加一，Application 啟動成功後歸零；連續失敗超過閾值自動回滾
- **雙 slot 保留**：A/B 兩個 slot 都保留完整 image，任何時候皆能切回
- **RAM ownership 明確**：Bootloader 的 Stack/Heap/Buffer 只在跳轉前有效；跳轉後除 Backup SRAM 交換區外，一般 SRAM 均交由 Application 重新配置
- **共用狀態模型**：Metadata、slot state、Backup SRAM exchange 與 OTA/rollback 流程以 `STM32H750BootSharedStateSpec.md` 為準

### 1.3 系統啟動流程

```
上電
 │
 ▼
Bootloader Minimal Init
（HSI 64MHz、GPIO、SPI1、CRC、Backup SRAM）
 │
 ├─── KEY2（PC13）長按 3 秒
 │      或 Backup SRAM recovery flag ──► Update Service
 │                                      │
 │                                      ├── 嘗試 T-Flash 指定檔案
 │                                      │     → 載入 firmware.img → 寫入 inactive slot
 │                                      │     → 驗證 CRC32 → 更新 Metadata → 重啟
 │                                      │
 │                                      └── 找不到 T-Flash/指定檔案/檔案無效
 │                                            → UART YMODEM
 │                                            → 接收 firmware.img → 寫入 inactive slot
 │                                            → 驗證 CRC32 → 更新 Metadata → 重啟
 │
 └─── 正常開機
          │
          ▼
     讀取 Metadata（NOR Flash 0x00_0000）
          │
     ├─── Metadata 損毀/無效 ──► 掃描 Slot A / B 選擇有效者
          │                              └─── 兩個都無效 ──► Update Service
          ▼
     選定 Active Slot（A 或 B）
     boot_count += 1，回寫 Metadata
          │
          ▼
     讀取 Slot Header → 驗證 Magic + CRC32
          │
     ├─── 驗證失敗 or boot_count > BOOT_TRIAL_MAX ──► 切換至 previous_slot 重試
          │                                     └─── 兩個都失敗 ──► Update Service
          ▼
     複製 Application Binary 至 AXI SRAM（0x2400_0000）
     LED 常亮提示複製中
          │
          ▼
     驗證 Binary CRC32
          │
          ├─── CRC 不符 ──► 標記 slot INVALID，嘗試另一個
          ▼
     DeInit 所有外設，RCC_DeInit（回到 HSI）
          │
          ▼
     SCB->VTOR = 0x2400_0000
     跳轉 Application Reset_Handler
```

---

## 2. 時脈策略

Bootloader 與 Application 使用不同時脈設定，各自針對需求最佳化。

### 2.1 Bootloader 時脈（Minimal Boot Path）

Bootloader 正常開機路徑以簡化與穩定為優先，預設只使用 HSI 64 MHz，不啟動 PLL1/PLL2/PLL3，也不初始化 USART、SDMMC、USB、ETH、FDCAN 等非必要外設。

| 項目 | 設定 | 數值 |
|------|------|------|
| CPU 時脈來源 | HSI | 64 MHz |
| PLL1 / PLL2 / PLL3 | 不啟用 | 正常 boot path 不使用 |
| SPI1 時脈來源 | HSI 或 kernel clock 可用來源 | 建議先用保守分頻 |
| SPI1 Prescaler | ÷2 或 ÷4 | SCK 約 32 MHz 或 16 MHz，依實測決定 |
| MCO1（PA8） | 不啟用 | LAN8720A 不在 Bootloader 階段使用 |

> BY25Q32ES Normal Read（`0x03`）最高支援 100 MHz，Fast Read（`0x0B`）最高支援 120 MHz；HSI 64 MHz 下的 16/32 MHz SPI 都在規格內。
> 若實測 copy 512 KB image 太慢，可在 Phase 2 啟用 PLL2P 120 MHz，將 SPI1 提升到 60 MHz；這是效能優化，不是正常開機必要條件。

### 2.2 Bootloader 高速 SPI 可選配置

若需要縮短 Application 複製時間，可在正常 boot path 中啟用 PLL2P 給 SPI1：

| 項目 | 設定 | 數值 |
|------|------|------|
| PLL2 來源 | HSE 25 MHz |  |
| PLL2 DIVM2 / DIVN2 / DIVP2 | ÷5 × 48 ÷2 | PLL2P = **120 MHz** |
| SPI1 時脈來源 | PLL2P | 120 MHz |
| SPI1 Prescaler | ÷2 | SCK = **60 MHz** |

此模式必須保留 30 MHz fallback，用於板級訊號完整性驗證或現場問題排查。

### 2.3 Fast Read 指令格式

```
CS low
→ 0x0B                              （指令 1 byte）
→ Addr[23:16], Addr[15:8], Addr[7:0] （位址 3 bytes）
→ 0x00                              （dummy 1 byte，必須）
→ 讀取 N bytes 資料
CS high
```

### 2.4 Update Service 時脈（延後初始化）

Update Service 才啟用額外週邊與 PLL：

| 功能 | 啟用時機 | Clock 建議 |
|------|----------|------------|
| UART YMODEM | T-Flash / USB CDC 不可用時 | USART1 可用 HSI 或 PLL3Q，115200 優先穩定 |
| T-Flash / SDMMC1 | 進入 Update Service 且需讀卡時 | 低速保守 SDMMC clock，穩定優先 |
| USB CDC Recovery | 進入 Update Service 且啟用 USB build option | HSE 25 MHz → PLL3Q 48 MHz，或 HSI48 + CRS |

正常開機路徑不得呼叫 `MX_SDMMC1_SD_Init()`、`MX_USB_OTG_FS_PCD_Init()`、`MX_USART1_UART_Init()` 等 recovery-only init。`bootloader.ioc` 可以包含這些外設設定，但 `main()` 必須依模式延後呼叫。

### 2.5 Application 時脈（完整功能）

Application 的 `SystemClock_Config()` 自行設定完整時脈，與 CubeMX `.ioc` 一致：

| 項目 | 數值 |
|------|------|
| CPU（PLL1P） | **480 MHz** |
| AXI / AHB | 240 MHz |
| APB1 / APB2 / APB3 / APB4 | 120 MHz |
| SPI1 來源（CKPER = HSE） | 25 MHz → SCK **12.5 MHz** |
| MCO1 輸出（PA8） | HSE 25 MHz → **LAN8720A REF_CLK** |
| FDCAN1 來源（PLL2Q） | 40 MHz |
| USART1 來源（PLL3Q） | 48 MHz |
| USB 來源（PLL3Q） | 48 MHz |
| SDMMC1 來源（PLL2R） | 48 MHz |

### 2.6 時脈切換流程

```
Bootloader 跳轉前：
  HAL_RCC_DeInit()     ← 重設所有 PLL，系統回到 HSI 64 MHz
  HAL_DeInit()         ← 重設所有外設
  SysTick disable

Application SystemClock_Config()：
  啟動 HSE → PLL1（480 MHz CPU）
  啟動 PLL2（FDCAN、SDMMC）
  啟動 PLL3（USART、USB）
  設定 MCO1 → PA8 輸出 HSE 25 MHz → LAN8720A
  ↑ 此後 LAN8720A 才有 REF_CLK

Application MX_ETH_Init()：
  HAL_Delay(1)         ← 等待 MCO1 與 LAN8720A PHY 穩定
  初始化 ETH MAC + PHY
```

---

## 3. 記憶體架構

### 3.1 記憶體區域總覽

| 區域 | 位址範圍 | 大小 | 用途 |
|------|----------|------|------|
| 內部 Flash | `0x0800_0000` ~ `0x0801_FFFF` | 128 KB | **Bootloader** |
| ITCMRAM | `0x0000_0000` ~ `0x0000_FFFF` | 64 KB | Application 可選：確定性 / 低延遲 code |
| DTCMRAM | `0x2000_0000` ~ `0x2001_FFFF` | 128 KB | Bootloader runtime 工作 RAM；跳轉後交還 Application |
| **AXI SRAM** | `0x2400_0000` ~ `0x2407_FFFF` | **512 KB** | **Application 主執行區 / RAM-load image** |
| RAM_D2 | `0x3000_0000` ~ `0x3004_7FFF` | 288 KB | Application DMA 緩衝區（ETH、SDMMC）與可選 heap |
| RAM_D3 | `0x3800_0000` ~ `0x3800_FFFF` | 64 KB | Application 可選低速資料 / 低功耗域暫存 |
| Backup SRAM | `0x3880_0000` ~ `0x3880_0FFF` | 4 KB | 前 32 bytes 作 Bootloader/Application 交換資訊 |
| SPI NOR Flash | 外部，SPI1 存取 | 4 MB | A/B Partition + Metadata + 資料 |

Bootloader 不應永久佔用一般 SRAM。除 Backup SRAM 前 32 bytes 交換區外，Bootloader 的 stack、heap、SPI buffer、YMODEM/T-Flash buffer、CRC buffer 都只在跳轉前有效；成功跳轉後，Application 可以重新配置 ITCMRAM、DTCMRAM、AXI SRAM、RAM_D2、RAM_D3。

### 3.2 Bootloader 記憶體配置

```
內部 Flash 128KB（0x0800_0000）
┌─────────────────────────────────┐ 0x0800_0000
│  Vector Table                   │ ~0.5 KB
├─────────────────────────────────┤
│  .text / .rodata                │
│  ・PLL2P 時脈初始化             │
│  ・SPI1 Flash 驅動              │
│  ・CRC32 硬體加速封裝           │
│  ・YMODEM 接收                  │
│  ・Metadata 讀寫                │
│  ・跳轉邏輯                     │
├─────────────────────────────────┤
│  （空餘，Bootloader 精簡優先）  │
└─────────────────────────────────┘ 0x0801_FFFF

DTCMRAM 128KB（0x2000_0000）
┌─────────────────────────────────┐ 0x2000_0000
│  .data / .bss                   │
│  Stack 8 KB                     │
│  YMODEM 接收緩衝 1 KB           │
│  SPI 操作暫存 256 B             │
│  Metadata 讀取緩衝 4 KB         │
│  T-Flash 讀取緩衝 4~8 KB        │  ← 僅 Update Service 使用
│  FATFS work area 2~8 KB         │  ← 可選
└─────────────────────────────────┘ 使用量遠小於 128 KB
```

Bootloader runtime RAM 預算：

| 項目 | 建議大小 | 位置 | 備註 |
|------|----------|------|------|
| Bootloader stack | 8 KB | DTCMRAM top | YMODEM、T-Flash、HAL driver 使用 |
| Bootloader `.data/.bss` | 8-16 KB | DTCMRAM | 實際以 map file 確認 |
| SPI page buffer | 256-512 B | DTCMRAM | Page Program / Fast Read 暫存 |
| Metadata buffer | 4 KB | DTCMRAM | 讀寫 metadata sector |
| YMODEM buffer | 1 KB | DTCMRAM | 128/1024 byte packet |
| T-Flash read buffer | 4-8 KB | DTCMRAM | Update Service 才配置/使用 |
| FATFS work area | 2-8 KB | DTCMRAM | 可選，若 Bootloader 支援 T-Flash update |
| AXI SRAM RAM-load image | 最多 512 KB | AXI SRAM | 由 Bootloader copy Application binary 後驗證 |

驗收條件：

- Bootloader linker map 必須列出 DTCMRAM 實際使用量。
- Bootloader 不使用 ITCMRAM，除非後續量測證明必要。
- Bootloader 不把 heap 設在 AXI SRAM，避免覆蓋待跳轉 Application image。
- Bootloader 只有進入 Update Service 時才初始化 SDMMC/FATFS；正常開機不掛載 T-Flash，避免拖慢 boot time。
- 跳轉前不需要顯式「release」RAM；只要 Bootloader 不再執行且 Application startup 重新初始化 `.data/.bss/heap/stack`，這些 RAM 即由 Application 接管。

### 3.3 Application 記憶體配置

```
AXI SRAM 512KB（0x2400_0000）
┌─────────────────────────────────┐ 0x2400_0000
│  Boot Vector Table              │  ← Bootloader 初始跳轉使用
├─────────────────────────────────┤
│  .text（程式碼）                │
│  .rodata（唯讀資料）            │
├─────────────────────────────────┤
│  .data（已初始化全域變數）      │
├─────────────────────────────────┤
│  .bss（未初始化全域變數）       │
├─────────────────────────────────┤
│  Heap（向上成長）               │
├─────────────────────────────────┤
│  Stack 8 KB（向下成長）         │
└─────────────────────────────────┘ 0x2407_FFFF

ITCMRAM 64KB（0x0000_0000，可選）
┌─────────────────────────────────┐
│  .itcm_vector                   │  ← 可選：runtime interrupt vector table
├─────────────────────────────────┤
│  .itcm_isr / .itcm_text         │  ← 低延遲 / 確定性 ISR/code
└─────────────────────────────────┘

DTCMRAM 128KB（0x2000_0000，可選）
┌─────────────────────────────────┐
│  .dtcm_data / .dtcm_bss         │  ← 重要資料、控制迴圈資料
│  Application heap / stack       │  ← 可由 Application 重新規劃
└─────────────────────────────────┘

RAM_D2 288KB（0x3000_0000）
┌─────────────────────────────────┐
│  ETH DMA 描述子（Rx + Tx）      │  ← section .dma_buffer
│  ETH DMA 資料緩衝區             │
│  SDMMC DMA 緩衝區               │
└─────────────────────────────────┘
```

> **重要**：ETH 和 SDMMC 的 DMA 只能存取 D2 domain SRAM。DMA 緩衝區放在 AXI SRAM 將導致傳輸失敗。
> **重要**：若使用 ITCM/DTCM 放 initialized code/data，本版單一 binary 載入模式仍要求 load image 放得進 AXI SRAM；若要真正突破 512 KB，需要增加 signed/CRC-protected segment manifest，讓 Bootloader 驗證後分段搬移 AXI/ITCM/DTCM。
> **重要**：若需工控等級中斷確定性，可把 runtime vector table 與關鍵 ISR 放入 ITCM。V1 建議由 Application early startup 從 AXI SRAM load image 複製 `.itcm_vector` / `.itcm_isr`，設定 `SCB->VTOR = 0x00000000` 後才啟用中斷；Bootloader 初始跳轉仍使用 AXI SRAM `0x24000000` 的 boot vector。

### 3.4 Backup SRAM 交換區

Backup SRAM 前 32 bytes 保留為 Bootloader 與 Application 的交換資訊。此區域可跨 software reset 保留，用於傳遞 boot reason、update result、recovery request 等小量狀態。

低功耗 Standby / VBAT 的持久 resume state 優先使用 RTC backup registers。Bootloader wakeup 後驗證 RTC backup registers 的 `RtcBackupResumeState_t`，或 Backup SRAM extended resume state，再把最後決定的 `boot_reason` 寫入 `BootExchange_t`。若 128-byte RTC backup registers 不足，才啟用 Backup SRAM retention；細節以 `STM32H750LowPowerModeSpec.md` 為準。

```c
#define BOOT_EXCHANGE_ADDR       0x38800000UL
#define BOOT_EXCHANGE_SIZE       32U
#define BOOT_EXCHANGE_MAGIC      0x42455843UL  // "BEXC"
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
- Application 啟動成功後可更新 `boot_slot`、`image_version`、`last_error = 0`。
- Application 要強制進入 Update Service 時，可設定 `flags`，再 `HAL_NVIC_SystemReset()`。
- 此 32-byte 區域不可放指標、RTOS handle、mutex 或大資料；其餘 Backup SRAM 先保留，後續可再分配。
- Standby / VBAT resume 不應直接改變 active slot 或 OTA metadata；Bootloader 只把它轉換為 `BOOT_REASON_STANDBY_RESUME` / `BOOT_REASON_VBAT_RETURN` 後正常驗證與載入 image。

---

## 4. SPI NOR Flash 分區規劃

BY25Q32 容量 4 MB，Sector 4 KB，Block 64 KB。

### 4.1 分區配置圖

```
SPI NOR Flash 4MB（BY25Q32）
┌─────────────────────────────────┐ 0x00_0000
│  Metadata（1 Sector = 4 KB）   │  Active slot、版本、boot_count
├─────────────────────────────────┤ 0x00_1000
│  Metadata 備份（1 Sector）     │  Metadata 損毀時的恢復來源
├─────────────────────────────────┤ 0x00_2000
│  保留（2 Sectors = 8 KB）      │
├─────────────────────────────────┤ 0x00_4000
│                                 │
│  Slot A（1 MB）                 │
│  ・App Header（4 KB）           │
│  ・App Binary（最大 ~1020 KB）  │
│                                 │
├─────────────────────────────────┤ 0x10_4000
│                                 │
│  Slot B（1 MB）                 │
│  ・App Header（4 KB）           │
│  ・App Binary（最大 ~1020 KB）  │
│                                 │
├─────────────────────────────────┤ 0x20_4000
│  未使用保留區（約 1.98 MB）    │
├─────────────────────────────────┤ 0x3C_0000
│  使用者資料區（256 KB）        │  設定、校正值、Log
└─────────────────────────────────┘ 0x3F_FFFF（4MB 上限）
```

### 4.2 分區位址常數

```c
#define FLASH_ADDR_METADATA       0x000000UL
#define FLASH_ADDR_METADATA_BAK   0x001000UL   // 備份

#define FLASH_ADDR_SLOT_A         0x004000UL   // Slot A 起始
#define FLASH_ADDR_SLOT_A_HEADER  0x004000UL
#define FLASH_ADDR_SLOT_A_BIN     0x005000UL   // Binary 起始（Header 之後）

#define FLASH_ADDR_SLOT_B         0x104000UL   // Slot B 起始
#define FLASH_ADDR_SLOT_B_HEADER  0x104000UL
#define FLASH_ADDR_SLOT_B_BIN     0x105000UL

#define FLASH_ADDR_USERDATA       0x3C0000UL

#define FLASH_SLOT_MAX_SIZE       (1024*1024)
#define FLASH_BIN_MAX_SIZE        (FLASH_SLOT_MAX_SIZE - 0x1000)  // ~1020 KB
#define SRAM_APP_MAX_SIZE         (512*1024)                       // V1 單一 AXI load image 上限

#define BOOT_EXCHANGE_ADDR        0x38800000UL
#define BOOT_EXCHANGE_SIZE        32U
```

### 4.3 Metadata 結構

```c
#define METADATA_MAGIC        0x4D455441UL   // "META"
#define BOOT_TRIAL_MAX        3    // 允許最多 3 次未確認啟動嘗試
#define BOOT_COUNT_MAX        BOOT_TRIAL_MAX
#define OTA_REQUEST_FLAG      0xA5A5A5A5UL

typedef uint8_t SlotID_t;
enum {
    SLOT_A = 0,
    SLOT_B = 1,
};

typedef uint8_t SlotState_t;
enum {
    SLOT_STATE_EMPTY   = 0xFF,  // 未寫入（Flash 預設）
    SLOT_STATE_VALID   = 0x01,  // 有效，可啟動
    SLOT_STATE_PENDING = 0x02,  // 新寫入，待首次驗證
    SLOT_STATE_WRITING = 0x03,  // Application OTA 寫入中，不可啟動
    SLOT_STATE_INVALID = 0x00,  // 損毀或驗證失敗
};

typedef struct __attribute__((packed)) {
    uint32_t    magic;              // 0x4D455441
    uint32_t    struct_version;     // 結構版本，目前為 2
    uint32_t    metadata_seq;       // 主/備份 metadata 選新用
    SlotID_t    active_slot;        // 目前 Active slot
    SlotID_t    previous_slot;      // 上次成功的 slot（回滾用）
    SlotState_t slot_state[2];      // [0]=Slot A, [1]=Slot B
    uint8_t     boot_count;         // 已嘗試啟動次數，App 成功後歸零
    uint8_t     reserved[3];        // 固定對齊到 32-bit 欄位，不依賴 enum ABI
    uint32_t    update_requested;   // OTA_REQUEST_FLAG 表示有待切換更新
    uint32_t    metadata_crc32;     // 對以上欄位的 CRC32
    uint8_t     padding[4068];      // 填滿至 4 KB
} FlashMetadata_t;                  // 總計 4096 bytes（1 sector）

_Static_assert(sizeof(FlashMetadata_t) == 4096,
               "FlashMetadata_t must be one 4KB sector");
```

Metadata 的 primary/backup 選擇、`WRITING` 狀態、`metadata_seq` 遞增與斷電恢復規則，以 `STM32H750BootSharedStateSpec.md` 為準。

### 4.4 App Header 結構

```c
#define APP_HEADER_MAGIC    0xDEADBEEFUL

typedef struct __attribute__((packed)) {
    uint32_t magic;           // 0xDEADBEEF
    uint32_t header_version;  // 目前為 1
    uint32_t fw_version;      // 版本（例：0x00010200 = v1.2.0）
    uint32_t binary_size;     // Binary 大小（bytes）
    uint32_t binary_crc32;    // Binary 的 CRC32
    uint32_t load_addr;       // 預期載入位址（必須為 0x24000000）
    uint32_t entry_offset;    // Reset_Handler 相對 binary 起始偏移
    uint32_t build_timestamp; // Unix timestamp
    uint8_t  git_hash[8];     // Git commit hash 前 8 字元
    uint8_t  reserved[212];   // 保留，填 0xFF
    uint32_t header_crc32;    // 對以上欄位的 CRC32
} AppHeader_t;                // 總計 256 bytes（Header 區 4 KB，其餘保留）

_Static_assert(sizeof(AppHeader_t) == 256,
               "AppHeader_t must be 256 bytes");
```

---

## 5. Bootloader 規格

### 5.1 初始化序列

```
1. VTOR = 0x0800_0000
2. BootClock_MinimalHSI64()：使用 HSI 64 MHz，不啟動 PLL1/PLL2/PLL3
3. GPIO 初始化：
   - PC13（KEY2，Input Pull-Up）
   - PC3（LED，Output，初始 Low）
4. SPI1 初始化：
   - PA4（CS，GPIO Output，Software NSS，初始 High）
   - PA5（SCK），PA6（MISO），PB5（MOSI）
   - Mode 0，Full-Duplex Master，Prescaler ÷2 或 ÷4 → 約 32 MHz 或 16 MHz
5. 硬體 CRC 控制器初始化
6. 讀取 BY25Q32 JEDEC ID，若不符則停止並等待
7. 啟用 backup domain / Backup SRAM 存取，讀取 BootExchange_t
8. delay 100 ms，偵測 KEY2 狀態與 recovery flag，決定啟動路徑
9. 正常 boot path：讀 metadata、驗證 slot、複製 Application、跳轉
10. Update Service path：才初始化 USART1 / SDMMC1 / USB CDC 與其所需 PLL
```

正常 boot path 不初始化 USART1、SDMMC1、USB OTG FS、ETH、FDCAN，也不初始化完整 RTC service；但可啟用 backup domain access 並用最小 helper 讀取 RTC backup registers / Backup SRAM handoff。若實測需要縮短複製時間，可在 `Boot_SPIClock_EnableHighSpeed()` 中啟用 PLL2P 120 MHz，把 SPI1 提升到 60 MHz，但必須保留 HSI 低速 fallback。

### 5.2 SPI Flash 驅動介面

```c
void     SPI_Flash_Init(void);
uint32_t SPI_Flash_ReadJEDEC(void);          // BY25Q32：0x68, 0x40, 0x16

/* Bootloader 讀取（正常 path 可用 HSI 低速；高速 path 可用 Fast Read 0x0B）*/
void     SPI_Flash_FastRead(uint32_t addr, void *buf, uint32_t len);

/* 寫入（需先 WriteEnable）*/
void     SPI_Flash_WriteEnable(void);
HAL_StatusTypeDef SPI_Flash_WritePage(uint32_t addr,
                                       const void *buf, uint32_t len); // max 256 B
HAL_StatusTypeDef SPI_Flash_EraseSector(uint32_t addr);  // 4 KB
HAL_StatusTypeDef SPI_Flash_EraseBlock(uint32_t addr);   // 64 KB

/* 狀態 */
void     SPI_Flash_WaitBusy(void);           // 輪詢 Status1 WIP bit
```

BY25Q32 關鍵指令表：

| 指令 | 代碼 | 最高 CLK | 說明 |
|------|------|----------|------|
| Fast Read | `0x0B` | 120 MHz | Bootloader 建議讀取方式，需 1 dummy byte |
| Read Data | `0x03` | 100 MHz | 一般 Read Data，60 MHz 仍符合 BY25Q32ES datasheet |
| Page Program | `0x02` | 120 MHz | 寫入，最多 256 B |
| Sector Erase | `0x20` | 120 MHz | 清除 4 KB |
| Block Erase | `0xD8` | 120 MHz | 清除 64 KB |
| Write Enable | `0x06` | 120 MHz | 每次寫入/抹除前必須執行 |
| Read Status 1 | `0x05` | 120 MHz | bit0 = WIP |
| Read JEDEC ID | `0x9F` | 120 MHz | 回傳 `0x68 0x40 0x16` |

### 5.3 驗證與複製流程

```c
bool Boot_TrySlot(SlotID_t slot)
{
    uint32_t hdr_addr = (slot == SLOT_A) ? FLASH_ADDR_SLOT_A_HEADER
                                         : FLASH_ADDR_SLOT_B_HEADER;
    uint32_t bin_addr = (slot == SLOT_A) ? FLASH_ADDR_SLOT_A_BIN
                                         : FLASH_ADDR_SLOT_B_BIN;
    AppHeader_t hdr;
    SPI_Flash_FastRead(hdr_addr, &hdr, sizeof(AppHeader_t));

    if (hdr.magic    != APP_HEADER_MAGIC) return false;
    if (hdr.load_addr != 0x24000000)      return false;
    if (hdr.binary_size > SRAM_APP_MAX_SIZE) return false;

    uint32_t hcrc = HW_CRC32(&hdr, sizeof(AppHeader_t) - 4);
    if (hcrc != hdr.header_crc32) return false;

    LED_On();
    SPI_Flash_FastRead(bin_addr, (void *)0x24000000, hdr.binary_size);
    LED_Off();

    uint32_t bcrc = HW_CRC32((void *)0x24000000, hdr.binary_size);
    return (bcrc == hdr.binary_crc32);
}
```

### 5.3.1 SRAM ECC / RAMECC Policy

STM32H750 的 SRAM ECC controller 由硬體 always enabled，提供 SECDED：single-bit error correction 與 double-bit error detection。RAMECC 是監控/診斷/中斷事件收集單元，不是 ECC correction enable switch。

V1 Bootloader policy：

- 不初始化 RAMECC peripheral。
- 不啟用 RAMECC interrupt。
- Bootloader 依賴 always-on SRAM ECC correction。
- Bootloader 從 SPI Flash 載入 AXI SRAM 後，必須從 AXI SRAM readback 並計算 CRC32。
- 若後續啟用 signature/hash，驗證對象也必須是 AXI SRAM 中即將執行的 image，不只驗證 SPI Flash 原始資料。

V2 / debug policy：

- 可加入 RAMECC polling，但仍不開 interrupt。
- 僅用於記錄 single-bit / double-bit ECC diagnostic event。
- 若發現 double-bit error，Bootloader 不得跳轉該 image，應進入 Update Service 或 reset/fault 策略。

### 5.4 跳轉實作

```c
void Boot_JumpToApplication(void)
{
    uint32_t *vectors   = (uint32_t *)0x24000000;
    uint32_t  app_sp    = vectors[0];
    uint32_t  app_entry = vectors[1];

    bool sp_in_dtcm = (app_sp >= 0x20000000UL && app_sp <= 0x20020000UL);
    bool sp_in_axi  = (app_sp >= 0x24000000UL && app_sp <= 0x24080000UL);
    if (!sp_in_dtcm && !sp_in_axi) return;

    if (app_entry < 0x24000000UL || app_entry >= 0x24080000UL) return;

    __disable_irq();

    HAL_RCC_DeInit();          // 重設時脈回 HSI
    HAL_DeInit();              // 重設外設
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    SCB->VTOR = 0x24000000;
    __DSB(); __ISB();

    __set_MSP(app_sp);
    ((void (*)(void))app_entry)();
    while (1);
}
```

---

## 6. Application 規格

### 6.1 啟動後必要動作

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();     // 480 MHz + PLL2 + PLL3 + MCO1(PA8)
    // MCO1 穩定後 LAN8720A 才有 REF_CLK
    MX_GPIO_Init();
    MX_ETH_Init();            // 內部加 1ms delay 等 PHY 穩定
    MX_SPI1_Init();           // CKPER 25MHz，SCK 12.5 MHz
    MX_FDCAN1_Init();
    MX_USART1_UART_Init();
    MX_SDMMC1_SD_Init();
    MX_USB_OTG_FS_PCD_Init();
    MX_RTC_Init();

    Boot_NotifySuccess();     // 清除 boot_count，確認本次啟動有效

    // 進入應用程式邏輯...
}
```

### 6.2 啟動成功通知

```c
void Boot_NotifySuccess(void)
{
    FlashMetadata_t meta;
    if (!Meta_Read(&meta)) return;
    if (meta.boot_count == 0) return;

    meta.boot_count = 0;
    meta.slot_state[meta.active_slot] = SLOT_STATE_VALID;
    Meta_Write(&meta);
    Meta_WriteBackup(&meta);  // 同步更新備份
}
```

### 6.3 DMA 緩衝區配置

```c
// ethernetif.c 或 dma_buffers.c
__attribute__((section(".dma_buffer")))
ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT];

__attribute__((section(".dma_buffer")))
ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT];

__attribute__((section(".dma_buffer")))
uint8_t Rx_Buff[ETH_RX_DESC_CNT][ETH_MAX_PACKET_SIZE];

__attribute__((section(".dma_buffer")))
uint8_t Tx_Buff[ETH_TX_DESC_CNT][ETH_MAX_PACKET_SIZE];
```

---

## 7. OTA 更新機制

### 7.1 Slot 狀態機

```
EMPTY ──── OTA 開始寫入 ────► WRITING ──── CRC/Header 驗證成功 ────► PENDING
                                      │                                 │
                         斷電/驗證失敗│                    Bootloader 驗證成功
                                      ▼                                 │
                                   INVALID                              ▼
                     Bootloader 驗證成功
                     App boot_count 歸零
                                  │
                                  ▼
INVALID ◄─ 驗證失敗/boot_count>3 ─ VALID ◄─── 正常執行
```

完整狀態轉換以 `STM32H750BootSharedStateSpec.md` 為準。

### 7.2 OTA 流程

```
Application 執行中（透過 LwIP TCP 接收）
 │
 ▼
確認 Inactive slot（非 Active 那個）
 │
 ▼
Metadata 標記：
  slot_state[new_slot] = WRITING
  active_slot / previous_slot 不變
 │
 ▼
抹除 Inactive slot 所有 sector（含 Header）
 │
 ▼
逐頁接收並寫入（256 B / 頁），同時計算 CRC32
 │
 ▼
比對 CRC32
 │
 ├─── 不符 ──► 標記 INVALID，回報錯誤，中止
 │
 └─── 正確
          │
          ▼
     寫入新 Slot Header（最後才寫，確保原子性）
          │
          ▼
     更新 Metadata：
       slot_state[new_slot] = PENDING
       previous_slot = active_slot
       active_slot   = new_slot
       boot_count    = 0
       update_requested = OTA_REQUEST_FLAG
     同步寫入 Metadata 備份
          │
          ▼
     回報成功，等待使用者確認重啟
          │
          ▼
     HAL_NVIC_SystemReset()
          │
          ▼
     Bootloader 啟動新 slot，驗證後清除 boot_count
```

### 7.3 回滾機制

| 觸發條件 | 時機 |
|----------|------|
| Header Magic 不符 | 每次 Bootloader 啟動時 |
| Header CRC32 不符 | 每次 Bootloader 啟動時 |
| Binary CRC32 不符 | 複製至 SRAM 後 |
| `boot_count > BOOT_TRIAL_MAX` | 每次啟動前遞增，App 成功後歸零；允許最多 `BOOT_TRIAL_MAX` 次未確認啟動嘗試 |
| `slot_state == INVALID` | Metadata 中已標記 |

回滾邏輯：

```c
meta.boot_count++;
Meta_Write(&meta);

if (meta.boot_count > BOOT_TRIAL_MAX ||
    !Boot_TrySlot(meta.active_slot)) {
    SlotID_t fallback = meta.previous_slot;
    if (fallback != meta.active_slot && Boot_TrySlot(fallback)) {
        meta.active_slot = fallback;
        meta.boot_count  = 0;
        meta.slot_state[fallback] = SLOT_STATE_VALID;
        Meta_Write(&meta);
        Boot_JumpToApplication();
    }
    enter_ymodem_recovery();
}
```

### 7.4 OTA 傳輸封包格式（TCP）

```
┌──────────────────────────────────┐
│  Magic         4 bytes  0x4F544155 ("OTAU")
│  FW Version    4 bytes
│  Binary Size   4 bytes
│  Binary CRC32  4 bytes
│  Reserved      16 bytes
├──────────────────────────────────┤
│  Binary Data   N bytes
└──────────────────────────────────┘
```

---

## 8. 韌體更新 Protocol

### 8.1 Bootloader Update Service

觸發條件：

- KEY2 長按 3 秒上電。
- Application 在 Backup SRAM `BootExchange_t.flags` 設定 update/recovery request 後 software reset。
- Metadata 損毀且兩個 Slot 都無效。
- Active slot / fallback slot 都驗證失敗。

Update Service 優先順序：

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
     - /STM32H750_UPDATE.IMG
     - /stm32h750_update.img
          │
          ├── 找不到指定檔案 ──► 進入 UART YMODEM Recovery
          │
          ▼
     讀取 App Header → 驗證 Magic + Header CRC32 + Binary Size
          │
          ├── 無效 ──► 進入 UART YMODEM Recovery
          │
          ▼
     寫入 inactive slot
     驗證 Binary CRC32
     更新 Metadata / Backup Metadata
     可選：將檔案改名為 *.DONE 或 *.BAD
     reboot
```

T-Flash 檔案規則：

- 檔案格式沿用本規格 `mkfirmware.py` 產生的 image：4 KB App Header + Binary。
- 檔名初版固定為 `/STM32H750_UPDATE.IMG`；為相容 FAT 大小寫，也接受 `/stm32h750_update.img`。
- 檔案大小必須 `>= 0x1000 + binary_size` 且 `<= FLASH_SLOT_MAX_SIZE`。
- Bootloader 必須驗證 App Header magic、header CRC32、`load_addr == 0x24000000`、`binary_size <= SRAM_APP_MAX_SIZE`、binary CRC32。
- 若檔案無效，不應覆蓋目前有效 slot；最多只寫 inactive slot，且失敗時不切換 `active_slot`。
- 若更新成功，可把檔案改名為 `/STM32H750_UPDATE.DONE`；若寫入或檢查失敗，可改名為 `/STM32H750_UPDATE.BAD`。若 FATFS rename 失敗，不影響安全性。

Bootloader SDMMC/FATFS 約束：

- 正常開機路徑不初始化 SDMMC/FATFS，只在 Update Service 中使用。
- SDMMC clock 可採低速保守設定，穩定優先，不追求最高讀取速度。
- T-Flash read buffer 使用 DTCMRAM 4-8 KB；不得放在 AXI SRAM，以免覆蓋 RAM-load image。
- FATFS 只需 read-only + rename 能力；若要最小化 Bootloader，可先不做 rename。
- T-Flash update 失敗時 fallback 到 UART YMODEM，不直接進入正常 boot，避免使用者以為離線更新已執行。

### 8.2 UART YMODEM 緊急更新（USART1 115200）

YMODEM 是 Update Service 的 fallback 路徑：T-Flash 無卡、掛載失敗、找不到指定檔案或檔案無效時進入。

```
Bootloader 輸出：
  "STM32H750NetLite Bootloader vX.X"
  "Update Service"
  "T-Flash update not available"
  "YMODEM Recovery Mode - send firmware.img"

每 1 秒發送 'C'，等待 PC 端開始傳送

PC 端：
  TeraTerm  → File → Transfer → YMODEM → Send
  minicom   → Ctrl-A S → ymodem → 選擇 .img
  命令列    → sb --ymodem firmware.img

Bootloader 邊接收邊寫入 Inactive slot
完成後驗證 CRC32 → 更新 Metadata → 重啟
```

### 8.3 Slot 寫入選擇邏輯

| 情況 | 寫入目標 |
|------|----------|
| 兩個 slot 都有效 | Inactive slot（非 Active） |
| 只有一個有效 | 另一個（Empty）slot |
| 兩個都無效 | Slot A，並設為 Active |

---

## 9. Linker Script 設計

### 9.1 Bootloader（`bootloader.ld`）

```ld
MEMORY
{
    FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
    DTCMRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text       : { *(.text*) *(.rodata*) } > FLASH
    .data       : { *(.data*) } > DTCMRAM AT> FLASH
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > DTCMRAM
    ._stack (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + 0x2000;   /* 8 KB */
        _estack = .;
    } > DTCMRAM
}
```

### 9.2 Application（`application.ld`）

```ld
MEMORY
{
    ITCM    (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM    (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    AXIRAM  (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
    RAM_D2  (rw)  : ORIGIN = 0x30000000, LENGTH = 288K
    BKPSRAM (rw)  : ORIGIN = 0x38800020, LENGTH = 4064
}

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > AXIRAM
    .text       : { *(.text*) *(.rodata*) } > AXIRAM
    .data       : { *(.data*) } > AXIRAM
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > AXIRAM

    /* 可選：runtime vector table，Application early startup 複製後改 VTOR 到 ITCM */
    .itcm_vector :
    {
        . = ALIGN(1024);
        _sitcm_vector = .;
        KEEP(*(.itcm_vector))
        . = ALIGN(1024);
        _eitcm_vector = .;
    } > ITCM AT> AXIRAM
    _sitcm_vector_load = LOADADDR(.itcm_vector);

    /* 可選：低延遲 / 確定性 code，由 startup 從 AXIRAM load image 複製 */
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

    /* 可選：重要資料 / 控制迴圈資料，由 startup 從 AXIRAM load image 複製 */
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

    /* DMA 緩衝區強制放入 RAM_D2 */
    .dma_buffer (NOLOAD) : { *(.dma_buffer) } > RAM_D2

    /* 避開 Backup SRAM 前 32 bytes BootExchange_t */
    .bkpsram (NOLOAD) : { *(.bkpsram*) } > BKPSRAM

    ._stack (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + 0x2000;   /* 8 KB */
        _estack = .;
    } > DTCM

    __image_end__ = LOADADDR(.dtcm_data) + SIZEOF(.dtcm_data);
    ASSERT(__image_end__ <= ORIGIN(AXIRAM) + LENGTH(AXIRAM),
           "Application load image exceeds AXI SRAM");
}
```

若使用 `.itcm_vector` / `.itcm_isr` / `.itcm_text` / `.dtcm_data`，startup 必須在 `main()` 前從 `*_load` 複製到對應 VMA，並清除 `.dtcm_bss`。若啟用 `.itcm_vector`，必須在複製完成後、任何 IRQ enable 前設定 `SCB->VTOR = 0x00000000`，並執行 `__DSB(); __ISB();`。`BKPSRAM` 起點從 `0x38800020` 開始，避免覆蓋前 32-byte boot exchange block。若未來要讓 initialized code/data 超過 AXI SRAM 512 KB，需新增受 CRC/簽章保護的 segment manifest，由 Bootloader 驗證後分段搬移。

---

## 10. 建構系統設定

### 10.1 目錄結構

```
my_projects/
├── STM32H750NetLite.ioc
├── bootloader/
│   ├── CMakeLists.txt
│   ├── bootloader.ld
│   └── Core/Src/
│       ├── main.c
│       ├── clock.c          # HSI64 minimal clock + optional PLL2/PLL3 recovery clocks
│       ├── spi_flash.c      # BY25Q32 驅動（HSI 低速，optional Fast Read 60MHz）
│       ├── metadata.c       # Metadata 讀寫（含備份）
│       ├── boot_exchange.c  # Backup SRAM 32B 交換區
│       ├── tflash_update.c  # T-Flash 指定檔案離線更新（可選）
│       ├── fatfs_port.c     # FATFS / SDMMC glue（可選）
│       ├── usb_cdc_update.c # USB CDC recovery（可選）
│       ├── ymodem.c         # YMODEM 接收
│       ├── crc32.c          # 硬體 CRC32 封裝
│       └── boot_jump.c      # 驗證、複製、跳轉
├── application/
│   ├── CMakeLists.txt
│   ├── application.ld
│   └── Core/Src/
│       ├── main.c
│       ├── ota.c            # OTA TCP 接收與寫入
│       ├── spi_flash.c      # BY25Q32 驅動（Read Data / Fast Read，依 Application clock）
│       ├── boot_exchange.c  # Backup SRAM 交換區更新
│       └── metadata.c       # Metadata 讀寫（共用結構）
└── tools/
    ├── mkfirmware.py        # .bin → 含 Header 的 OTA image
    └── ota_send.py          # TCP OTA 上傳工具
```

### 10.2 CMake 關鍵設定

```cmake
# 共用工具鏈設定
set(CPU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")

# Bootloader：優先縮體積
target_compile_options(bootloader PRIVATE
    ${CPU_FLAGS} -Os -ffunction-sections -fdata-sections -Wall)
target_link_options(bootloader PRIVATE
    ${CPU_FLAGS} -Tbootloader.ld -Wl,--gc-sections
    --specs=nano.specs --specs=nosys.specs
    -Wl,-Map=bootloader.map)

# Application：優先效能
target_compile_options(application PRIVATE
    ${CPU_FLAGS} -O2 -ffunction-sections -fdata-sections -Wall)
target_link_options(application PRIVATE
    ${CPU_FLAGS} -Tapplication.ld -Wl,--gc-sections
    --specs=nano.specs --specs=nosys.specs
    -Wl,-Map=application.map
    -Wl,--defsym=__FLASH_BIN_MAX__=0xFF000
    -Wl,--check-sections)
```

### 10.3 Firmware 打包工具（`tools/mkfirmware.py`）

```python
#!/usr/bin/env python3
"""
用法：python3 mkfirmware.py input.bin output.img --version 0x00010000
"""
import struct, zlib, sys, time, argparse

APP_HEADER_MAGIC = 0xDEADBEEF
LOAD_ADDR        = 0x24000000

def make_image(bin_path, out_path, version, git_hash=b"00000000"):
    binary = open(bin_path, "rb").read()
    crc32  = zlib.crc32(binary) & 0xFFFFFFFF
    ts     = int(time.time())

    # 256 bytes header，最後 4 bytes 為 header_crc32
    header_body = struct.pack(
        "<IIIIIIII8s216x",
        APP_HEADER_MAGIC, 1, version,
        len(binary), crc32,
        LOAD_ADDR, 0, ts, git_hash[:8],
    )
    header_crc = struct.pack("<I", zlib.crc32(header_body) & 0xFFFFFFFF)

    with open(out_path, "wb") as f:
        # Header 區 4 KB（header 256B + 補 0xFF 至 4 KB）
        f.write(header_body + header_crc)
        f.write(b"\xFF" * (0x1000 - 256))
        # Binary
        f.write(binary)

    print(f"[OK] {out_path}")
    print(f"     binary={len(binary)} bytes  crc32={crc32:#010x}  version={version:#010x}")

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("input");  p.add_argument("output")
    p.add_argument("--version", type=lambda x: int(x, 0), default=0x00010000)
    p.add_argument("--git-hash", default="00000000")
    args = p.parse_args()
    make_image(args.input, args.output, args.version, args.git_hash.encode())
```

---

## 11. 實作順序與里程碑

### Phase 1 — 基礎 SPI 驗證

- [ ] 建立 Bootloader CMake 專案框架
- [ ] 實作 HSI 64 MHz minimal boot clock
- [ ] 實作 SPI1 驅動（HSI 低速 16/32 MHz，Read Data 或 Fast Read）
- [ ] 驗證 BY25Q32 JEDEC ID（`0x68 0x40 0x16`）
- [ ] 驗證 Sector Erase + Page Program + Fast Read 正確性
- [ ] 實作硬體 CRC32 封裝

### Phase 2 — Bootloader 核心

- [ ] 實作 Metadata 讀寫（含 CRC32 保護與備份）
- [ ] 實作 Backup SRAM 32-byte `BootExchange_t`
- [ ] 實作 App Header 驗證
- [ ] 實作 Binary 複製至 AXI SRAM + CRC32 驗證
- [ ] 實作 `Boot_JumpToApplication()`（含時脈重設）
- [ ] 確認 Bootloader DTCMRAM runtime 使用量，並確認不使用 AXI SRAM heap
- [ ] 手動燒入測試 image，驗證完整啟動流程

### Phase 3 — Update Service 緊急更新

- [ ] 實作 T-Flash 指定檔案檢查（`/STM32H750_UPDATE.IMG`）
- [ ] 實作 T-Flash → 寫入 SPI Flash → 驗證 → Metadata 更新 → 重啟
- [ ] 實作 YMODEM 接收（USART1 115200）
- [ ] T-Flash 無卡、掛載失敗、找不到指定檔案時 fallback 到 UART YMODEM
- [ ] 若啟用 USB CDC Recovery，僅在 Update Service 啟用 PLL3Q 48 MHz 或 HSI48+CRS
- [ ] 驗證 KEY2 觸發 Update Service
- [ ] 驗證兩個 slot 失敗後進入 Update Service

### Phase 4 — Application 基礎

- [ ] 建立 Application CMake 專案，Linker Script 固定 AXI SRAM 起點
- [ ] 驗證 Application 可在跳轉後重用 DTCMRAM stack/heap
- [ ] 若使用 ITCM/DTCM section，實作 startup copy / clear
- [ ] 確認 SystemClock_Config（480 MHz + MCO1 25 MHz）
- [ ] 確認 DMA 緩衝區在 RAM_D2
- [ ] 實作 `Boot_NotifySuccess()`（boot_count 歸零）
- [ ] 驗證 LED、USART1 printf、ETH Link

### Phase 5 — OTA 機制

- [ ] 實作 Application 端 OTA TCP 接收
- [ ] 實作 Inactive slot 抹除 + 寫入 + 驗證
- [ ] 實作 Metadata 切換（PENDING → 重啟 → VALID）
- [ ] 驗證 A→B 正常切換
- [ ] 驗證 B→A 回滾（人工製造失敗場景）
- [ ] 實作 `mkfirmware.py` 與 `ota_send.py`
- [ ] 端對端 OTA 測試

### Phase 6 — 強化與壓力測試

- [ ] boot_count 連續失敗 → 自動回滾測試
- [ ] OTA 寫到一半斷電恢復測試
- [ ] Metadata sector 損毀 → 備份恢復測試
- [ ] Backup SRAM 32-byte 交換區 software reset 保留測試
- [ ] T-Flash update 檔案錯誤 / CRC 錯誤 / rename 失敗測試
- [ ] 正常 boot path 檢查：不得初始化 USART1 / SDMMC1 / USB OTG FS / ETH / FDCAN
- [ ] 若啟用高速 SPI，驗證 PLL2P 60 MHz 與 HSI 低速 fallback 皆可開機
- [ ] 正常 boot path 不初始化 RAMECC、不啟用 RAMECC IRQ；AXI SRAM readback CRC32 必須通過才跳轉
- [ ] Bootloader 大小確認（目標 < 48 KB，保留空間）
- [ ] Application slot 大小確認（需 < 1 MB），AXI load image 初版需 < 512 KB
- [ ] 加入 Bootloader 版本號與 USART1 啟動訊息

---

## 12. 風險與注意事項

| 風險 | 影響 | 對策 |
|------|------|------|
| Bootloader 超過 128 KB | 無法燒錄 | `-Os`、`newlib-nano`、精簡 HAL；T-Flash/FATFS 可設為 build-time option |
| Application 超過 1 MB | 超出 Slot 大小 | Linker `ASSERT` 檢查，`mkfirmware.py` 也驗證 |
| Application AXI load image 超過 512 KB | 本版單一 AXI 載入模式無法執行 | 先將 heap/stack/bss 放 DTCM/ITCM NOLOAD；若 initialized code/data 仍超過，新增 CRC/簽章保護 segment manifest |
| Metadata sector 損毀 | 無法選擇 slot | 雙份 Metadata（主 + 備份），掃描回退機制 |
| OTA 斷電（寫 Binary 中途） | Binary CRC32 不符 | Header 最後才寫，CRC 不符視為 EMPTY |
| OTA 斷電（更新 Metadata 前） | 舊 slot 仍 Active，重啟用舊版 | 安全，舊版仍可正常運作 |
| BY25Q32 JEDEC ID 讀取錯誤 | SPI 時序問題 | Bootloader 啟動強制讀取 ID，不符則停止所有操作 |
| LAN8720A REF_CLK 未穩定 | ETH Link 失敗 | MCO1 初始化後強制 delay 1 ms 再初始化 ETH |
| DMA 緩衝區在 AXI SRAM | ETH/SDMMC 失敗 | Linker `.dma_buffer` section 強制 RAM_D2 |
| boot_count 未歸零 | 正常 App 被誤判連續失敗 | `Boot_NotifySuccess()` 在 App 初始化完成後立即呼叫 |
| Bootloader SPI 60 MHz 不穩定 | 複製資料損毀 | PCB 佈線問題，可降回 30 MHz（Prescaler ÷4）驗證 |
| T-Flash + FATFS 讓 Bootloader 變大 | Bootloader 放不下 | T-Flash update 設為可選功能；最小救援路徑保留 UART YMODEM |
| T-Flash 掛載或讀檔 timeout | 進入 Update Service 後卡住 | SDMMC/FATFS 初始化與讀檔設定 timeout，失敗 fallback YMODEM |
| T-Flash 檔案被替換或損毀 | 錯誤 image 被寫入 slot | 驗證 App Header CRC32 與 Binary CRC32；正式產品建議升級簽章驗證 |
| 正常 boot path 初始化太多外設 | 開機變慢、失敗面變大 | 正常 path 只開 GPIO/SPI1/CRC/Backup SRAM；USART/SDMMC/USB 只在 Update Service 初始化 |
| USB CDC clock 不準 | USB 枚舉失敗 | Recovery mode 使用 HSE→PLL3Q 48 MHz，或 HSI48+CRS；不要直接用 HSI64 供 USB |
| Backup SRAM 交換區被 Application 清零 | 跨重啟狀態遺失 | linker 保留 `0x38800000` ~ `0x3880001F`，startup 不初始化此區 |
| Bootloader 與 Application RAM ownership 不清 | Application 覆蓋 Bootloader 工作區或浪費 DTCMRAM | 明確規定跳轉後除 Backup SRAM 32 bytes 外，其餘 RAM 由 Application 接管 |
| ITCM/DTCM section copy 漏做 | 關鍵函式或資料內容錯誤 | startup copy table 納入 `.itcm_text` / `.dtcm_data`，並加入 map 檢查 |
| ITCM runtime vector 切換太晚 | 中斷仍走 AXI vector，jitter 不符合工控需求 | 在任何 IRQ enable 前複製 `.itcm_vector` 並設定 `SCB->VTOR = 0x00000000` |
| 誤以為 RAMECC 是 ECC enable | Bootloader 初始化過度或留下 IRQ 狀態 | V1 不初始化 RAMECC、不啟 IRQ；依賴 always-on SRAM ECC，並對 AXI SRAM image 做 readback CRC32 |

---

*本文件隨實作進度持續更新。各 Phase 完成後補充實際測試結果與調整記錄。*
