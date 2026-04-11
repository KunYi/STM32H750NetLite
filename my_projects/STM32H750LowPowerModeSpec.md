# STM32H750NetLite -- Low Power Mode 實作與 Trade-off 規格書

**硬體平台：** STM32H750VBT6 + BY25Q32 SPI NOR Flash（4 MB）
**適用文件：** `STM32H750BootSpec.md` v0.4、`STM32H750MCUbootSpec.md` v0.1、`STM32H750BootSharedStateSpec.md` v0.1
**文件版本：** v0.1
**日期：** 2026-04-11
**狀態：** 規劃中

---

## 1. 目的

本文件定義 STM32H750NetLite 的低功耗模式策略，讓 Bootloader 與 Application 對 Sleep、Stop、Standby、VBAT 回復流程有一致判斷。

本系統的 Application image 執行於 AXI SRAM。因此低功耗策略必須明確區分：

- 哪些模式會從原程式點繼續執行。
- 哪些模式會重新進入 Bootloader。
- 哪些 RAM 內容可視為保留。
- 什麼狀態應放在 RTC backup registers 或 Backup SRAM。

---

## 2. 設計結論

建議採用兩級策略：

```text
LP1 Stop Resume:
- 用於短時間 idle / 快速喚醒
- Application 自行 suspend/resume
- Bootloader 不介入
- AXI SRAM image 與 runtime context 視為保留

LP2 Standby Logical Resume:
- 用於長時間低功耗
- wakeup 後視為 reset-like startup
- Bootloader 重新執行
- Bootloader 重新從 SPI Flash 載入 Application 到 AXI SRAM
- Application 根據 backup domain 中的壓縮狀態重建 runtime
```

資料保存優先序：

```text
1. RTC backup registers：最多 128 bytes，優先用於最小 resume state
2. Backup SRAM：4 KB，只有 128 bytes 不足時才啟用 retention
3. External SPI NOR / T-Flash：用於較大、非揮發、可延遲寫入的狀態
```

不建議把完整 runtime context 放入 Backup SRAM。Backup SRAM 只保存可讓 Application 重建狀態的最小 checkpoint。

---

## 3. Low Power Mode 行為表

| 模式 | 主要用途 | Wakeup 後執行點 | Bootloader 是否重跑 | 一般 SRAM / AXI SRAM | Backup domain |
|------|----------|-----------------|---------------------|----------------------|---------------|
| Sleep / CSleep | 極短 idle | 中斷後繼續 | 否 | 保留 | 保留 |
| Stop / CStop | 快速恢復、省電 idle | WFI/WFE 後繼續 | 否 | 保留 | 保留 |
| Standby | 深度低功耗 | reset-like startup | 是 | 視為遺失 | 可保留 |
| VBAT / VDD 掉電 | 主電源移除 | reset-like startup | 是 | 遺失 | 依 VBAT 保留 |

注意：

- Stop mode 適合保留 AXI SRAM 中的 Application image，Application wakeup 後重設 clock tree / peripheral。
- Standby mode 更省電，但 VCORE domain power down，不能依賴 AXI SRAM / DTCMRAM / ITCMRAM。
- 從 Standby wakeup 後，Bootloader 必須重新載入 Application 到 AXI SRAM。

---

## 4. Backup Domain 使用策略

### 4.1 RTC Backup Registers

STM32H750 RTC backup registers 可保存 128 bytes 使用者資料。若 low-power resume state 可壓縮在 128 bytes 內，應優先使用 RTC backup registers，避免為了 4 KB Backup SRAM 額外開啟 backup regulator。

適合保存：

```text
magic
version
seq
crc32
wake_reason
active_slot
last_app_state
resume_flags
rtc_timestamp
small checkpoint
```

### 4.2 Backup SRAM

Backup SRAM 位於 `0x3880_0000` ~ `0x3880_0FFF`，大小 4 KB。若要在 Standby / VBAT mode 保留 Backup SRAM，Application 必須：

```text
1. Enable backup domain write access
2. Set PWR_CR2.BREN
3. Wait PWR_CR2.BRRDY
4. 寫入 Backup SRAM resume state
5. 以 magic/version/seq/crc32 驗證資料完整性
```

若 `PWR_CR2.BREN` 未啟用，Backup SRAM 仍可在 Run / Stop mode 使用，但 Standby / VBAT mode 內容不可視為可靠。

建議配置：

