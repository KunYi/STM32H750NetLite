# STM32H750NetLite — 個人移植專案

基於 STM32H750VBT6 開發板的個人開發專案，
以 CubeMX 產生基礎設定，目標工具鏈為 VSCode + GNU Arm GCC + CMake。

> **目前狀態：CubeMX 設定階段，尚未產生程式碼。**

---

## 檔案說明

```
my_projects/
└── STM32H750NetLite.ioc    # CubeMX 6.17 專案設定檔
```

---

## 開發環境

| 工具 | 版本 / 說明 |
|------|-------------|
| STM32CubeMX | 6.17.0 |
| STM32CubeH7 Firmware | v1.13.0 |
| 目標工具鏈 | CMake（CubeMX 已設定） |
| 編譯器（規劃中） | GNU Arm Embedded Toolchain（`arm-none-eabi-gcc`） |
| IDE（規劃中） | VSCode + Cortex-Debug |
| 燒錄（規劃中） | OpenOCD / ST-Link |

---

## CubeMX 外設設定

### 時鐘（RCC）

| 項目 | 數值 |
|------|------|
| HSE | 25 MHz（外部晶振，PH0/PH1） |
| LSE | 外部 32.768 kHz（PC14/PC15，供 RTC） |
| CPU（SYSCLK） | **480 MHz**（PLL1P） |
| AXI / AHB | 240 MHz |
| APB1 / APB2 / APB3 / APB4 | 120 MHz |
| FDCAN 時脈來源 | PLL2Q → **40 MHz** |
| SPI1 時脈來源 | CKPER（HSE）→ **25 MHz** |
| USART1 時脈來源 | PLL3Q → **48 MHz** |
| USB 時脈來源 | PLL3Q → **48 MHz** |
| SDMMC1 時脈來源 | PLL2R → **48 MHz** |
| MCO1 輸出（PA8）| HSE → **25 MHz**（提供 LAN8720A 參考時脈） |

### 記憶體映射

| 區域 | 起始位址 | 大小 | 說明 |
|------|----------|------|------|
| FLASH | `0x0800_0000` | 128 KB | 內部 Flash，預設程式碼區 |
| ITCMRAM | `0x0000_0000` | 64 KB | 指令緊耦合 RAM（Write-through cached） |
| DTCMRAM | `0x2000_0000` | 128 KB | 資料緊耦合 RAM |
| RAM（AXI SRAM） | `0x2400_0000` | 512 KB | 主要資料區（預設 heap/stack） |
| RAM_D2 | `0x3000_0000` | 288 KB | DMA 資料區（ETH、SDMMC 等） |
| RAM_D3 | `0x3800_0000` | 64 KB | 低功耗域 SRAM |

> Stack: 0x400（1 KB）　Heap: 0x800（2 KB）

### 腳位分配

**ETH — 乙太網路（LAN8720A，RMII）**

| 腳位 | 信號 |
|------|------|
| PA1 | ETH_REF_CLK |
| PA2 | ETH_MDIO |
| PA7 | ETH_CRS_DV |
| PB11 | ETH_TX_EN |
| PB12 | ETH_TXD0 |
| PB13 | ETH_TXD1 |
| PC1 | ETH_MDC |
| PC4 | ETH_RXD0 |
| PC5 | ETH_RXD1 |
| PA8 | MCO1 → LAN8720A REF_CLK（25 MHz） |

**FDCAN1（NXP TJA1042）**

| 腳位 | 信號 |
|------|------|
| PD0 | FDCAN1_RX |
| PD1 | FDCAN1_TX |

> 額定波特率：833.333 kbps（CubeMX 計算值）

**SPI1 — NOR Flash（Boya Micro BY25Q32ES）**

| 腳位 | 信號 |
|------|------|
| PA4 | SPI1_NSS（Software） |
| PA5 | SPI1_SCK |
| PA6 | SPI1_MISO |
| PB5 | SPI1_MOSI |

> Full-Duplex Master，12.5 Mbps

**USART1 — 除錯串口**

| 腳位 | 信號 |
|------|------|
| PA9 | USART1_TX |
| PA10 | USART1_RX |

> Asynchronous 模式，時脈 48 MHz（PLL3Q）

**USB_OTG_FS**

| 腳位 | 信號 |
|------|------|
| PA11 | USB_OTG_FS_DM |
| PA12 | USB_OTG_FS_DP |

> Device Only 模式

**SDMMC1 — SD 卡（4-bit）**

| 腳位 | 信號 |
|------|------|
| PC8 | SDMMC1_D0 |
| PC9 | SDMMC1_D1 |
| PC10 | SDMMC1_D2 |
| PC11 | SDMMC1_D3 |
| PC12 | SDMMC1_CK |
| PD2 | SDMMC1_CMD |

**GPIO**

| 腳位 | 標籤 | 方向 | 說明 |
|------|------|------|------|
| PC3 | LED | Output | 板載 LED |
| PC13 | PC13_KEY2 | Input | 按鍵 2 |
| PA0 | PA0_WKUP | PWR_WKUP1 | 喚醒按鍵 |
| PA4 | FLASH_CS | Output | Flash Chip Select |

**RTC**

| 腳位 | 信號 | 說明 |
|------|------|------|
| PB2 | RTC_OUT_CALIB | 512 Hz 校正輸出 |
| PB15 | RTC_REFIN | 外部參考時脈偵測 |

**SWD 除錯**

| 腳位 | 信號 |
|------|------|
| PA13 | SWDIO |
| PA14 | SWCLK |

---

## Middleware 計畫

目前 CubeMX 未啟用任何 Middleware，待程式碼移植階段依需求加入：

| Middleware | 用途 | 狀態 |
|------------|------|------|
| LwIP | TCP/IP 協議棧（搭配 ETH） | 規劃中 |
| FreeRTOS | 即時作業系統 | 規劃中 |
| FatFS | SD 卡檔案系統（搭配 SDMMC1） | 規劃中 |
| USB Device CDC | 虛擬串口 | 規劃中 |

---

## 開發進度

- [x] 確認板載硬體規格（Flash、PHY、CAN 收發器）
- [x] CubeMX 6.17 + CubeH7 v1.13.0 建立專案
- [x] 設定所有板載外設腳位與時鐘
- [x] 目標工具鏈設為 CMake
- [ ] CubeMX 產生初始程式碼框架
- [ ] 建立 VSCode + Cortex-Debug 除錯設定（`.vscode/`）
- [ ] 驗證 LED / USART1 基本輸出
- [ ] 驗證 ETH + LAN8720A Link
- [ ] 驗證 FDCAN1 Loopback
- [ ] 驗證 SPI1 Flash（BY25Q32ES）讀寫
- [ ] 驗證 SDMMC1 SD 卡
- [ ] 驗證 USB CDC

---

## 授權

本目錄內容由本人自行建立，以 **MIT License** 釋出。
詳見上層目錄 [LICENSE-MIT](../LICENSE-MIT)。
