# my_projects

這個目錄用來放本倉庫的自行整理與移植專案。
目前的基礎專案為 `STM32H750NetLite.ioc`，已使用 **STM32CubeMX 6.17.0** 建立，作為後續開發的起點。

## 專案定位

`STM32H750NetLite.ioc` 不是針對單一功能範例，而是以這塊開發板的硬體資源為基礎，先把板上可用且已知的主要外設配置完成，方便後續直接延伸成實際應用專案。

開發板硬體背景與模組來源請先閱讀根目錄的 [README.md](../README.md)。

## 目前已完成的基礎設定

這份 `IOC` 目前已先把開發板上主要外設腳位與時鐘基礎配置整理完成，包含：

- `GPIO`
  - `PC3` 板載 LED 輸出
  - `PC13` 板載按鍵輸入
  - `PA0` Wakeup 腳位
- `ETH`
  - 使用 `RMII`
  - 已配置 `LAN8720A` 所需腳位
  - `PA8` 輸出 `MCO1`，提供 PHY 參考時鐘
- `FDCAN1`
  - `PD0` / `PD1`
- `SPI1`
  - `PA4` / `PA5` / `PA6` / `PB5`
- `SDMMC1`
  - 4-bit bus 模式
- `USART1`
  - `PA9` / `PA10`
- `USB_OTG_FS`
  - 目前設為 `Device Only`
- `RTC`
  - 已啟用 `LSE`
- `SWD`
  - 保留除錯下載介面
- `RCC / Clock Tree`
  - 已完成 HSE / LSE 與主時鐘基礎配置
  - `SYSCLK` 設為 `480 MHz`

## Middleware 狀態

目前 **沒有選用任何 Middleware**。
也就是說，這份基礎專案先只處理：

- MCU 型號與記憶體配置
- 板上外設腳位分配
- 基本時鐘樹
- HAL 初始化框架

像是下列功能都刻意先不綁定：

- `LwIP`
- `USB Class`
- `FatFs`
- `FreeRTOS`
- 其他 CubeMX Middleware

這樣做的目的，是先保留一個乾淨、可維護、方便再生的基礎專案，再依實際需求逐步加入中介層與應用程式碼。

## 使用方式

建議把這份 `IOC` 視為板級初始化模板：

1. 先用 CubeMX 開啟 `my_projects/STM32H750NetLite.ioc`
2. 依實際需求增減外設設定
3. 需要網路、檔案系統或 RTOS 時，再選擇對應 Middleware
4. 重新產生專案碼後，在使用者程式區加入應用邏輯

## 補充說明

- 目前的設定重點是「先把整塊板子的外設基礎鋪好」，不是直接對應賣家的某一個單獨範例。
- 如果後續確認某些板上資源接法、腳位用途或時鐘需求需要微調，可以直接回到 `IOC` 修改後再重新產生。
- 根目錄 `README.md` 主要整理的是開發板硬體資訊與原廠範例背景；這裡則專注在 `my_projects` 內的基礎 CubeMX 專案說明。