```text
0x38800000 + 0x0000 : 32B  BootExchange_t
0x38800000 + 0x0020 : 256B LowPowerResumeState copy A
0x38800000 + 0x0120 : 256B LowPowerResumeState copy B
0x38800000 + 0x0220 : reserved
```

使用雙份 copy 的原因是避免進 Standby 前寫到一半或電源事件造成單份資料不一致。讀取時以 `magic/version/crc32` 有效且 `seq` 較新的 copy 為準。

---

## 5. LP1 Stop Resume 流程

Stop mode 由 Application 管理，Bootloader 不介入。

進入 Stop：

```text
Application
 │
 ├── flush log / protocol state
 ├── 停止不需要的 peripheral
 ├── 設定 wakeup source
 │   - EXTI / WKUP pin
 │   - RTC alarm / wakeup timer
 │   - LPTIM
 │   - USART / LPUART / I2C / SPI Stop wakeup
 ├── 可選：寫入 RTC backup registers 記錄 last_low_power_mode = STOP
 └── WFI / WFE
```

喚醒後：

```text
Wakeup event
 │
 ▼
Application 從 WFI/WFE 後繼續
 │
 ├── 重新設定 system clock / PLL
 ├── 重新設定 peripheral clock
 ├── 恢復外部 IC 狀態
 ├── 清除 wakeup flag
 └── 回到 Application main loop / RTOS scheduler
```

Stop mode 規則：

- 不跳回 Bootloader。
- 不重載 AXI SRAM image。
- Application 必須自行處理 clock tree resume。
- Application 不應在 Stop resume path 假設 USB / SDMMC / Ethernet PHY / SPI Flash 狀態仍完整。

---

## 6. LP2 Standby Logical Resume 流程

Standby mode 由 Application 發起，但 wakeup 後 Bootloader 必須重新執行。

進入 Standby：

```text
Application
 │
 ├── 收斂 runtime state 為 compact resume state
 ├── 若 <= 128B：寫 RTC backup registers
 ├── 若 > 128B：啟用 Backup SRAM retention 並寫 Backup SRAM
 ├── 寫入 magic/version/seq/crc32
 ├── 設定 wakeup source
 └── enter Standby
```

從 Standby 喚醒：

```text
Wakeup event
 │
 ▼
Reset-like startup
 │
 ▼
Bootloader
 │
 ├── Minimal init: HSI 64MHz, GPIO, SPI1, CRC/crypto, backup domain access
 ├── 讀取 RTC backup registers / Backup SRAM
 ├── 驗證 magic/version/seq/crc32
 ├── 若有強制 Update flag 或 T-Flash special file，進入 Update Service
 ├── 否則選擇 active slot
 ├── 從 SPI Flash 載入 Application 到 AXI SRAM
 ├── 對 AXI SRAM image 做 readback hash/signature
 └── 跳轉 Application
        │
        ▼
 Application
        │
        ├── 讀取 resume reason
        ├── 重建 runtime object / protocol state
        └── 清理一次性 resume flag
```

Standby mode 規則：

- 一般 SRAM、AXI SRAM、DTCMRAM、ITCMRAM 不可作為 retained state。
- Bootloader 不應把 Standby resume 視為 OTA request，除非 backup flag 或 T-Flash special file 明確要求。
- Application 不可把 pointer、RTOS handle、mutex、task stack address 放入 backup domain。

---

## 7. Resume State 建議格式

V1 低功耗 resume state 優先放在 RTC backup registers，建議新增獨立 shared header，並使用與 `STM32H750BootSharedStateSpec.md` 相同的 32-byte `RtcBackupResumeState_t`：

```c
#define RTC_RESUME_MAGIC       0x5253554DUL  /* "RSUM" */
#define RTC_RESUME_VERSION     1U
#define RTC_RESUME_WORDS       8U

typedef enum {
    LP_MODE_NONE              = 0x00,
    LP_MODE_STOP              = 0x01,
    LP_MODE_STANDBY           = 0x02,
    LP_MODE_VBAT_RETURN       = 0x03,
} LowPowerMode_t;

typedef enum {
    LP_WAKE_UNKNOWN           = 0x00,
    LP_WAKE_RTC               = 0x01,
    LP_WAKE_PIN               = 0x02,
    LP_WAKE_WATCHDOG          = 0x03,
    LP_WAKE_TAMPER            = 0x04,
} LowPowerWakeReason_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  mode;
    uint8_t  wake_reason;
    uint8_t  active_slot;
    uint32_t seq;
    uint32_t rtc_timestamp;
    uint32_t app_state_id;
    uint32_t resume_flags;
    uint32_t app_state_arg0;
    uint32_t crc32;
} RtcBackupResumeState_t;

_Static_assert(sizeof(RtcBackupResumeState_t) == RTC_RESUME_WORDS * 4U,
               "RtcBackupResumeState_t must be 32 bytes");
```

