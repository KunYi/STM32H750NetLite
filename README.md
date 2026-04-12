# STM32H750VBT6 開發板 SDK

> STM32H750VBT6 + LAN8720A 乙太網路 / CANFD / USB 多功能開發板
> 原廠範例整理、學習筆記與未來 GCC 移植紀錄

![Platform](https://img.shields.io/badge/MCU-STM32H750VBT6-blue)
![Core](https://img.shields.io/badge/Core-Cortex--M7%20%40%20480MHz-brightgreen)
![IDE](https://img.shields.io/badge/原廠IDE-Keil%20MDK-orange)
![Status](https://img.shields.io/badge/GCC移植-規劃中-yellow)

購買連結：[淘寶商品頁面](https://item.taobao.com/item.htm?id=763078439777)

![Board](docs/pics/board.webp)

---

## 硬體規格

| 項目 | 說明 |
|------|------|
| **主控晶片** | STM32H750VBT6（Arm Cortex-M7，480 MHz） |
| **內建 Flash** | 128 KB（零等待） |
| **SRAM** | 1 MB（含 TCM / AXI / SRAM1~3） |
| **外接 NOR Flash** | Boya Micro BY25Q32ESTI（32 Mbit） |
| **乙太網路 PHY** | Microchip LAN8720A，RMII 介面，10/100 Mbps |
| **CAN** | FDCAN1 × 1（支援 Classic CAN / CAN FD），收發器 NXP TJA1042 |
| **USB** | USB FS（Device / Host / OTG） |
| **其他介面** | SPI、I²C、UART / RS232、TIM、ADC、DAC、SDMMC |
| **電源** | USB Type-C 供電，3.3 V / 5 V |
| **工作溫度** | −20 °C ～ 85 °C |

電路圖：[docs/750NetLiteSch_V1.1.pdf](docs/750NetLiteSch_V1.1.pdf)

---

## 目錄結構

```
.
├── docs/
│   └── 750NetLiteSch_V1.1.pdf              # 硬體電路圖（賣家提供）
├── 软件发布/                                # 賣家提供的 Keil 範例（版權歸賣家所有，詳見授權說明）
│   ├── ADC电压采集/
│   │   ├── ADC—双重ADC-单通道—交替采集/
│   │   ├── ADC—独立模式-单通道—DMA/
│   │   ├── ADC—独立模式-单通道—中断/
│   │   ├── ADC—独立模式-多通道—DMA/
│   │   └── ADC—电压采集/
│   ├── CAN通信实验/
│   │   ├── CAN参考资料/
│   │   ├── CAN—双机通讯/
│   │   └── CAN—回环测试/
│   ├── DAC输出正弦波/
│   ├── EXTI一外部中断/
│   ├── GPIO输入一按键检测/
│   ├── LPTIM—低功耗定时器PWM输出/
│   ├── LWIP_Done/
│   │   ├── lwip_dhcp/
│   │   ├── lwip_http/
│   │   ├── lwip_http_led/
│   │   ├── lwip_ping_raw/
│   │   ├── lwip_tcpclient_raw/
│   │   ├── lwip_tcpecho/
│   │   ├── lwip_tcpecho_raw/
│   │   └── lwip_udpecho/
│   ├── SDMMC—SD卡读写测试/
│   ├── SPI一读写串行FLASH/
│   ├── TIM—基本定时器定时/
│   └── USART一串口通信/
│       ├── UART5—RS232接发/
│       ├── USART2—RS232接发/
│       ├── USART—USART1指令控制LED/
│       ├── USART—USART1接发/
│       └── USART—USART2指令控制LED/
│
└── my_projects/                            # 自行整理的 CubeMX / GCC 基礎專案
    ├── README.md
    └── STM32H750NetLite.ioc
```

每個 Keil 範例的內部結構大致如下：

```
<範例名稱>/
├── Libraries/    # STM32 HAL / 驅動函式庫
├── Listing/      # 編譯清單輸出
├── Output/       # 編譯產出（.o、.d、.bin 等，已加入 .gitignore）
├── Project/      # Keil 專案檔（.uvprojx、.uvoptx）
├── User/         # 使用者程式碼（main.c 等）
└── 必读说明.txt  # 賣家說明文件（每個範例都建議先閱讀）
```

`my_projects/` 目前已新增 [`my_projects/README.md`](my_projects/README.md) 與 [`my_projects/STM32H750NetLite.ioc`](my_projects/STM32H750NetLite.ioc)，作為後續自行開發的基礎專案。

### 原廠目錄名稱對照

由於賣家提供的原始檔名採用簡體中文，以下整理常見目錄名稱的繁體中文對照，方便閱讀；實際資料夾名稱仍以倉庫中的原始名稱為準。

| 原始名稱 | 繁體中文對照 |
|------|------|
| `软件发布/` | 軟體發布 |
| `ADC电压采集/` | ADC 電壓採集 |
| `CAN通信实验/` | CAN 通訊實驗 |
| `CAN参考资料/` | CAN 參考資料 |
| `DAC输出正弦波/` | DAC 輸出正弦波 |
| `EXTI一外部中断/` | EXTI 外部中斷 |
| `GPIO输入一按键检测/` | GPIO 輸入與按鍵檢測 |
| `LPTIM—低功耗定时器PWM输出/` | LPTIM 低功耗定時器 PWM 輸出 |
| `SDMMC—SD卡读写测试/` | SDMMC SD 卡讀寫測試 |
| `SPI一读写串行FLASH/` | SPI 讀寫序列 Flash |
| `TIM—基本定时器定时/` | TIM 基本定時器計時 |
| `USART一串口通信/` | USART 串列埠通訊 |
| `必读说明.txt` | 必讀說明 |

---

## 原廠範例說明

### ADC 電壓採集

| 範例 | 說明 |
|------|------|
| 独立模式-单通道—中断 | ADC 單通道，中斷觸發讀值 |
| 独立模式-单通道—DMA | ADC 單通道，使用 DMA 傳輸 |
| 独立模式-多通道—DMA | ADC 多通道掃描，使用 DMA 傳輸 |
| 双重ADC-单通道—交替采集 | 雙 ADC 交替，提升採樣率 |
| ADC—电压采集 | 基本電壓量測範例 |

### CAN 通信

| 範例 | 說明 |
|------|------|
| CAN—回环测试 | FDCAN1 迴路測試，不需外部裝置 |
| CAN—双机通讯 | 兩塊開發板互傳，使用 TJA1042，需接終端電阻（120 Ω） |

### 乙太網路（LwIP）

| 範例 | 說明 |
|------|------|
| lwip_dhcp | DHCP 自動取得 IP |
| lwip_http | 靜態 HTTP 伺服器 |
| lwip_http_led | HTTP 控制板載 LED |
| lwip_ping_raw | Ping 回應（ICMP） |
| lwip_tcpecho | TCP Echo 伺服器（netconn API） |
| lwip_tcpecho_raw | TCP Echo 伺服器（Raw API） |
| lwip_tcpclient_raw | TCP 用戶端 |
| lwip_udpecho | UDP Echo |

注意：LAN8720A 使用 `PA8 / MCO1` 作為時鐘來源，可從相關初始化程式中確認設定方式。

### 其他

| 範例 | 說明 |
|------|------|
| DAC输出正弦波 | DAC 輸出正弦波，可搭配示波器觀察 |
| EXTI一外部中断 | 外部中斷（按鍵） |
| GPIO输入一按键检测 | GPIO 輸入偵測 |
| LPTIM—低功耗定时器PWM输出 | 低功耗定時器 PWM 輸出 |
| SDMMC—SD卡读写测试 | SD 卡讀寫（SDMMC 介面） |
| SPI一读写串行FLASH | SPI Flash 讀寫 |
| TIM—基本定时器定时 | 基本定時器 |
| USART一串口通信 | UART / RS232 多組範例 |

---

## 開發環境

### 原廠 Keil 環境

| 工具 | 說明 |
|------|------|
| Keil MDK ≥ 5.34 | 需安裝 STM32H7 Device Pack |
| STM32CubeMX | 外設圖形化設定 |
| STM32CubeProgrammer | 韌體燒錄、選項位元設定 |
| ST-Link V2 / V3 | 燒錄與偵錯介面 |

### 未來 GCC 開源環境（規劃中）

| 工具 | 說明 |
|------|------|
| VSCode + Cortex-Debug | 編輯器與偵錯介面 |
| GNU Arm Embedded Toolchain | `arm-none-eabi-gcc` 編譯器 |
| CMake + Ninja | 建構系統 |
| OpenOCD | 開源燒錄與 GDB Server |

#### 使用 OpenOCD v0.12 with RP2040 Zero debugging

基本上基於 Raspberry DebugProbe 適配到 Waveshare RP2040 Zero 上面

![DebugProbe](docs/pics/debugprobe.webp)

**需要連接下面接腳**

|  Zero |   說明   | H750NetLite |
|-------|---------|-------------|
|  GP1  | nReset  |    Reset    |
|  GP2  | SWD_CLK |    CLK      |
|  GP3  | SWD_DIO |    DIO      |
|  GP4  | UART TX |    RX       |
|  GP5  | UART RX |    TX       |
|  GND  |   GND   |    GND      |
|  3V3  |  3.3V   |    3V3      |

![PinOut Defined](docs/pics/RP2040-Zero-pinout.jpg)

---

## CubeMX 板級設定注意

這塊 STM32H750NetLite 板子的 Vcore power supply 依賣家範例應設定為
**LDO supply**。

在 STM32CubeMX 建立或重新產生 `.ioc` 時，請確認：

| 項目 | 設定 |
|------|------|
| PWR / Power Supply | `PWR_LDO_SUPPLY` |
| 產生碼檢查 | `HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);` |
| CMake symbol | `USE_PWR_LDO_SUPPLY` |

不要改成 SMPS 相關設定，除非硬體版本已確認有對應的 SMPS 供電設計；否則可能造成上電或時脈初始化不穩定。

---

## 燒錄注意事項

原廠範例的燒錄方式大致可分為兩類：

**1. 直接燒錄到內部 Flash（128 KB）**

大多數功能範例（GPIO、ADC、UART、TIM、CAN、LwIP 等）程式碼較小，可直接燒錄到內部 128 KB Flash，使用一般 ST-Link 即可，通常不需要額外設定。

**2. 使用板上外部序列快閃記憶體**

這塊板子雖然有外部 NOR Flash，但實際上不是接到 STM32 的 QUADSPI 控制器，因此不能直接用標準 QSPI XIP 的方式處理。

若要利用板上 SPI Flash，通常需要把程式設計成位置獨立碼（PIC），再於啟動後搬移到 SRAM 執行。

使用 `arm gcc` 編譯此類程式時，可視需求評估是否加入 `-fPIC`。

各範例的 `必读说明.txt` 會說明實際採用哪一種方式，建議優先閱讀。

---

## 常見問題

**Q：乙太網路 Link 燈不亮？**
A：確認 LAN8720A PHY 位址（通常 `0x00`，依 `PHYAD0` 腳位決定）；檢查 RMII 50 MHz 時脈輸出是否正常。

**Q：CAN 收不到資料？**
A：確認 TJA1042 供電（3.3 V）以及 STBY 腳位為低電位（正常模式）；確認終端電阻（120 Ω）；FDCAN1 時脈需來自 PLL，並檢查 CubeMX 的時鐘樹設定。

**Q：燒錄後無反應？**
A：先確認目前範例是要燒錄到內部 Flash，還是採用外部 SPI Flash 搬移執行；若是後者，需再檢查啟動流程、搬移位址與連結腳本設定是否正確。

---

## 參考資料

- [STM32H750 Datasheet](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf)
- [STM32H7 Reference Manual (RM0433)](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [LAN8720A Datasheet](docs/Datasheets/LAN8720a.pdf)
- [BY25Q32ES Datasheet](docs/Datasheets/BY25Q32ES.pdf)
- [TJA1042 Datasheet](docs/Datasheets/TJA1042.pdf)
- [LwIP 官方文件](https://www.nongnu.org/lwip/2_1_x/index.html)
- [STM32CubeH7 韌體套件](https://github.com/STMicroelectronics/STM32CubeH7)

---

## 授權說明

本倉庫包含來自不同來源的內容，各部分授權不同：

| 目錄 / 檔案 | 內容 | 授權 |
|-------------|------|------|
| `软件发布/` | 原廠 Keil 範例、驅動、說明文件 | 版權歸原始賣家所有 |
| `docs/750NetLiteSch_V1.1.pdf` | 硬體原理圖 | 版權歸原始賣家所有 |
| `my_projects/` | 自行整理的 CubeMX / GCC 專案 | MIT License |

### `软件发布/` — 僅供個人學習

此目錄為購買開發板後隨附的原廠 SDK，**版權歸原始賣家所有**。本倉庫僅以個人學習與備份為目的保存，不代表擁有再次散布或商業使用的權利。

如需取得正式授權或商業授權，請聯絡原始賣家：
👉 [淘寶商品頁面](https://item.taobao.com/item.htm?id=763078439777)

### `my_projects/` — MIT License

`my_projects/` 目錄中的自行整理內容與後續 GCC 移植內容，以 MIT License 釋出。
