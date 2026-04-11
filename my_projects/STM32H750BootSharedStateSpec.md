# STM32H750NetLite -- Boot/OTA Shared State 規格書

**硬體平台：** STM32H750VBT6 + BY25Q32 SPI NOR Flash（4 MB）
**適用文件：** `STM32H750BootSpec.md` v0.4、`STM32H750MCUbootSpec.md` v0.1
**文件版本：** v0.1
**日期：** 2026-04-11
**狀態：** 規劃中

---

## 1. 目的

本文件定義 Bootloader 與 Application 共用的 OTA 狀態語意，避免自製 Bootloader 規格與 MCUboot-based 規格各自定義不一致的狀態流程。

適用範圍：

- SPI NOR flash slot layout 命名規則
- Application OTA 寫 inactive slot 的狀態流程
- Bootloader 開機選 slot / rollback 的狀態流程
- Backup SRAM 32-byte exchange block
- 自製 metadata 版本的 `WRITING` / `metadata_seq` 設計
- MCUboot 版本如何對應到 MCUboot trailer / confirm 機制

原則：

- Application 不覆寫正在執行的 active slot。
- 更新過程中任何斷電都不可破壞上一個可啟動版本。
- Bootloader 與 Application 必須使用同一份 header 定義，不可各自複製 struct。
- Backup SRAM exchange block 不是安全信任根，只能作跨 reset 小量狀態提示。

---

## 2. 共用檔案建議

建議把共用定義放在獨立目錄，Bootloader 與 Application 同時 include：

```
my_projects/
├── shared/
│   ├── boot_flash_layout.h
│   ├── boot_metadata.h        # 自製 Bootloader 使用；MCUboot 版不使用
│   ├── app_image_header.h     # 自製 Bootloader 使用；MCUboot 版不使用
│   ├── boot_exchange.h
│   └── boot_update_result.h
```

規則：

- `boot_flash_layout.h` 定義 SPI NOR slot、userdata、metadata 位址。
- `boot_exchange.h` 定義 Backup SRAM 32-byte exchange block。
- 自製 Bootloader 與 Application 共用 `boot_metadata.h` / `app_image_header.h`。
- MCUboot 版不使用自製 metadata/header，但仍可使用 `boot_flash_layout.h`、`boot_exchange.h`、`boot_update_result.h`。

---

## 3. 共用 Flash Layout 名稱

```c
#define FLASH_ADDR_METADATA       0x000000UL
#define FLASH_ADDR_METADATA_BAK   0x001000UL

#define FLASH_ADDR_SLOT_A         0x004000UL
#define FLASH_ADDR_SLOT_A_HEADER  0x004000UL
#define FLASH_ADDR_SLOT_A_BIN     0x005000UL

#define FLASH_ADDR_SLOT_B         0x104000UL
#define FLASH_ADDR_SLOT_B_HEADER  0x104000UL
#define FLASH_ADDR_SLOT_B_BIN     0x105000UL

#define FLASH_ADDR_USERDATA       0x3C0000UL

#define FLASH_SLOT_MAX_SIZE       (1024UL * 1024UL)
#define FLASH_BIN_MAX_SIZE        (FLASH_SLOT_MAX_SIZE - 0x1000UL)
#define SRAM_APP_MAX_SIZE         (512UL * 1024UL)
```

MCUboot 版可把：

- `FLASH_ADDR_SLOT_A` 對應 `FLASH_AREA_IMAGE_PRIMARY`
- `FLASH_ADDR_SLOT_B` 對應 `FLASH_AREA_IMAGE_SECONDARY`

自製版可把：

- `FLASH_ADDR_SLOT_X_HEADER` 作為 `AppHeader_t` 起點
- `FLASH_ADDR_SLOT_X_BIN` 作為 binary 起點

---

## 4. Boot Handoff 與 Low-power Resume State

本系統把跨 reset 狀態分成三層：