規則：

- `RtcBackupResumeState_t` 不取代 `BootExchange_t`。
- `BootExchange_t` 仍保留 Bootloader/Application 的 32-byte 即時交換。
- `RtcBackupResumeState_t` 只用於 Standby / VBAT logical resume 的 V1 compact state。
- 若使用 Backup SRAM extended resume state，才定義較大的 `LowPowerResumeState_t`，並建議保存 copy A / copy B。

---

## 8. 與 Bootloader / OTA 的互動

Bootloader 啟動時的低功耗判斷順序：

```text
1. 讀取 reset / wakeup flags
2. 讀取 BootExchange_t
3. 讀取 RtcBackupResumeState_t 或 Backup SRAM extended resume state
4. 若有強制 Update flag：進 Update Service
5. 若 T-Flash 存在 special update file：進 T-Flash Update Service
6. 若低功耗 resume state 有效：記錄 BOOT_REASON_STANDBY_RESUME / VBAT_RETURN
7. 正常選 slot、驗證、載入 AXI SRAM、跳轉 Application
```

不論是自製 Bootloader 還是 MCUboot-based Bootloader：

- Standby wakeup 後都必須重新載入 AXI SRAM Application image。
- SPI Flash slot 的 A/B OTA 狀態仍以各 Bootloader 規格為準。
- Low-power resume state 不可直接修改 active slot / pending slot / confirm 狀態。
- Low-power resume state 不是安全信任根，只是恢復提示；image 是否可執行仍以 header/hash/signature/CRC 驗證為準。

---

## 9. Trade-off

| 選項 | 優點 | 代價 | 建議 |
|------|------|------|------|
| Sleep / CSleep | 喚醒最快、實作簡單 | 省電有限 | 短 idle 使用 |
| Stop / CStop | 保留 AXI SRAM 與 runtime，喚醒快 | 比 Standby 耗電高，resume path 複雜 | 中短時間 idle 使用 |
| Standby + RTC backup registers | 深度省電，無需 Backup SRAM retention | 只有 128B 狀態 | V1 優先方案 |
| Standby + Backup SRAM | 可保存最多 4KB 狀態 | backup regulator 增加耗電 | 128B 不夠時使用 |
| VBAT retention | 主電源消失仍可保留 backup domain | 依賴電池/超級電容與板級設計 | 只保存必要狀態 |
| External Flash checkpoint | 狀態可長期保存 | 寫入慢、有 wear、需斷電安全設計 | 大資料或低頻狀態使用 |

---

## 10. 測試清單

- [ ] Stop mode wakeup 後，Application 不經 Bootloader 並可重新設定 clock tree。
- [ ] Stop mode wakeup 後，AXI SRAM Application image 仍可正常執行。
- [ ] Standby wakeup 後，Bootloader 重新執行並重新載入 AXI SRAM image。
- [ ] Standby wakeup 後，RTC backup registers 的 resume state 可通過 CRC。
- [ ] Backup SRAM retention disabled 時，Standby 後不得信任 Backup SRAM resume state。
- [ ] Backup SRAM retention enabled 時，`PWR_CR2.BRRDY` ready 後寫入的 resume state 可在 Standby 後保留。
- [ ] copy A / copy B 其中一份 CRC 損壞時，能選擇另一份有效 copy。
- [ ] Low-power resume state 不會觸發錯誤 OTA slot 切換。
- [ ] 強制 Update flag 優先於 low-power resume reason。
- [ ] T-Flash special update file 優先於 low-power logical resume。

---

## 11. 待實測項目

- Backup SRAM retention 對整板 Standby 電流的實測影響。
- Wakeup source 最終清單：RTC、WKUP pin、LPTIM、USART/LPUART、tamper。
- V1 固定把 `RtcBackupResumeState_t` 放入 `my_projects/shared/low_power_resume.h`；extended `LowPowerResumeState_t` 只有超過 128 bytes 時才加入。
