# STM32H750NetLite — Bootloader 與 OTA 實作規格書

**硬體平台：** STM32H750VBT6 + BY25Q32 SPI NOR Flash（4 MB）
**文件版本：** v0.3
**日期：** 2026-04-10
**狀態：** 規劃中

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
- **緊急更新** 透過 USART1 + YMODEM，無論 Application 狀態如何皆可救援

### 1.2 設計原則

- **Bootloader 不可磚**：Bootloader 本身不透過 OTA 更新，確保系統永遠可救援
- **原子切換**：Active slot 由單一 Metadata sector 寫入決定，避免部分更新不一致
- **驗證後才切換**：新 image 寫入並通過 CRC32 驗證後，才修改 Metadata 指向新 slot
- **啟動計數保護**：每次啟動將 boot_count 加一，Application 啟動成功後歸零；連續失敗超過閾值自動回滾
- **雙 slot 保留**：A/B 兩個 slot 都保留完整 image，任何時候皆能切回

### 1.3 系統啟動流程

```
上電
 │
 ▼
Bootloader 初始化
（PLL2P 120MHz 給 SPI1 60MHz、USART1 115200、GPIO）
 │
 ├─── KEY2（PC13）長按 3 秒 ──► 強制 YMODEM 更新模式（USART1）
 │
 └─── 正常開機
          │
          ▼
     讀取 Metadata（NOR Flash 0x00_0000）
          │
          ├─── Metadata 損毀/無效 ──► 掃描 Slot A / B 選擇有效者
          │                              └─── 兩個都無效 ──► YMODEM 等待
          ▼
     選定 Active Slot（A 或 B）
     boot_count += 1，回寫 Metadata
          │
          ▼
     讀取 Slot Header → 驗證 Magic + CRC32
          │
          ├─── 驗證失敗 or boot_count > 3 ──► 切換至 previous_slot 重試
          │                                     └─── 兩個都失敗 ──► YMODEM 等待
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

### 2.1 Bootloader 時脈（高速 SPI 複製用）

Bootloader 的主要工作是盡快把 Application 從 SPI Flash 複製到 SRAM，SPI1 使用 PLL2P 提供高速時脈。CPU 本身只需 HSI 64 MHz 即可，節省 PLL1 啟動時間。

| 項目 | 設定 | 數值 |
|------|------|------|
| CPU 時脈來源 | HSI | 64 MHz |
| PLL2 來源 | HSE 25 MHz |  |
| PLL2 DIVM2 / DIVN2 / DIVP2 | ÷5 × 48 ÷2 | PLL2P = **120 MHz** |
| SPI1 時脈來源 | PLL2P | 120 MHz |
| SPI1 Prescaler | ÷2 | SCK = **60 MHz** |
| MCO1（PA8） | 不啟用 | LAN8720A 不在 Bootloader 階段使用 |

> BY25Q32 Fast Read（`0x0B`）最高支援 120 MHz，60 MHz 完全在規格內。
> Bootloader **必須使用 Fast Read 指令**，不可用 Read Data（`0x03`，上限 50 MHz）。

### 2.2 Fast Read 指令格式

```
CS low
→ 0x0B                              （指令 1 byte）
→ Addr[23:16], Addr[15:8], Addr[7:0] （位址 3 bytes）
→ 0x00                              （dummy 1 byte，必須）
→ 讀取 N bytes 資料
CS high
```

### 2.3 Application 時脈（完整功能）

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

### 2.4 時脈切換流程

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
| ITCMRAM | `0x0000_0000` ~ `0x0000_FFFF` | 64 KB | 保留（可選用） |
| DTCMRAM | `0x2000_0000` ~ `0x2001_FFFF` | 128 KB | Bootloader stack / data / buffer |
| **AXI SRAM** | `0x2400_0000` ~ `0x2407_FFFF` | **512 KB** | **Application 執行區** |
| RAM_D2 | `0x3000_0000` ~ `0x3004_7FFF` | 288 KB | DMA 緩衝區（ETH、SDMMC） |
| RAM_D3 | `0x3800_0000` ~ `0x3800_FFFF` | 64 KB | 低功耗域暫存 |
| SPI NOR Flash | 外部，SPI1 存取 | 4 MB | A/B Partition + Metadata + 資料 |

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
│  Stack 4 KB                     │
│  YMODEM 接收緩衝 1 KB           │
│  SPI 操作暫存 256 B             │
│  Metadata 讀取緩衝 4 KB         │
└─────────────────────────────────┘ 使用量遠小於 128 KB
```

### 3.3 Application 記憶體配置

```
AXI SRAM 512KB（0x2400_0000）
┌─────────────────────────────────┐ 0x2400_0000
│  Vector Table（從 Flash 複製）  │ ~0.5 KB
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

RAM_D2 288KB（0x3000_0000）
┌─────────────────────────────────┐
│  ETH DMA 描述子（Rx + Tx）      │  ← section .dma_buffer
│  ETH DMA 資料緩衝區             │
│  SDMMC DMA 緩衝區               │
└─────────────────────────────────┘
```