| 層級 | 位置 | 用途 | Standby / VBAT 保留策略 |
|------|------|------|-------------------------|
| Boot handoff | Backup SRAM 前 32 bytes | Bootloader 跳轉 Application 前傳遞 boot reason、slot、error、request flag | 可用，但不作 V1 主要 low-power resume state |
| Low-power resume state | RTC backup registers 128 bytes | Standby / VBAT 後保存壓縮狀態 | V1 優先使用 |
| Extended resume state | Backup SRAM 剩餘區域 | RTC backup registers 不足時保存較大狀態 | V2 才啟用 Backup SRAM retention |

設計原則：

- `BootExchange_t` 是標準 handoff 介面，Application 啟動後只需要讀它即可知道 `boot_reason`。
- Standby / VBAT 的持久狀態 V1 優先放在 RTC backup registers，避免為了 4 KB Backup SRAM 額外啟用 backup regulator。
- Bootloader wakeup 後驗證 RTC backup registers 的 resume state，再把最後判斷出的 `boot_reason` 寫回 `BootExchange_t`。
- 若 128 bytes 不足，才啟用 Backup SRAM retention，並在 Backup SRAM 剩餘區域保存 `LowPowerResumeState` copy A/B。

### 4.1 Backup SRAM BootExchange_t

Backup SRAM 前 32 bytes 保留給 Bootloader 與 Application 交換資訊：

```c
#define BOOT_EXCHANGE_ADDR       0x38800000UL
#define BOOT_EXCHANGE_SIZE       32U
#define BOOT_EXCHANGE_MAGIC      0x42455843UL  /* "BEXC" */
#define BOOT_EXCHANGE_VERSION    1U

typedef enum {
    /* Normal boot path: no special recovery/update/low-power reason. */
    BOOT_REASON_NORMAL           = 0x00,

    /* User held the boot/update key and Bootloader entered Update Service. */
    BOOT_REASON_KEY_UPDATE       = 0x01,

    /* Application requested Update Service through BootExchange flags. */
    BOOT_REASON_APP_REQUEST      = 0x02,

    /* Bootloader could not find a valid slot; usually kept as diagnostic only. */
    BOOT_REASON_NO_VALID_SLOT    = 0x03,

    /* Bootloader selected previous confirmed image after failed trial image. */
    BOOT_REASON_ROLLBACK         = 0x04,

    /* Wakeup from Standby: Bootloader reran and loaded Application again. */
    BOOT_REASON_STANDBY_RESUME   = 0x05,

    /* VDD returned after VBAT-only retention; only backup domain is trusted. */
    BOOT_REASON_VBAT_RETURN      = 0x06,
} BootReason_t;

typedef enum {
    BOOT_FLAG_NONE               = 0x00,
    BOOT_FLAG_REQUEST_UPDATE     = 0x01,
    BOOT_FLAG_REQUEST_YMODEM     = 0x02,
    BOOT_FLAG_REQUEST_TFLASH     = 0x04,
} BootExchangeFlags_t;

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
- `magic`、`version`、`crc32` 都有效才接受內容。
- Application 要請求 Update Service 時，設定 `BOOT_FLAG_REQUEST_UPDATE` 後 `HAL_NVIC_SystemReset()`。
- Bootloader 進入 Update Service 後可清除 request flag，避免 reset loop。
- Application linker 必須保留 `0x38800000` ~ `0x3880001F`，不得放 `.bss` / heap / RTOS object。
- 交換區不得放指標、RTOS handle、mutex 或大資料。
- Standby / VBAT resume state 優先使用 RTC backup registers；BootExchange 只保存 Bootloader 最終整理後的 handoff 結果。

### 4.2 RTC Backup Registers Resume State

STM32H750 RTC backup registers 共 32 個 32-bit word，可保存 128 bytes 使用者資料。V1 低功耗 resume state 建議優先使用 RTC backup registers：

```c
#define RTC_RESUME_MAGIC       0x5253554DUL  /* "RSUM" */
#define RTC_RESUME_VERSION     1U
#define RTC_RESUME_WORDS       8U

