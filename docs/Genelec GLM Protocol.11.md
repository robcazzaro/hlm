*This file is part of the Homebrew Loudspeaker Manager distribution (https://github.com/robcazzaro/hlm).<br>
Copyright (c) 2026 Rob Cazzaro.*<br><br>

# Genelec GLM Telemetry & Control Protocol Specification

**Revision:** 2026 v11 (Standby settings, level boundaries, telemetry VU meters, standalone/power lifecycle and volume/dim semantics fully decoded. Low-confidence items flagged for future testing.)

**Note: The protocol was reverse engineered using two 8320A speakers. It's entirely possible that different speakers require different data formats. For example, the 8320A uses a 48 kHz DSP and the PEQ coefficients are calculated for a 48 kHz DSP. Higher end speakers like the 8351B use a 96 kHz DSP and will need a new set of functions and an update to this document. Please use the protocol analyzer function to confirm with your setup.**

*Certain commands and parameters in this document are marked with a confidence level less than 90% and might require updates*

## 1. Physical & Data Link Layer

* **Medium:** RS-485 Half-Duplex Serial.  
* **UART Config:** 9-bit UART (no parity). The 9th bit is set to 1 for the first byte of every frame (the Address byte), and 0 for all subsequent payload, CRC, and flag bytes.  
* **Memory Buffers:** The host processor uses 16-bit `uint16_t` arrays for RX/TX to accommodate the 9th bit.  
  * Address bytes are stored as `0x01XX`.  
  * Data/Escape/End bytes are stored as `0x00XX`.  

* **Framing:** HDLC-like. Every frame ends with the End Flag `0x7E` (stored as `0x007E`).  
* **HDLC Escaping:** To prevent false flags, payload and CRC bytes are escaped during transmission:  
  * If a byte is `0x7E`, transmit `0x7D` followed by `0x5E`.  
  * If a byte is `0x7D`, transmit `0x7D` followed by `0x5D`.  
  * *Note: Escaping is applied after CRC calculation on TX, and must be reversed before CRC calculation on RX.*

## 2. Error Checking (CRC)

* **Algorithm:** CRC-16/GSM (Equivalent to CRC-16/XMODEM with a final XOR of `0xFFFF`).  
* **Parameters:** Polynomial `0x1021`, Init `0x0000`, RefIn: False, RefOut: False, XorOut: `0xFFFF`.  
* **Scope:** The CRC is calculated over the unescaped lower 8 bits of the Address byte AND the unescaped Payload bytes.  
* **Byte Order:** The resulting 16-bit CRC is appended to the frame in Big-Endian format (High byte, then Low byte). The CRC bytes themselves are subject to HDLC escaping if they match `0x7E` or `0x7D`.

## 3. Addressing Scheme

* **0x01FF:** Hub Broadcast (Used by PC App for global commands).  
* **0x01F0:** Hub Multicast (Used by Hub standalone for global commands and session assignment).  
* **0x0102 to 0x01FE:** Hub Unicast (Lower 8 bits represent the dynamically assigned Speaker Session ID).  
  * Session IDs observed to start at `0x02` on a fresh hub boot and increment by one per assignment. `0x01` has never been observed. Re-discovered speakers (e.g. after standby/loss of session) are assigned the next unused ID; unaffected speakers retain theirs. If a speaker doesn't respond to 4 telemetry requests, the hub restarts Session ID discovery from the last ID+1 (a rapid 4-frame retry burst ~9 ms apart is issued to the dead address before re-assignment).
* **0x0101:** Speaker Response (All speakers reply to the Hub using this universal address).

## 4. Payload Data Formats

* **Byte Order:** All multi-byte integer values and CRCs are Big-Endian unless otherwise specified.  
* **Floating Point:** DSP filter coefficients use 32-bit IEEE 754 format in Little-Endian byte order. The system operates at a fixed internal processing sample rate of **48,000 Hz**.  
* **Volume (Fixed-Point):** 24-bit unsigned integer representing linear gain. Scale factor is $2^{23}$ (`0x7FFFFF` ≈ 0 dB, clipped).

$$\text{Value} \approx 10^{\frac{\text{dB}}{20}} \times 2^{23}$$

*Note: The UI volume range is −120.4 dB (UI minimum, `0x000008`) to 0 dB. Volume 0 (`0x000000`) is hard mute. `0x000002` (−132.4 dB) is used exclusively as the store/apply duck level. Verified calibration points: 0 dB `7F FF FF`, −10 `28 7A 26`, −20 `0C CC CC`, −30 `04 0C 37`, −40 `01 47 AE`, −50 `00 67 9F`, −59.5 `00 22 B5`, −70 `00 0A 5C`, −80 `00 03 46`, −120.4 `00 00 08`, −132.4 `00 00 02`. The UI volume drag step is 0.5 dB. Values match `round(10^(dB/20) × 2²³)` exactly.*

## 5. Parametric EQ (PEQ) Filter Conversion

The speakers utilize a 20-band parametric EQ running at $48\text{ kHz}$. The PC/Hub calculates standard biquad coefficients based on Robert Bristow-Johnson (RBJ) Audio EQ Cookbook formulas and transfers them to the speaker. The DSP does not process Frequency/Gain/Q directly; it processes normalized feedforward and sign-inverted feedback coefficients.

* **Command Structure:** `10 0E [Index] [b0] [b1] [b2] [a1] [a2] [Type/Flag]`  
* **Index:** `0x00` to `0x13` (Filters 1 through 20).  
* **Coefficients:** Five 32-bit IEEE 754 floats in Little-Endian byte order (`b0`, `b1`, `b2`, `a1`, `a2`). Feedback coefficients `a1` and `a2` are normalized by $a_0$ and have their signs mathematically inverted.  
* **Type/Flag:** `0x00` for active custom filters.  
* **Bypassed/Flat/0.0dB Filters:** If a filter is disabled in the UI, or if a Peaking filter features a gain of exactly $0.0\text{ dB}$, the Hub applies an optimization shortcut. It transmits `b0` set to 1.0 (`0x00 0x00 0x80 0x3F`) and all remaining coefficients (`b1`, `b2`, `a1`, `a2`) set to 0.0 (`0x00 0x00 0x00 0x00`). The Type/Flag remains `0x00`.

## 6. DSP Level & Delay Configuration

These commands manage the settings typically found on the left side of the speaker management UI.

* **Time of Flight Delay:** `10 02 [4-byte Payload]`  
  * Sets the speaker alignment delay.  
  * Payload is 4 bytes. `00 00 00 00` represents 0.00 ms.  
  * Unit: 32-bit integer representing samples at 48 kHz (confirmed). (?)

* **Level Compensation / Boundaries:** `10 01 [Sub-Cmd] [3-byte Payload]`  
  * Sets the startup and maximum level restrictions.  
  * `00 [3-byte BE]`: Max Level Restriction. `7F FF FF` = 0.0 dB, `7A 39 57` = -0.6 dB. (Values use the §4 volume encoding; exact dB-to-value math follows $\text{Value} \approx 10^{\frac{\text{dB}}{20}} \times 2^{23}$.)
  * `05 [3-byte BE]`: Startup Level. `00 67 9F` = -50.0 dB, `00 22 B5` = -59.5 dB.  
  * `09 [3-byte BE]`: Unknown level sub-command. `00 00 00` observed during runtime. (Still TBD.)

## 7. Hub Commands (Hub → Speakers)

| Cmd Byte | Target Address | Payload Format | Description |
| --- | --- | --- | --- |
| **0x02** | `0x01F0` | `02 [ID1] [ID2] [ID3] [SessID]` | **Assign Session ID:** Instructs a newly discovered speaker to use `[SessID]`. |
| **0x04** | `0x01FF` | `04` (1-byte payload) | **Broadcast Sync / Preamble:** Sent before each configuration sub-burst (twice per speaker per push). (?) |
| **0x05** | Unicast | `05 [Subcmd] [Mask] 00 00 [Level24] 00 [Freq16] 00 02 DA` (14-byte payload) | **Signal Generator / Test Control:** |
| | | | • `04 [Mask] …`: Generator Control (Noise-type). `05 04 80` = Start Pink, `05 04 00` = Stop/Restore, `05 04 04` = Tweeter muted, `05 04 10` = Woofer muted, `05 04 14` = Both muted. Mask bits apply to **both generator and program audio**. |
| | | | • `08 [Mask] …`: Sine Wave Generator. Byte1 = `0x80` (Start), `04`/`10`/`14` masks as above. The generator **replaces** (not sums with) the program input at the volume-stage input. |
| | | | • `[Level24]`: two's-complement 24-bit field, `FF FF EA` (−22, stopped) vs `00 00 16` (+22, running) — semantics TBD. |
| | | | • `[Freq16]`: Big-Endian Hz at payload bytes 9–10. Calibration: `0014`=20, `0064`=100, `0190`=400 (pink), `03E8`=1000, `07D0`=2000, `0BB8`=3000, `0CE4`=3300, `0FA0`=4000 Hz. |
| | | | • Affects only the addressed speaker. Test-off commands are double-sent. Opening/closing the test UI emits an off-sync frame. Gen state changes are persisted (`09 06` markers). Meter response to mask (driver-gate) changes is ~1.5–2 s. |
| **0x08** | Unicast | `08` | **Telemetry Poll:** Requests a real-time telemetry frame from a speaker. No-data polls are answered with a bare 1-byte ACK (`09`); telemetry is sent ~1 Hz on change. No retry within the polling cycle. |
| **0x10** | Unicast | `10 [Subcmd] [Data...]` | **DSP Parameter Update:** |
| | | | • `0E [Idx] [5x Float32 LE] [Type]`: Set PEQ Filter (Note: feedback signs inverted) |
| | | | • `02 [4-byte Delay]`: Set Time of Flight Delay  
| | | | • `01 [Sub] [3-byte Level]`: Set Level Compensation / Boundaries |
| **0x15** | Unicast | `15 33 00` (3-byte payload) | **Commit to Flash:** Copies current RAM settings to non-volatile EEPROM/Flash. |
| **0x17** | Unicast | `17 01` | **Prepare for Configuration:** Sent before a block of DSP settings (firmware‑specific). (?) |
| **0x19** | Unicast | `19 01` | **Serial Number Request:** Requests the ASCII serial number. |
| **0x1F** | `0x01FF` | `1F [B3] [B2] [B1]` (3-byte payload) | **Set Volume:** 24-bit BE fixed-point linear gain (Scale factor $2^{23}$, see §4). Fire-and-forget (no ACK, no retry); also sent out-of-cadence immediately on changes. Also used for dim (app-computed `max(current − 20 dB, −120.4)`, sent as a plain volume frame — no dedicated dim command exists) and the store/apply duck (`00 00 02`, sometimes preceded by `00 00 08`). Fire-and-forget; sent ×2 on state changes. |
| **0x2B** | Unicast | `2B [03 / 04 / 08]` | **Set Mute / LED:** 03 = Mute ON, 04 = Restore Normal State / Unmute / LED ON, 08 = Disables LED indicator. Always two interleaved rounds to both speakers; the non-target speaker receives `04` state sync. Mute-all is preceded by a volume re-broadcast; individual mutes are not. LED commands are single-round with no `3D` companion. Mute state is invisible in telemetry flags (but floors output meters). |
| **0x2D** | Broadcast? | `2D 00 40` … | **Input Select / Configuration Preamble:** Acts as a broadcast preamble for input configuration alongside `0x3D`. Meaning of payload bytes remains unclear. (Confidence: ~50%) |
| **0x39** | Unicast | `39` | **System Info Request:** Requests the system info ASCII string. |
| **0x3A** | Broadcast/Multicast/Unicast | `3A [Subcmd] [Data...]` | **Set Standalone Settings / System Power:** (Confidence: ~100%) |
| | | | • `01 [4-byte BE]`: ISS Sleep Delay. Value in minutes (`02` = 2 min, `0A` = 10 min, `F0` = 240 min / 4 hours). `FF FF FF FF` = Never.  
| | | | • `02 [4-byte BE]`: ISS Sensitivity. `FF FF FF B5` = High, `FF FF FF BC` = Medium, `FF FF FF C3` = Low.  
| | | | • `06 [1-byte]`: LED State. `00` = LED On, `01` = LED Off.  
| | | | • `03 02`: System ON  
| | | | • `03 00`: System OFF  
| | | | • `03 7F / 03 01`: Unknown standalone power/standby trigger (?) |
| | | | • **Power-off sequence (observed, issued by the PC App on `0x01FF`):** `3A 03 02` ×2 followed by `3A 03 00` ×2, ~22 ms apart. Preempts the speakers' own ISS timer (fired at ~120.0 s after silence onset, twice, matching the stored 2-min delay; polls 3 ms prior still showed `47 01`). Speaker-side self-standby exists (user-confirmed) but its bus signature is uncaptured. Auto-restart is disabled by the double `03 00`. |
| **0x3B** | Unicast | `3B 00 01` | **Bass Management:** Sets full-band mode. Sent during standard configuration syncs and store sequences. (Confidence: ~60%) |
| **0x3D** | Unicast | `3D 00 00` | **Input Select Sync:** Sent as a post-configuration sync command to finalize input selection. (Confidence: ~60%) |
| **0x40** | Unicast | `40 00 [Input] 02 [Counter/ID]` | **Standalone Config Block:** Byte 2 is Input Select (`01` = Analog, `02` = Digital, `03` = Automatic). Byte 3 is always `02`. Byte 4 has remained `00` in all observed stores across sessions (any counter behavior is refuted). (Confidence: ~90%) |
| **0xFE** | `0x01FF` | `FE` | **Discovery Probe:** Broadcasts to find unassigned speakers. |

## 8. Speaker Responses (Speaker → Hub)

All speaker responses are sent from Address `0x0101`. The payload always begins with `0x09`. Sometimes a `0x06` wrapper/sequence byte follows the `0x09` header before the actual response data.

| Resp Byte(s) | Payload Format | Description |
| --- | --- | --- |
| **0x09** | `0x09 [SessID]` | **Session ID Acknowledge:** Confirms acceptance of a newly assigned Session ID. (Length: 2) |
| **0x09** | `0x09 [ID1] [ID2] [ID3]` | **Hardware ID Response:** Returns the 3-byte unique hardware ID in response to a Discovery Probe. (Length: 4) |
| **0x09** | `09` (1-byte payload) | **Generic ACK:** Payload is 1 byte. (CRC = `5D E7` for payload `09`.) Acknowledges configuration commands (PEQ — every frame —, Mute, Volume-related sync, Flash Commit, etc.). ACK latency < 1 ms. |
| **0x06** | `0x09 0x06` | **Settings Write Marker:** Returned when a settings-persistence operation is in progress — after `0x40`, `0x15`, generator on/off transitions and `05 04` commands (~0.5–2.2 s). Not triggered by `05 08` mask-only changes while running, nor by mute/volume operations. The `09 06 41 [telemetry]` wrapper form persists ~0.6–1.1 s after config pushes (flags `B6`). |
| **0x41** | `0x09 41 [Temp] [TLV Fields...]` | **Telemetry Response:** Returns real-time data. Frame is dynamically constructed with Tag-Value fields. `[Temp]` is main temperature in °C. Standby form: `09 41 [temp] 47 02 84 03 [byte]` (audio TLVs dropped). |
| **0x63** | `0x09 [NUL-terminated ASCII String]` | **System Info Response:** Returns firmware and model info. The `0x63` byte is most likely the first character of the string ("c-2;model-…"), not a type byte. Parse rule: `09 [NUL-terminated ASCII]`. (Confidence: ~65%) |
| **ASCII** | `0x09 [15-char ASCII serial, no NUL]` | **Serial Number Response:** Returns the serial number string (e.g. `8320apm2100xxxx`). (Confidence: ~65%) |

*Note: For `0x63` and ASCII Serial Number responses, the `[06]` byte previously documented is not present in recent logs; data immediately follows the `09` header.*

## 8.1 Telemetry TLV Field Definitions

The `0x41` Telemetry Response dynamically includes various Tag-Value fields depending on the speaker's state (e.g., audio metrics are omitted when powered off).  

*Example ON frame:* `09 41 26 81 00 24 83 00 24 42 E2 43 AF 45 CB 47 01 84 02 B0`  
*Example OFF frame:* `09 41 26 47 02 84 02 F0`

| Tag | Value Length | Value Description |
| --- | --- | --- |
| **`81`** | 2 bytes | `00 [Val]`: Independent slow sensor (see also `83`). Mirrors the temperature in steady state; diverges briefly after power transitions. Not a VU meter. |
| **`83`** | 2 bytes | `00 [Val]`: Independent slow sensor. Mirrors the temperature in steady state; diverges briefly after power transitions. Not a VU meter. |
| **`42`** | 1 byte | **Input Meter (pre-volume):** 8-bit unsigned meter of the signal entering the volume stage (program audio **or** active generator). Floor `0x89` (silence), ~`0xD2` (music at −50 dB). Not a per-driver VU. (Confidence: ~80%) |
| **`43`** | 1 byte | **HF-Channel Output Meter:** Post-volume, post-2B-mute, post-crossover, post-tweeter-gate. Floors at/below ~1–2 kHz input; rises with frequency. (Confidence: ~90%) |
| **`45`** | 1 byte | **LF-Channel Output Meter:** Post-volume, post-woofer-gate. Reads `0xA3` at 20 Hz (likely excursion-protection clamp). Moves independently of `43`; does not mirror it. (Confidence: ~70%) |
| **`47`** | 1 byte | **System Status:** `01` = Normal (ON), `02` = Standby (OFF). |
| **`84`** | 2 bytes | State Flags. First byte `02` (ON) / `03` (standby); second byte is an **opaque dynamic metric** that fluctuates with audio level and state changes — no stable bit assignment; do not decode as a fixed bitfield. Stable observations: values in `B0–BF` while ON; standby uses `84 03 [xx]`. (Confidence: ~30%) |

#### VU Meter Mapping (Tags 0x42, 0x43, 0x45)
All three meters are 8-bit unsigned absolute dBFS-style levels with ballistics and ~0.5–1 s reporting latency.

* **Floor:** `43`/`45` = `0x80`, `42` = `0x89`.
* **Slope (output meters 43/45):** ~1 dB per count (verified −19 to −83 dB). 
* **42 is pre-volume** — unchanged by master volume changes; tracks program content and generator level.
* Output meters are post-mute and post-driver-gate: 2B-mute floors them immediately; driver gates (mask 04/10) take ~1.5–2 s to appear.
* Transient caveat: ignore meter readings for ~1.5 s after any `05` state change (restart transients).
* The absolute offset is source-level-dependent; only the slope and floors are portable. Calibrate the display scale empirically. Example (music, −50 dB): `43` ≈ `0xB4–0xB6`.

To map the raw 8-bit values to a bar height for display, compute the level in dB above the floor (e.g. `dB = (raw − 0x80)` for output meters), choose a display window (e.g. 60 dB), and map linearly within the window. Clamp at both ends. (The previous piecewise pixel-mapping function is deprecated.)

## 9. Required State Machine & Logic Implementation

A custom Hub management application must implement the following state machine logic to manage the bus:

* **Discovery & Enumeration:**  
  * Hub periodically broadcasts `0x01FF 0xFE` (Discovery Probe).  
  * If an unconfigured speaker is present, it responds with `0x0101 0x09 [ID1] [ID2] [ID3]`.  
  * Hub assigns a Session ID by sending `0x01F0 0x02 [ID1] [ID2] [ID3] [SessID]`.  
  * Speaker acknowledges with `0x0101 0x09 [SessID]`.  
  * The Hub must maintain a registry mapping HW IDs to active Session IDs.  
  * Enumeration order per speaker: `04`, `05 04 00`, `17 01`, `04`, `10 02`, `10 01 00`, `10 01 09`, PEQ ×20, `3B`, `3D`, `40`, `2B 04`, `39`, `19 01`. PEQ frames are ACKed individually (< 1 ms); the sender paces PEQ bursts ~3 ms apart without blocking on ACKs.

* **System Power & Standalone Volume:**  
  * If the Hub is operating standalone (no PC app), it issues `0x01F0 0x3A 03 02` to power on speakers, and `0x3A 03 00` to power off.  
  * Standalone volume changes are broadcast via `0x01F0 0x1F [B3] [B2] [B1]` (24-bit linear gain).  
  * If a PC app is connected, it takes over and issues these same commands via `0x01FF`.  
  * To power off speakers, the hub issues `0x01F0 0x3A 03 02` twice, followed by `0x3A 03 00` twice. It seend so be a special sequence to turn off speakers and disable ISS. Issuing `0x3A 03 00` only once, turns the speakers off but they restart automatically if ISS is enabled and music is playing
  * To reliably wake on the speakers, the OEM hub issues the following sequence `0x01F0 0x3A 03 0x7F` `0x01F0 0x3A 03 01` `0x01F0 0x3A 03 02`
  * **Observed wake paths:** App connect / manual restart: `(3A 03 7F, 3A 03 01) × 3` on `1FF`, followed by FE re-discovery and full re-enumeration (~12–13 s). Apply/group routine: `3A 03 7F, 3A 03 7F, 3A 03 01` (no `02`). Speaker recovered via standby button: **no wake sequence at all** — FE discovery + enumeration alone restored it.
  * **Standalone boot ramp (reproducible):** `1F0 1F` `000000` → `000089` → `000200` → `000775` → `001BCC` → `00679F` (~610 ms steps, ×3.73 ≈ +11.4 dB/step), landing exactly on the stored startup level. The last value repeats at ~610 ms cadence while standalone. No FE/polls/wake occur during standalone.
  * **Hub/App arbitration:** While a PC app is connected, the hub continuously broadcasts `1F0 1F 00 00 00` (~610 ms); this value is a null/defer (speakers never mute on it — volume comes from `1FF`). When the app disconnects/closes, the hub continues `000000` for ~2.6 s, then takes over with its own volume (timeout ≈ 2.6 s). 
  * **ISS standby:** Speaker-side timer exists (user-confirmed). With the app connected, the app preempts it: it fires `1FF 3A 03 02`×2 + `3A 03 00`×2 at the stored delay after silence (observed at 120.0 s twice with 2-min stored). The speaker-side transition's bus signature is uncaptured.

* **Telemetry Polling Loop:**  
  * The Hub must continuously poll active speakers for telemetry using `0x01[SessID] 0x08`.  
  * The Hub must parse the `0x41` response dynamically, iterating through the TLV fields to extract Temperature, VU meters (dBFS), Status, and State Flags.  
  * If a `0x09 0x06` response is received instead, the Hub must recognize that a settings write is in progress and wait for normal telemetry to resume.  
  * Polls receiving no data are answered by a bare 1-byte ACK (`09`); telemetry is sent ~1 Hz, on change. Do not retry within the cycle. When a speaker dies (e.g. standby button), expect a rapid 4-frame retry burst (~9 ms apart) to the dead ID, then re-discovery.

* **Configuration Querying:**  
  * Upon enumeration, the Hub should query `0x19 01` and `0x39` to populate the UI/database with the speaker's Serial Number and System Info string.  
  * `0x63` System Info: parse as `09 [NUL-terminated ASCII]` (the `0x63` is the string's first character). Serial response is 15 ASCII chars, no NUL.

* **DSP & EQ Management:**  
  * The Hub must format and transmit the 20-band Parametric EQ using the `0x10 0x0E` command, correctly packing 5 IEEE 754 Little-Endian 32-bit floats per filter.  
  * The application must implement standard RBJ biquad calculations at $48\text{ kHz}$, invert the signs of $a_1$ and $a_2$, and replace any $0.0\text{ dB}$ peaking filters with the standard bypass vector (`1.0, 0.0, 0.0, 0.0, 0.0`).  
  * The Hub must manage Level Compensation (`0x10 0x01`) and Time of Flight Delay (`0x10 0x02`).  
  * PEQ frames are individually ACKed (< 1 ms latency); pace bursts ~3 ms/frame without blocking.

* **Mute, Solo & Dim:**  
  * Mute: unicast `2B 03`, always two interleaved rounds to both speakers; the non-target speaker gets `2B 04` state sync. Solo ≡ muting all speakers except the target. Mute state must be tracked in software — telemetry flags do not reflect it.  
  * Dim: no dedicated command — broadcast `1FF 1F [max(current − 20 dB, −120.4 dB)]` as a plain volume frame; dim-off/preset restores via an ordinary volume broadcast.

* **Flash Commit / Standalone Saving:** (Confidence: ~100%)  
  * To save settings to the speaker’s NVRAM, the Hub first writes the Level Boundaries (`0x10 0x01`), then the Standalone Settings (`0x3A`), followed by `0x40` (standalone config/input block), and any other active DSP configurations.  
  * The Hub then sends `0x15 33 00` to trigger the non-volatile memory burn.  
  * The speaker will immediately ACK the commit, but the physical flash write takes tens of milliseconds. The Hub should handle the abbreviated `0x09 0x06` telemetry response gracefully during this window.  
  * Confirmed store sequence (per speaker, L then R; volume ducked to `00 00 02` for ~2.2 s during the burn, with `04` preambles):  
    1. `0x10 01 00` (Max level restriction)  
    2. `0x10 01 05` (Startup level)  
    3. `0x3A 01` (ISS sleep delay)  
    4. `0x3A 02` (ISS sensitivity)  
    5. `0x3A 06` (LED state)  
    6. `0x40` (Input select / standalone config)  
    7. `0x15 33 00` (Commit to flash)

* **Apply / Group ("re-apply configuration"):**  
  1. Duck: `1FF 1F 00 00 08` then `00 00 02` (1 ms apart).  
  2. Wake: `1FF 3A 03 7F, 7F, 01`.  
  3. Per speaker: gen-off sync (`05 04 00`), `2B 04`, `40`.  
  4. `04 BC 84` preambles, then the full DSP block per speaker (`17 01`, `10 02`, `10 01 00`, `10 01 09`, PEQ ×20, `3B`, `3D`, `40`).  
  5. Restore volume (×5 over ~40 ms), `2B 04` + `3D` sync rounds.

* **Bus reliability notes:** Double-send redundancy for state changes is OEM practice (mute rounds ×2, test-off ×2) — recommended for noisy buses. Observed corruption (~1 frame per ~96 s on a long, noisy run) affected only the high-rate hub `1F0 1F 00 00 00` broadcast; CRC validation is essential on RX.

---

## Appendix: DSP Biquad Implementation Details & Optimization Tricks

### A.1 Mathematical Foundation

The speaker hardware processes incoming audio channels using a series of cascaded Second-Order Section (SOS) Direct Form I or Direct Form II biquad structures. The mathematical transfer function implemented in the frequency domain is defined as:

$$H(z) = \frac{b_0 + b_1 z^{-1} + b_2 z^{-2}}{a_0 + a_1 z^{-1} + a_2 z^{-2}}$$

When executed in real-time within the time domain, the standard difference equation follows this structure:

$$y[n] = \left(\frac{b_0}{a_0}\right)x[n] + \left(\frac{b_1}{a_0}\right)x[n-1] + \left(\frac{b_2}{a_0}\right)x[n-2] - \left(\frac{a_1}{a_0}\right)y[n-1] - \left(\frac{a_2}{a_0}\right)y[n-2]$$

### A.2 The Hardware Sign-Inversion Optimization

To enhance processing efficiency, the speaker's internal DSP architecture utilizes specialized Multiply-Accumulate (MAC) circuitry that performs cumulative additions faster than mixed additions and subtractions. To support this hardware shortcut, the speaker expects the feedback subtraction operations to be computed natively as additions:

$$y[n] = \hat{b}_0 x[n] + \hat{b}_1 x[n-1] + \hat{b}_2 x[n-2] + \hat{a}_1 y[n-1] + \hat{a}_2 y[n-2]$$

Consequently, before sending the data packet, the managing application must divide all variables by $a_0$ and **explicitly flip the signs of $a_1$ and $a_2$**. The bytes mapped to the network wire translate to:

$$\text{Payload } [b_0] = \frac{b_0}{a_0}, \quad \text{Payload } [b_1] = \frac{b_1}{a_0}, \quad \text{Payload } [b_2] = \frac{b_2}{a_0}$$

$$\text{Payload } [a_1] = -\frac{a_1}{a_0}, \quad \text{Payload } [a_2] = -\frac{a_2}{a_0}$$

### A.3 UI Parameter Generation Mechanics (RBJ Derivations)

All calculations assume an internal system sample rate ($f_s$) of **48,000 Hz**. Intermediate calculation terms rely on standard equations where $\omega_0 = 2\pi \frac{f_0}{f_s}$ and $A = 10^{\frac{\text{Gain dB}}{40}}$.

#### 1. Peaking EQ Filters ("Notch" in UI)

The UI labels manual corrective entries (Bands 5–20) as "Notches", but the underlying DSP engine computes them as standard Peaking/Bell EQ filters capable of handling both positive boost and negative cut values.

$$\alpha = \frac{\sin(\omega_0)}{2Q}$$

$$b_0 = 1 + \alpha A, \quad b_1 = -2\cos(\omega_0), \quad b_2 = 1 - \alpha A$$

$$a_0 = 1 + \frac{\alpha}{A}, \quad a_1 = -2\cos(\omega_0), \quad a_2 = 1 - \frac{\alpha}{A}$$

#### 2. Shelving Filters (Low Shelf & High Shelf)

The system UI conceals the Q/Slope parameter for the low and high shelving bands (Bands 1, 2, and 4). Analysis of the coefficient streams proves that the system implements these bands using a constant shelf slope parameter fixed at $S = 1.0$.

When setting $S = 1.0$, the standard cookbook definition for the shelf resonance variable $\alpha$ simplifies to a constant form:

$$\alpha = \frac{\sin(\omega_0)}{2}\sqrt{(A + \frac{1}{A})(\frac{1}{S} - 1) + 2} \implies \alpha = \frac{\sin(\omega_0)}{2}\sqrt{2}$$

* **Low Shelf Formulas:**

$$b_0 = A \left[ (A+1) - (A-1)\cos(\omega_0) + 2\sqrt{A}\alpha \right]$$

$$b_1 = 2A \left[ (A-1) - (A+1)\cos(\omega_0) \right]$$

$$b_2 = A \left[ (A+1) - (A-1)\cos(\omega_0) - 2\sqrt{A}\alpha \right]$$

$$a_0 = (A+1) + (A-1)\cos(\omega_0) + 2\sqrt{A}\alpha$$

$$a_1 = -2 \left[ (A-1) + (A+1)\cos(\omega_0) \right]$$

$$a_2 = (A+1) + (A-1)\cos(\omega_0) - 2\sqrt{A}\alpha$$

* **High Shelf Formulas:**

$$b_0 = A \left[ (A+1) + (A-1)\cos(\omega_0) + 2\sqrt{A}\alpha \right]$$

$$b_1 = -2A \left[ (A-1) + (A+1)\cos(\omega_0) \right]$$

$$b_2 = A \left[ (A+1) + (A-1)\cos(\omega_0) - 2\sqrt{A}\alpha \right]$$

$$a_0 = (A+1) - (A-1)\cos(\omega_0) + 2\sqrt{A}\alpha$$

$$a_1 = 2 \left[ (A-1) - (A+1)\cos(\omega_0) \right]$$

$$a_2 = (A+1) - (A-1)\cos(\omega_0) - 2\sqrt{A}\alpha$$

### A.4 Strategic Runtime Optimization Rules

#### 1. Zero-Gain Structural Bypass Instantiation

When a Peaking EQ band configuration calculates to exactly $0.0\text{ dB}$, its frequency response forms an absolute flat identity line ($H(z) = 1.0$). Rather than forcing the speaker to continuously compute complex mathematical multiplication operations that produce no acoustic change, the Hub overrides the calculated variables under the hood. It transmits a clean structural bypass array:

$$\text{Payload Float Bytes} \longrightarrow [b_0 = 1.0],\, [b_1 = 0.0],\, [b_2 = 0.0],\, [a_1 = 0.0],\, [a_2 = 0.0]$$

This matches the exact byte sequence transmitted to explicitly deactivated or bypassed filter elements, allowing the hardware to skip execution logic for that section entirely.

#### 2. Escape Processing Ordering Rules

Because the floating-point representations of calculated biquad parameters can randomly match framing tokens, the communication software must stringently apply escape processing constraints in the correct sequence.

* **On Transmission (TX):** The raw 32-bit floats must be fully derived, sign-flipped, and appended alongside the frame index. The complete packet CRC must then be calculated over this raw unescaped sequence. Only *after* the final CRC value has been appended can the escaping process scan the stream to turn instances of `0x7E` and `0x7D` into their respective double-byte sequences.  
* **On Reception (RX):** The hardware receiving the array must immediately strip away the extra escaping bytes (`0x7D 0x5E` $\rightarrow$ `0x7E`) before routing the buffer to the validation algorithm to run the incoming CRC comparison block.