> **重要**：ETH 和 SDMMC 的 DMA 只能存取 D2 domain SRAM。DMA 緩衝區放在 AXI SRAM 將導致傳輸失敗。

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
#define SRAM_APP_MAX_SIZE         (512*1024)                       // 512 KB 上限
```

### 4.3 Metadata 結構

```c
#define METADATA_MAGIC        0x4D455441UL   // "META"
#define BOOT_COUNT_MAX        3
#define OTA_REQUEST_FLAG      0xA5A5A5A5UL

typedef enum { SLOT_A = 0, SLOT_B = 1 } SlotID_t;

typedef enum {
    SLOT_STATE_EMPTY   = 0xFF,  // 未寫入（Flash 預設）
    SLOT_STATE_VALID   = 0x01,  // 有效，可啟動
    SLOT_STATE_PENDING = 0x02,  // 新寫入，待首次驗證
    SLOT_STATE_INVALID = 0x00,  // 損毀或驗證失敗
} SlotState_t;

typedef struct __attribute__((packed)) {
    uint32_t    magic;              // 0x4D455441
    uint32_t    struct_version;     // 結構版本，目前為 1
    SlotID_t    active_slot;        // 目前 Active slot
    SlotID_t    previous_slot;      // 上次成功的 slot（回滾用）
    SlotState_t slot_state[2];      // [0]=Slot A, [1]=Slot B
    uint8_t     boot_count;         // 連續失敗次數，App 成功後歸零
    uint8_t     reserved[2];
    uint32_t    update_requested;   // OTA_REQUEST_FLAG 表示有待切換更新
    uint32_t    metadata_crc32;     // 對以上欄位的 CRC32
    uint8_t     padding[4060];      // 填滿至 4 KB
} FlashMetadata_t;                  // 總計 4096 bytes（1 sector）
```

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
    uint8_t  reserved[216];   // 保留，填 0xFF
    uint32_t header_crc32;    // 對以上欄位的 CRC32
} AppHeader_t;                // 總計 256 bytes（Header 區 4 KB，其餘保留）
```

---

## 5. Bootloader 規格

### 5.1 初始化序列

```
1. VTOR = 0x0800_0000
2. 啟動 HSE，設定 PLL2（PLL2P = 120 MHz）
3. GPIO 初始化：
   - PC13（KEY2，Input Pull-Up）
   - PC3（LED，Output，初始 Low）
4. SPI1 初始化：
   - PA4（CS，GPIO Output，Software NSS，初始 High）
   - PA5（SCK），PA6（MISO），PB5（MOSI）
   - Mode 0，Full-Duplex Master，Prescaler ÷2 → 60 MHz
5. USART1 初始化（PA9 TX, PA10 RX，115200 8N1）
6. 硬體 CRC 控制器初始化
7. 讀取 BY25Q32 JEDEC ID，若不符則停止並等待
8. delay 100 ms，偵測 KEY2 狀態，決定啟動路徑
```

### 5.2 SPI Flash 驅動介面