typedef enum {
    LP_MODE_NONE               = 0x00,
    LP_MODE_STOP               = 0x01,
    LP_MODE_STANDBY            = 0x02,
    LP_MODE_VBAT_RETURN        = 0x03,
} LowPowerMode_t;

typedef enum {
    LP_WAKE_UNKNOWN            = 0x00,
    LP_WAKE_RTC                = 0x01,
    LP_WAKE_PIN                = 0x02,
    LP_WAKE_WATCHDOG           = 0x03,
    LP_WAKE_TAMPER             = 0x04,
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

RTC backup registers 寫入範例：

```c
static void RtcBackup_WriteResumeState(RTC_HandleTypeDef *hrtc,
                                       const RtcBackupResumeState_t *state)
{
    uint32_t words[RTC_RESUME_WORDS];

    HAL_PWR_EnableBkUpAccess();

    memcpy(words, state, sizeof(words));

    for (uint32_t i = 0; i < RTC_RESUME_WORDS; ++i) {
        HAL_RTCEx_BKUPWrite(hrtc, RTC_BKP_DR0 + i, words[i]);
    }
}

static void RtcBackup_SaveStandbyState(RTC_HandleTypeDef *hrtc,
                                       RtcBackupResumeState_t state)
{
    state.magic = RTC_RESUME_MAGIC;
    state.version = RTC_RESUME_VERSION;
    state.mode = LP_MODE_STANDBY;
    state.crc32 = Boot_Crc32(&state, sizeof(state) - sizeof(state.crc32));

    RtcBackup_WriteResumeState(hrtc, &state);
}
```

RTC backup registers 讀取範例：

```c
static bool RtcBackup_ReadResumeState(RTC_HandleTypeDef *hrtc,
                                      RtcBackupResumeState_t *state)
{
    uint32_t words[RTC_RESUME_WORDS];

    HAL_PWR_EnableBkUpAccess();

    for (uint32_t i = 0; i < RTC_RESUME_WORDS; ++i) {
        words[i] = HAL_RTCEx_BKUPRead(hrtc, RTC_BKP_DR0 + i);
    }

    memcpy(state, words, sizeof(*state));

    if (state->magic != RTC_RESUME_MAGIC ||
        state->version != RTC_RESUME_VERSION) {
        return false;
    }

    return Boot_Crc32(state, sizeof(*state) - sizeof(state->crc32)) ==
           state->crc32;
}
```

Bootloader 對 Standby / VBAT resume state 的處理範例：

```c
static uint8_t Boot_DeriveReasonFromLowPower(const RtcBackupResumeState_t *state)
{
    if (state->mode == LP_MODE_STANDBY) {
        return BOOT_REASON_STANDBY_RESUME;
    }

    if (state->mode == LP_MODE_VBAT_RETURN) {
        return BOOT_REASON_VBAT_RETURN;
    }

    return BOOT_REASON_NORMAL;
}
```

使用規則：

- RTC backup registers 的 resume state 只保存可重建 runtime 的小量狀態，不放 pointer、RTOS handle、stack address。
- RTC backup registers 的 state 不直接修改 OTA slot、MCUboot trailer、confirm/revert 或自製 metadata。
- Bootloader 只在驗證 magic/version/crc32 成功後才接受 resume state。
- Bootloader 接受 resume state 後，將推導出的 `BOOT_REASON_STANDBY_RESUME` / `BOOT_REASON_VBAT_RETURN` 寫入 `BootExchange_t.boot_reason`。
- Application 完成 logical resume 後應清除一次性 resume flag，避免下一次 reset 誤判。
- 範例中的 `Boot_Crc32()` 是共用 CRC helper；實作時可用 STM32 CRC peripheral 或軟體 CRC32，但 Bootloader 與 Application 必須使用相同算法。
- 使用 RTC backup registers 不需要啟用 `PWR_CR2.BREN`；`BREN` 只在需要 Backup SRAM 於 Standby / VBAT retention 時使用。

### 4.3 Backup SRAM Extended Resume State

只有當 RTC backup registers 128 bytes 不足時，才使用 Backup SRAM 剩餘區域保存 extended resume state。此時 Application 進 Standby / VBAT 前必須：

```text
1. Enable backup domain write access
2. Set PWR_CR2.BREN
3. Wait PWR_CR2.BRRDY
4. 寫入 Backup SRAM extended resume state
5. 寫入 magic/version/seq/crc32
```

建議配置：

```text
0x38800000 + 0x0000 : 32B  BootExchange_t
0x38800000 + 0x0020 : 256B LowPowerResumeState copy A
0x38800000 + 0x0120 : 256B LowPowerResumeState copy B
0x38800000 + 0x0220 : reserved
```

若使用 Backup SRAM extended resume state，讀取時以 `magic/version/crc32` 有效且 `seq` 較新的 copy 為準。

---

## 5. 自製 Bootloader Metadata v2

自製 Bootloader 版應把 slot state 擴充為 `WRITING`，並加入 `metadata_seq`。這是解決 Application OTA 與 Bootloader rollback 不同步的核心。

```c
#define METADATA_MAGIC        0x4D455441UL  /* "META" */
#define METADATA_STRUCT_VER   2U
#define BOOT_TRIAL_MAX        3U   /* maximum unconfirmed boot attempts */
#define BOOT_COUNT_MAX        BOOT_TRIAL_MAX
#define OTA_REQUEST_FLAG      0xA5A5A5A5UL

typedef uint8_t SlotID_t;
enum {
    SLOT_A = 0,
    SLOT_B = 1,
};

typedef uint8_t SlotState_t;
enum {
    SLOT_STATE_INVALID = 0x00,
    SLOT_STATE_VALID   = 0x01,
    SLOT_STATE_PENDING = 0x02,
    SLOT_STATE_WRITING = 0x03,
    SLOT_STATE_EMPTY   = 0xFF,
};

typedef struct __attribute__((packed)) {
    uint32_t    magic;
    uint32_t    struct_version;
    uint32_t    metadata_seq;
    SlotID_t    active_slot;
    SlotID_t    previous_slot;
    SlotState_t slot_state[2];
    uint8_t     boot_count;
    uint8_t     reserved[3];     /* align following uint32_t fields; do not rely on enum ABI */
    uint32_t    update_requested;
    uint32_t    metadata_crc32;
    uint8_t     padding[4068];
} FlashMetadata_t;

_Static_assert(sizeof(FlashMetadata_t) == 4096,
               "FlashMetadata_t must be one 4KB sector");
```

`metadata_seq` 規則：

- 每次寫入新的 metadata state 時遞增。
- primary / backup 都 CRC valid 時，選 `metadata_seq` 較大的版本。
- 只有一份 CRC valid 時，使用 valid 版本。
- 兩份都 invalid 時，Bootloader 掃描 Slot A/B 的 header，選有效者；若都無效，進 Update Service。

寫入順序：

```text
meta.metadata_seq += 1
更新 metadata_crc32
erase/write primary metadata
erase/write backup metadata
```

若 primary 寫入成功但 backup 寫入前斷電，Bootloader 仍可用 `metadata_seq` 選出較新的 primary。若 primary 寫壞但 backup 保持舊版，Bootloader 使用 backup。

---

## 6. 自製版 Application OTA 狀態流程

Application OTA 只寫 inactive slot，不修改正在執行版本所在 slot。

```text
Application running
 │
 ▼
Read metadata primary + backup，選 seq 最新且 CRC valid 者
 │
 ├── metadata invalid
 │      └── abort OTA，不由 Application 修復 metadata
 │
 ▼
new_slot = inactive_slot
 │
 ▼
slot_state[new_slot] = WRITING
active_slot 不變
previous_slot 不變
write metadata + backup
 │
 ▼
erase new_slot
 │
 ▼
write binary data，邊寫邊計算 CRC32
 │
 ▼
verify binary CRC32 by readback
 │
 ├── fail
 │      └── slot_state[new_slot] = INVALID
 │          active_slot 不變
 │          write metadata + backup
 │          abort
 │
 ▼
write App Header last
 │
 ▼
readback verify Header CRC32 + Binary CRC32
 │
 ├── fail
 │      └── slot_state[new_slot] = INVALID
 │          active_slot 不變
 │          write metadata + backup
 │          abort
 │
 ▼
commit update:
  slot_state[new_slot] = PENDING
  previous_slot = active_slot
  active_slot = new_slot
  boot_count = 0
  update_requested = OTA_REQUEST_FLAG
  write metadata + backup
 │
 ▼
HAL_NVIC_SystemReset()
```

`WRITING` 的目的：

- OTA 寫入中斷電後，Bootloader 明確知道該 slot 不能啟動。
- Application 不需要把半包 image 猜成 `EMPTY` 或 `INVALID`。
- Bootloader 不會把 `WRITING` slot 當作 fallback。

---

## 7. 自製版 Bootloader 開機與 Rollback 流程

```text
Bootloader start
 │
 ▼
Read primary + backup metadata
 │
 ├── both invalid
 │      └── scan Slot A/B header
 │          ├── found valid slot → rebuild metadata
 │          └── no valid slot → Update Service
 │
 ▼
select metadata by crc + metadata_seq
 │
 ▼
if slot_state[active_slot] == WRITING:
    if previous_slot valid:
        active_slot = previous_slot
        boot_count = 0
        write metadata + backup
    else:
        enter Update Service
 │
 ▼
if slot_state[active_slot] == PENDING or VALID:
    boot_count += 1
    write metadata + backup
    try Boot_TrySlot(active_slot)
 │
 ├── verify fail
 │      └── slot_state[active_slot] = INVALID
 │          active_slot = previous_slot
 │          boot_count = 0
 │          write metadata + backup
 │          try fallback
 │
 ├── boot_count > BOOT_TRIAL_MAX
 │      └── active_slot = previous_slot
 │          boot_count = 0
 │          write metadata + backup
 │          try fallback
 │
 ▼
copy to AXI SRAM and jump
```

Bootloader 不應啟動：

- `EMPTY`
- `INVALID`
- `WRITING`
- Header CRC invalid
- Binary CRC invalid
- `load_addr != 0x24000000`
- `binary_size > SRAM_APP_MAX_SIZE`，除非已導入 segmented loader

---

## 8. 自製版 Application Confirm 流程

Application 只在最小自檢通過後 confirm：

```text
Application starts
 │
 ▼
HAL / clock / critical peripheral init
 │
 ▼
App_MinimalSelfTest()
 │
 ├── fail
 │      └── 不修改 metadata，等待 watchdog/reset 後 Bootloader rollback
 │
 ▼
Read metadata by seq + crc
 │
 ├── active_slot != current_slot
 │      └── 不修改 metadata
 │
 ▼
slot_state[active_slot] = VALID
boot_count = 0
update_requested = 0
write metadata + backup
```

Application 必須知道自己從哪個 slot 啟動。建議 Bootloader 在 `BootExchange_t.boot_slot` 寫入 slot id，Application confirm 時用它交叉檢查 metadata。

---

## 9. MCUboot 版對應規則

MCUboot 版不使用自製 `FlashMetadata_t`、`AppHeader_t`、`WRITING`、`metadata_seq`。對應關係如下：

| 共用概念 | 自製版 | MCUboot 版 |
|----------|--------|------------|
| inactive slot write | Application 寫 inactive slot binary/header | Application 寫 MCUboot signed image 到 secondary/inactive area |
| write-in-progress | `SLOT_STATE_WRITING` | 不設定 test/permanent trailer；未完成 image 不會被選中 |
| pending/test boot | `SLOT_STATE_PENDING` | MCUboot test trailer / unconfirmed image |
| confirmed boot | `SLOT_STATE_VALID` + `boot_count = 0` | `boot_set_confirmed()` / image OK |
| rollback | `boot_count > BOOT_TRIAL_MAX` 或 verify fail | MCUboot revert / image not confirmed |
| metadata primary/backup | `metadata_seq + crc32` | MCUboot trailer 狀態與 image version |
| recovery request | `BootExchange_t.flags` | 同樣使用 `BootExchange_t.flags` |

MCUboot Application OTA 規則：

- 下載 payload 必須是 `imgtool.py sign` 產生的 signed image。
- 寫入完成前不要設定 test/permanent trailer。
- 寫入完成且基本 readback 檢查通過後，呼叫 `Image_RequestTestUpgrade()`；其底層必須使用 MCUboot bootutil API 或本專案固定版本相容 wrapper。
- 新 image 啟動並完成自檢後，呼叫 `Image_ConfirmCurrent()`；其底層對應 `boot_set_confirmed()` 或平台等效 API。
- 若使用 T-Flash Update Service，T-Flash 檔案也必須是 MCUboot signed image。
- Application、Update Service 與任何 RTOS OTA task 不得各自手寫 trailer layout；若必須手寫，需固定 MCUboot commit 並把 trailer layout regression test 納入 CI。

---

## 10. Segment Loader 擴展邊界

若 Application initialized code/data 要突破 AXI SRAM 512 KB，必須引入 segment manifest。

共通規則：

- manifest 必須被完整性保護。
- MCUboot 版：V2 初版固定把 manifest 放在 signed payload 起點；protected TLV 版本只作未來擴展。
- MCUboot V2 segmented loader 不直接使用 stock `MCUBOOT_RAM_LOAD` copy path，否則整包 payload 仍需放入 AXI SRAM，無法突破 512 KB。
- 自製版：manifest 至少要被 App Header CRC / Binary CRC 覆蓋；正式產品建議升級簽章驗證。
- Bootloader 必須檢查 segment address range、overlap、alignment。
- segment 不可覆蓋 Backup SRAM exchange block `0x38800000` ~ `0x3880001F`。
- boot vector table 與 Reset_Handler 初版仍固定在 AXI SRAM `0x24000000` 起跳。
- runtime interrupt vector table 可在 Application early startup 複製到 ITCMRAM `0x00000000`，並在任何 IRQ enable 前設定 `SCB->VTOR = 0x00000000`。
- 關鍵 ISR 可放在 signed image 內的 `.itcm_isr` / `.itcm_text`，由 Application startup 或 V2 segment loader 搬到 ITCMRAM；manifest 必須檢查 ITCM address range、alignment、overlap。

---

## 11. 測試清單

- [ ] OTA 中途斷電後，active slot 仍維持舊版。
- [ ] `WRITING` slot 不會被 Bootloader 啟動。
- [ ] primary metadata 新、backup metadata 舊，Bootloader 選 `metadata_seq` 較新的 valid metadata。
- [ ] primary metadata 壞、backup metadata valid，Bootloader 使用 backup。
- [ ] 兩份 metadata 都壞，Bootloader 掃描 slot header；都無效則進 Update Service。
- [ ] 新 image 不 confirm，Bootloader rollback 到 previous slot。
- [ ] 新 image confirm 後，後續 reboot 維持新 slot。
- [ ] Backup SRAM request update flag 可讓 Application 觸發 Update Service。
- [ ] MCUboot 版未完成寫入的 image 不設定 trailer，不被選中。
- [ ] T-Flash 無卡 / 無檔案 / 檔案無效時 fallback 到 UART YMODEM。