```c
void     SPI_Flash_Init(void);
uint32_t SPI_Flash_ReadJEDEC(void);          // BY25Q32：0x68, 0x40, 0x16

/* Bootloader 讀取（60 MHz，Fast Read 0x0B）*/
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
| Fast Read | `0x0B` | 120 MHz | Bootloader 讀取，需 1 dummy byte |
| Read Data | `0x03` | 50 MHz | Application 讀取 |
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

### 5.4 跳轉實作

```c
void Boot_JumpToApplication(void)
{
    uint32_t *vectors   = (uint32_t *)0x24000000;
    uint32_t  app_sp    = vectors[0];
    uint32_t  app_entry = vectors[1];

    if (app_sp < 0x24000000 || app_sp > 0x24080000) return;

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
EMPTY ──── OTA 開始寫入 ────► PENDING
                                  │
                     Bootloader 驗證成功
                     App boot_count 歸零
                                  │
                                  ▼
INVALID ◄─ 驗證失敗/boot_count>3 ─ VALID ◄─── 正常執行
```

### 7.2 OTA 流程

```
Application 執行中（透過 LwIP TCP 接收）
 │
 ▼
確認 Inactive slot（非 Active 那個）
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
| `boot_count >= 3` | 每次啟動遞增，App 成功後歸零 |
| `slot_state == INVALID` | Metadata 中已標記 |

回滾邏輯：

```c
if (!Boot_TrySlot(meta.active_slot)) {
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

### 8.1 YMODEM 緊急更新（USART1 115200）

觸發條件：KEY2 長按 3 秒上電，或兩個 Slot 均驗證失敗。

```
Bootloader 輸出：
  "STM32H750NetLite Bootloader vX.X"
  "YMODEM Recovery Mode - send firmware.bin"

每 1 秒發送 'C'，等待 PC 端開始傳送

PC 端：
  TeraTerm  → File → Transfer → YMODEM → Send
  minicom   → Ctrl-A S → ymodem → 選擇 .bin
  命令列    → sb --ymodem firmware.bin

Bootloader 邊接收邊寫入 Inactive slot
完成後驗證 CRC32 → 更新 Metadata → 重啟
```

### 8.2 Slot 寫入選擇邏輯

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
        . = . + 0x1000;   /* 4 KB */
        _estack = .;
    } > DTCMRAM
}
```

### 9.2 Application（`application.ld`）

```ld
MEMORY
{
    RAM    (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
    RAM_D2 (rw)  : ORIGIN = 0x30000000, LENGTH = 288K
}

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > RAM
    .text       : { *(.text*) *(.rodata*) } > RAM
    .data       : { *(.data*) } > RAM
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM

    /* DMA 緩衝區強制放入 RAM_D2 */
    .dma_buffer (NOLOAD) : { *(.dma_buffer) } > RAM_D2

    ._stack (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + 0x2000;   /* 8 KB */
        _estack = .;
    } > RAM
}
```

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
│       ├── clock.c          # PLL2P 120MHz 時脈設定
│       ├── spi_flash.c      # BY25Q32 驅動（Fast Read 60MHz）
│       ├── metadata.c       # Metadata 讀寫（含備份）
│       ├── ymodem.c         # YMODEM 接收
│       ├── crc32.c          # 硬體 CRC32 封裝
│       └── boot_jump.c      # 驗證、複製、跳轉
├── application/
│   ├── CMakeLists.txt
│   ├── application.ld
│   └── Core/Src/
│       ├── main.c
│       ├── ota.c            # OTA TCP 接收與寫入
│       ├── spi_flash.c      # BY25Q32 驅動（Read Data 12.5MHz）
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
- [ ] 實作 PLL2P 120 MHz 時脈初始化
- [ ] 實作 SPI1 驅動（60 MHz，Fast Read `0x0B`）
- [ ] 驗證 BY25Q32 JEDEC ID（`0x68 0x40 0x16`）
- [ ] 驗證 Sector Erase + Page Program + Fast Read 正確性
- [ ] 實作硬體 CRC32 封裝

### Phase 2 — Bootloader 核心

- [ ] 實作 Metadata 讀寫（含 CRC32 保護與備份）
- [ ] 實作 App Header 驗證
- [ ] 實作 Binary 複製至 AXI SRAM + CRC32 驗證
- [ ] 實作 `Boot_JumpToApplication()`（含時脈重設）
- [ ] 手動燒入測試 image，驗證完整啟動流程

### Phase 3 — YMODEM 緊急更新

- [ ] 實作 YMODEM 接收（USART1 115200）
- [ ] 整合 YMODEM → 寫入 SPI Flash → 驗證 → Metadata 更新 → 重啟
- [ ] 驗證 KEY2 觸發 YMODEM 模式
- [ ] 驗證兩個 slot 失敗後進入救援模式

### Phase 4 — Application 基礎

- [ ] 建立 Application CMake 專案，Linker Script 固定 AXI SRAM
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
- [ ] Bootloader 大小確認（目標 < 48 KB，保留空間）
- [ ] Application 大小確認（需 < 1 MB）
- [ ] 加入 Bootloader 版本號與 USART1 啟動訊息

---

## 12. 風險與注意事項

| 風險 | 影響 | 對策 |
|------|------|------|
| Bootloader 超過 128 KB | 無法燒錄 | `-Os`、`newlib-nano`、精簡 HAL，目標 < 48 KB |
| Application 超過 1 MB | 超出 Slot 大小 | Linker `ASSERT` 檢查，`mkfirmware.py` 也驗證 |
| Metadata sector 損毀 | 無法選擇 slot | 雙份 Metadata（主 + 備份），掃描回退機制 |
| OTA 斷電（寫 Binary 中途） | Binary CRC32 不符 | Header 最後才寫，CRC 不符視為 EMPTY |
| OTA 斷電（更新 Metadata 前） | 舊 slot 仍 Active，重啟用舊版 | 安全，舊版仍可正常運作 |
| BY25Q32 JEDEC ID 讀取錯誤 | SPI 時序問題 | Bootloader 啟動強制讀取 ID，不符則停止所有操作 |
| LAN8720A REF_CLK 未穩定 | ETH Link 失敗 | MCO1 初始化後強制 delay 1 ms 再初始化 ETH |
| DMA 緩衝區在 AXI SRAM | ETH/SDMMC 失敗 | Linker `.dma_buffer` section 強制 RAM_D2 |
| boot_count 未歸零 | 正常 App 被誤判連續失敗 | `Boot_NotifySuccess()` 在 App 初始化完成後立即呼叫 |
| Bootloader SPI 60 MHz 不穩定 | 複製資料損毀 | PCB 佈線問題，可降回 30 MHz（Prescaler ÷4）驗證 |

---

*本文件隨實作進度持續更新。各 Phase 完成後補充實際測試結果與調整記錄。*
