*This file is part of the Homebrew Loudspeaker Manager distribution (https://github.com/robcazzaro/hlm).<br>
Copyright (c) 2026 Rob Cazzaro.*<br><br><br>

# Genelec GLM Telemetry & Control Protocol Specification

**Revision:** 2026 v12 (Cross‑validated against [espgensam](https://github.com/markbergsma/espgensam) at `968fdc4`. Adds subwoofer device class, multi‑rate PEQ design, corrected telemetry tag assignments, revised power‑sequence semantics, multi‑group configurations, and several newly‑identified opcodes. Low‑confidence items flagged for future testing.)

*Note: Certain commands and parameters in this document are marked with a confidence level less than 90% and might require updates.*
<br><br>

## 1. Physical & Data Link Layer

* **Medium:** RS‑485 Half‑Duplex Serial.  
* **UART Config:** 9-bit UART (no parity, **2 stop bits**). The 9th bit is set to 1 for the first byte of every frame (the Address byte), and 0 for all subsequent payload, CRC, and flag bytes.  
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
* **Floating Point:** DSP filter coefficients use 32-bit IEEE 754 format in Little-Endian byte order.  
  * **Coefficient design rate is device-class and block specific:**
    * **Two-way monitors** (8320A, 8330A, etc.): device PEQ designed at **48,000 Hz** (confirmed).
    * **Subwoofers** (7350A, etc.): device PEQ designed at **12,000 Hz** (confirmed from OEM config file).
    * **Group-level LP/HP shelves**: designed at **96,000 Hz** (host-side only; not transmitted as `10 0E` frames — the hub/GLM adapter applies them).
    * **Higher-end monitors** (83x1 series and similar) are reported to run a 96 kHz DSP path. Whether their transmitted PEQ coefficients are designed at 48 kHz and converted internally, or designed at 96 kHz directly, is **unresolved** pending a capture from one of those models. Do not assume the two-way rate generalises upward.
* **Volume (Fixed-Point):** 24-bit unsigned integer representing linear gain. Scale factor is $2^{23}$ (`0x7FFFFF` ≈ 0 dB, clipped).

$$\text{Value} \approx 10^{\frac{\text{dB}}{20}} \times 2^{23}$$

*Note: The UI volume range is −120.4 dB (UI minimum, `0x000008`) to 0 dB. Volume 0 (`0x000000`) is hard mute. `0x000002` (−132.4 dB) is used exclusively as the store/apply duck level. Verified calibration points: 0 dB `7F FF FF`, −10 `28 7A 26`, −20 `0C CC CC`, −30 `04 0C 37`, −40 `01 47 AE`, −50 `00 67 9F`, −59.5 `00 22 B5`, −70 `00 0A 5C`, −80 `00 03 46`, −120.4 `00 00 08`, −132.4 `00 00 02`. The UI volume drag step is 0.5 dB. Values match `round(10^(dB/20) × 2²³)` exactly.*

## 5. Parametric EQ (PEQ) Filter Conversion

The speakers utilise a 20-band parametric EQ. The PC/Hub calculates standard biquad coefficients based on Robert Bristow-Johnson (RBJ) Audio EQ Cookbook formulas and transfers them to the speaker. The DSP does not process Frequency/Gain/Q directly; it processes normalized feedforward and sign-inverted feedback coefficients.

* **Command Structure:** `10 0E [Index] [b0] [b1] [b2] [a1] [a2] [Type/Flag]`  
* **Index:** `0x00` to `0x13` (Filters 1 through 20).  
* **Coefficients:** Five 32-bit IEEE 754 floats in Little-Endian byte order (`b0`, `b1`, `b2`, `a1`, `a2`). Feedback coefficients `a1` and `a2` are normalized by $a_0$ and have their signs mathematically inverted.  
* **Type/Flag:** `0x00` for active custom filters.  
* **Bypassed/Flat/0.0dB Filters:** If a filter is disabled in the UI, or if a Peaking filter features a gain of exactly $0.0\text{ dB}$, the Hub applies an optimization shortcut. It transmits `b0` set to 1.0 (`0x00 0x00 0x80 0x3F`) and all remaining coefficients (`b1`, `b2`, `a1`, `a2`) set to 0.0 (`0x00 0x00 0x00 0x00`). The Type/Flag remains `0x00`.

* **Design rate (per §4):** Two-way monitors use 48 kHz; subwoofers use 12 kHz. The bypass shortcut is rate-independent. The RBJ formulas in the Appendix must be evaluated at the device-class-specific rate — using the wrong rate shifts the filter centre frequencies by the ratio of the rates (e.g. a 12 kHz design evaluated at 48 kHz puts a 40 Hz filter at 160 Hz).

## 6. DSP Level & Delay Configuration

These commands manage the settings typically found on the left side of the speaker management UI.

* **Time of Flight Delay:** `10 02 [4-byte Payload]`  
  * Sets the speaker alignment delay.  
  * Payload is 4 bytes. `00 00 00 00` represents 0.00 ms.  
  * Unit: 32-bit integer representing samples at 48 kHz. (Confirmed for two-way monitors: `00 00 01 64` = 356 samples ≈ 7.42 ms observed on a 7350A subwoofer.)  
  * *(For subwoofers, verify whether the sample-count unit is at 12 kHz or 48 kHz — the 7350A's 356-sample value produces a different physical delay depending on the rate.)*

* **Level Compensation / Boundaries:** `10 01 [Sub-Cmd] [3-byte Payload]`  
  * Sets the startup and maximum level restrictions.  
  * `00 [3-byte BE]`: Max Level Restriction. `7F FF FF` = 0.0 dB, `7A 39 57` = -0.6 dB. Subwoofers have been observed at `74 8C 42` ≈ −0.8 dB. (Values use the §4 volume encoding; exact dB-to-value math follows $\text{Value} \approx 10^{\frac{\text{dB}}{20}} \times 2^{23}$.)  
  * `05 [3-byte BE]`: Startup Level. `00 67 9F` = -50.0 dB, `00 22 B5` = -59.5 dB.  
  * `09 [3-byte BE]`: Unknown level sub-command. `00 00 00` observed during runtime. (Still TBD.)

* **Subwoofer-specific fields** (from `SubwooferGen2` config, not yet observed on the wire): `Phase`, `PhaseCalibratedWith`, `LFE_+10`, `LFE_Channel`, `LFE_CrossoverFrequency`, `SubwooferGroupID`. These are stored in the OEM config and transmitted to the subwoofer during configuration, but the encoding is currently unmapped. Flagged for future capture.

* **Multi-Group Configurations:** A single setup may contain more than one group (e.g. an `Analog` group and an `AES3` group), each with its own input selection, crossover, and per-device input routing. The hub must maintain per-group state and re-apply it when the active group changes.

## 7. Hub Commands (Hub → Speakers)

| Cmd Byte | Target Address | Payload Format | Description |
| --- | --- | --- | --- |
| **0x02** | `0x01F0` | `02 [ID1] [ID2] [ID3] [SessID]` | **Assign Session ID:** Instructs a newly discovered speaker to use `[SessID]`. |
| **0x04** | `0x01FF` | `04` (1-byte payload) | **Keep-Alive / Online Refresh:** Broadcast once per poll cycle to refresh address leases. In OEM traffic it appears at a steady 1.2–1.8 s cadence, immediately after the `0x1F` volume broadcast and immediately before the `0x08` poll round. The `BC 84` payload that appears in some captures is a device-specific variant and is not required. (Confidence: ~90%) |
| **0x05** | Unicast | `05 [Subcmd] [Mask] 00 00 [Level24] 00 [Freq16] 00 02 DA` (14-byte payload) | **Signal Generator / Test Control:** |
| | | | • `04 [Mask] …`: Generator Control (Noise-type). `05 04 80` = Start Pink, `05 04 00` = Stop/Restore, `05 04 04` = Tweeter muted, `05 04 10` = Woofer muted, `05 04 14` = Both muted. Mask bits apply to **both generator and program audio**. |
| | | | • `08 [Mask] …`: Sine Wave Generator. Byte1 = `0x80` (Start), `04`/`10`/`14` masks as above. The generator **replaces** (not sums with) the program input at the volume-stage input. |
| | | | • `[Level24]`: two's-complement 24-bit field, `FF FF EA` (−22, stopped) vs `00 00 16` (+22, running) — semantics TBD. |
| | | | • `[Freq16]`: Big-Endian Hz at payload bytes 9–10. Calibration: `0014`=20, `0064`=100, `0190`=400 (pink), `03E8`=1000, `07D0`=2000, `0BB8`=3000, `0CE4`=3300, `0FA0`=4000 Hz. |
| | | | • Affects only the addressed speaker. Test-off commands are double-sent. Opening/closing the test UI emits an off-sync frame. Gen state changes are persisted (`09 06` markers). Meter response to mask (driver-gate) changes is ~1.5–2 s. |
| **0x08** | Unicast | `08` | **Telemetry Poll:** Requests a real-time telemetry frame from a speaker. No-data polls are answered with a bare 1-byte ACK (`09`); telemetry is sent ~1 Hz on change. No retry within the polling cycle. |
| **0x10** | Unicast | `10 [Subcmd] [Data...]` | **DSP Parameter Update:** |
| | | | • `0E [Idx] [5x Float32 LE] [Type]`: Set PEQ Filter (Note: feedback signs inverted). Design rate is device-class-specific — see §4/§5. |
| | | | • `02 [4-byte Delay]`: Set Time of Flight Delay   |
| | | | • `01 [Sub] [3-byte Level]`: Set Level Compensation / Boundaries |
| **0x15** | Unicast | `15 33 00` (3-byte payload) | **Commit to Flash:** Copies current RAM settings to non-volatile EEPROM/Flash. |
| **0x17** | Unicast | `17 01` | **Prepare for Configuration:** Sent before a block of DSP settings (firmware‑specific). (?) |
| **0x19** | Unicast | `19 01` | **Serial Number Request:** Requests the ASCII serial number. |
| **0x1F** | `0x01FF` | `1F [B3] [B2] [B1]` (3-byte payload) | **Set Volume:** 24-bit BE fixed-point linear gain (Scale factor $2^{23}$, see §4). Fire-and-forget (no ACK, no retry); also sent out-of-cadence immediately on changes. Also used for dim (app-computed `max(current − 20 dB, −120.4)`, sent as a plain volume frame — no dedicated dim command exists) and the store/apply duck (`00 00 02`, sometimes preceded by `00 00 08`). Fire-and-forget; sent ×2 on state changes. |
| **0x2B** | Unicast | `2B [03 / 04 / 08]` | **Set Mute / LED:** `03` = Mute ON, `04` = Restore Normal State / Unmute / LED ON, `08` = Disables LED indicator. Always two interleaved rounds to both speakers; the non-target speaker receives `04` state sync. Mute-all is preceded by a volume re-broadcast; individual mutes are not. LED commands are single-round with no `3D` companion. Mute state is invisible in telemetry flags (but floors output meters). |
| | | | **Bitfield model (unverified):** bit 0 mute, bits 1–2 LED colour (`0`=green, `1`=red, `2`=off, `3`=yellow), bit 3 pulsing, bit 4 invert. This conflicts with the value-enum reading on the LED colour bits (`04` = "unmuted + LED off" under the bitfield model, but has been observed as "LED on" in practice). The captures contain only `2B 04`, so the question is **unresolved** and testable in minutes by sending `2B 00/02/04/06/08` to one monitor and recording the front LED behaviour. |
| **0x2D** | Broadcast/Multicast | `2D 00 40` … | **Input Select / Configuration Preamble:** Acts as a broadcast preamble for input configuration alongside `0x3D`. Sent on `0x1FF` as well as unicast. Meaning of payload bytes remains unclear. (Confidence: ~50%) |
| **0x39** | Unicast | `39` | **System Info Request:** Requests the system info ASCII string. |
| **0x3A** | Broadcast/Multicast/Unicast | `3A [Subcmd] [Data...]` | **Set Standalone Settings / System Power:** (Confidence: ~100%) |
| | | | • `01 [4-byte BE]`: ISS Sleep Delay. Value in minutes (`02` = 2 min, `0A` = 10 min, `F0` = 240 min / 4 hours). `FF FF FF FF` = Never.  
| | | | • `02 [4-byte BE]`: ISS Sensitivity. `FF FF FF B5` = High, `FF FF FF BC` = Medium, `FF FF FF C3` = Low.  
| | | | • `06 [1-byte]`: LED State. `00` = LED On, `01` = LED Off.  
| | | | • `03 02`: **Standby prepare (phase 1)** — first half of the power-off sequence.  
| | | | • `03 00`: **Power off (phase 2)** — second half of the power-off sequence.  
| | | | • `03 7F`, `03 01`: **Wake trigger** — emitted as ×3 pairs, with no trailing `03 02`.  
| | | | • **Power-off sequence (observed):** `3A 03 02` ×2 followed by `3A 03 00` ×2, ~22 ms apart. Preempts the speakers' own ISS timer. Auto-restart is disabled by the double `03 00`.  
| | | | • **Wake sequence (observed):** `3A 03 7F` / `3A 03 01` ×3, on `0x1FF`. After the wake, discovery and full re-enumeration typically complete in 12–13 s.  
| | | | • *Note on earlier labelling:* previous revisions of this spec labelled `03 02` as "System ON". That label contradicts the observed power-off sequence, which uses `03 02` as the first half of a shutdown. The "standby prepare" reading is self-consistent with the observed captures and is adopted here. |
| **0x3B** | Unicast | `3B [Value16]` | **Bass Management / Crossover:** Big-endian 16-bit value. Values in the 50–120 range encode crossover frequency in Hz (`00 5A` = 90 Hz observed; min/max/step 50/120/5). Small values (e.g. `00 01`) act as mode selectors — full-band / bass management off. Sent during standard configuration syncs and store sequences. (Confidence: ~75%) |
| **0x3C** | Unicast | `3C 00 00` | **Unknown (subwoofer-only):** Observed only on a 7350A during enumeration. Not in any public spec. (Confidence: ~20%) |
| **0x3D** | Unicast | `3D 00 00` | **Input Select Sync:** Sent as a post-configuration sync command to finalize input selection. (Confidence: ~60%) |
| **0x3E** | Unicast | `3E 00 00` | **Unknown (subwoofer-only):** Observed only on a 7350A during enumeration. Not in any public spec. (Confidence: ~20%) |
| **0x40** | Unicast | `40 [input_idx] [source] [pair_selector] [aes3_channel]` | **Standalone Config Block / Input Routing:** (Confidence: ~90%) |
| | | | • `input_idx`: `00` or `01` — primary or secondary input on subwoofers. Two-way monitors use `00`.  
| | | | • `source`: `01` = Analog, `02` = Digital (AES3), `03` = Automatic.  
| | | | • `pair_selector`: pair selection / input pairing.  
| | | | • `aes3_channel`: `00` = none, `01` = A, `02` = B, `03` = A+B sum.  
| | | | Observed payload variants: `[00 01 02 00]`, `[01 01 01 00]`, `[00 02 00 01]`, `[01 02 00 00]`, `[00 02 00 02]`, `[00 02 00 03]`. |
| **0x42** | Unicast | `42 00 00` | **Unknown (subwoofer-only):** Observed only on a 7350A during enumeration. Not in any public spec. (Confidence: ~20%) |
| **0xFE** | `0x01FF` | `FE` | **Discovery Probe:** Broadcasts to find unassigned speakers. |

## 8. Speaker Responses (Speaker → Hub)

All speaker responses are sent from Address `0x0101`. The payload always begins with `0x09`. The reply may optionally include a **one-byte busy / sequence marker** after `09` before the actual response data. Both `0x06` and `0x07` have been observed in this position. Neither indicates standby: frames carrying tag `47 01` (active) are frequently prefixed with `0x07`, and appear in a single poll round shortly after a configuration burst. The `47` tag is the sole authority on power state. A bare `09 06` / `09 07` (no TLV stream) means "no telemetry this round".

| Resp Byte(s) | Payload Format | Description |
| --- | --- | --- |
| **0x09** | `0x09 [SessID]` | **Session ID Acknowledge:** Confirms acceptance of a newly assigned Session ID. (Length: 2) |
| **0x09** | `0x09 [ID1] [ID2] [ID3]` | **Hardware ID Response:** Returns the 3-byte unique hardware ID in response to a Discovery Probe. (Length: 4) |
| **0x09** | `09` (1-byte payload) | **Generic ACK:** Payload is 1 byte. (CRC = `5D E7` for payload `09`.) Acknowledges configuration commands (PEQ — every frame —, Mute, Volume-related sync, Flash Commit, etc.). ACK latency < 1 ms. |
| **0x06 / 0x07** | `0x09 [0x06 or 0x07]` | **Busy / Sequence Marker:** Returned when a settings-persistence operation is in progress — after `0x40`, `0x15`, generator on/off transitions and `05 04` commands (~0.5–2.2 s). The `09 06 41 [telemetry]` wrapper form persists ~0.6–1.1 s after config pushes. `0x07` is a distinct value of the same field and has been observed on active frames; treat the two identically. |
| **0x41** | `0x09 [marker?] 41 [Temp] [TLV Fields...]` | **Telemetry Response:** Returns real-time data. Frame is dynamically constructed with Tag-Value fields. `[Temp]` is main temperature in °C. Standby form: `09 41 [temp] 47 02 84 03 [byte]` (audio TLVs dropped). |
| **0x63** | `0x09 [NUL-terminated ASCII String]` | **System Info Response:** Returns firmware and model info. Two observed formats: `c-1;model-7350A;ver-…;hw-…;build-…` and space-delimited `7350A 1 0000 0106 3733`. Parse rule: `09 [NUL-terminated ASCII]`. (Confidence: ~70%) |
| **ASCII** | `0x09 [15-char ASCII serial, no NUL]` | **Serial Number Response:** Returns the serial number string (e.g. `8320apm2100xxx`). (Confidence: ~65%) |

*Note: For `0x63` and ASCII Serial Number responses, the `[06]` byte previously documented is not present in recent logs; data immediately follows the `09` header.*

## 8.1 Telemetry TLV Field Definitions

The `0x41` Telemetry Response dynamically includes various Tag-Value fields depending on the speaker's state (e.g., audio metrics are omitted when powered off) and on the **device class** (subwoofers emit different tags from two-way monitors).  

*Example ON frame (two-way):* `09 41 26 81 00 24 83 00 24 42 E2 43 AF 45 CB 47 01 84 02 B0`  
*Example ON frame (subwoofer):* `09 41 26 83 00 26 42 94 46 93 43 80 45 92 47 01 84 01 66`  
*Example OFF frame:* `09 41 26 47 02 84 02 F0`

| Tag | Value Length | Value Description |
| --- | --- | --- |
| **`42`** | 1 byte | **Input Meter (pre-volume):** 8-bit unsigned meter of the signal entering the volume stage (program audio **or** active generator). Floor `0x89` (silence), ~`0xD2` (music at −50 dB). Not a per-driver VU. (Confidence: ~80%) |
| **`43`** | 1 byte | **HF-Channel Output Meter:** Post-volume, post-mute, post-crossover, post-tweeter-gate. Floors at/below ~1–2 kHz input; rises with frequency. **Permanently floored (`0x80`) on subwoofers**, which have no HF driver. (Confidence: ~95%) |
| **`44`** | 1 byte | **Midrange Output Meter:** Anticipated for three-way models. Not observed in any capture to date. (Confidence: ~30%) |
| **`45`** | 1 byte | **LF-Channel Output Meter:** Post-volume, post-woofer-gate. Reads `0xA3` at 20 Hz (likely excursion-protection clamp). Active on subwoofers; drives the subwoofer VU. (Confidence: ~85%) |
| **`46`** | 1 byte | **Subwoofer Driver Meter:** Observed only on the 7350A. Tracks the subwoofer's own output and is distinct from `45`. Not emitted by two-way monitors. (Confidence: ~85%) |
| **`47`** | 1 byte | **System Status:** `01` = Normal (ON), `02` = Standby (OFF). **This is the sole authority on power state.** |
| **`81`** | 2 bytes | `00 [Val]`: Independent slow sensor. Mirrors the temperature in steady state; diverges briefly after power transitions. Not a VU meter. Emitted by two-way monitors. |
| **`83`** | 2 bytes | `00 [Val]`: Independent slow sensor. Mirrors the temperature in steady state; diverges briefly after power transitions. Not a VU meter. Emitted by all device classes. |
| **`84`** | 2 bytes | **State Flags:** First byte is a **device-class field**: `01` = subwoofer, `02` = two-way monitor, `03` = standby (observed by one project only). Second byte is an opaque dynamic metric that fluctuates with audio level and state changes. Stable observations: values in `B0–BF` while ON; standby uses `84 03 [xx]`. (Confidence: first-byte class mapping ~90%; second-byte semantics ~30%.) |

#### VU Meter Mapping (Tags 0x42, 0x43, 0x45, 0x46)

All meters are 8-bit unsigned absolute dBFS-style levels with ballistics and ~0.5–1 s reporting latency.

* **Floor:** `43`/`45`/`46` = `0x80`, `42` = `0x89`.
* **Slope (output meters):** ~1 dB per count (verified −19 to −83 dB). 
* **42 is pre-volume** — unchanged by master volume changes; tracks program content and generator level.
* Output meters are post-mute and post-driver-gate: 2B-mute floors them immediately; driver gates (mask 04/10) take ~1.5–2 s to appear.
* Transient caveat: ignore meter readings for ~1.5 s after any `05` state change (restart transients).
* The absolute offset is source-level-dependent; only the slope and floors are portable. Calibrate the display scale empirically.

To map the raw 8-bit values to a bar height for display, compute the level in dB above the floor (e.g. `dB = (raw − 0x80)` for output meters), choose a display window (e.g. 60 dB), and map linearly within the window. Clamp at both ends.

## 9. Required State Machine & Logic Implementation

* **Discovery & Enumeration:**  
  * Hub periodically broadcasts `0x01FF 0xFE` (Discovery Probe).  
  * If an unconfigured speaker is present, it responds with `0x0101 0x09 [ID1] [ID2] [ID3]`.  
  * Hub assigns a Session ID by sending `0x01F0 0x02 [ID1] [ID2] [ID3] [SessID]`.  
  * Speaker acknowledges with `0x0101 0x09 [SessID]`.  
  * The Hub must maintain a registry mapping HW IDs to active Session IDs, and a **device-class registry** (subwoofer / two-way / standby) derived from the `84` first byte once telemetry is available.  
  * Enumeration order per speaker: `04`, `05 04 00`, `17 01`, `04`, `10 02`, `10 01 00`, `10 01 09`, PEQ ×20, `3B`, `3D`, `40`, `2B 04`, `39`, `19 01`. Subwoofers may additionally emit `3C`, `3E`, `42`. PEQ frames are ACKed individually (< 1 ms); the sender paces PEQ bursts ~3 ms apart without blocking on ACKs.  

* **Multi-Group Operation:** A single setup may contain multiple groups with distinct input selections, crossovers, and per-device routing. The hub must maintain per-group state and re-apply the active group's settings when the active group changes.

* **System Power & Standalone Volume:**  
  * If the Hub is operating standalone (no PC app), it issues `0x01F0 0x3A 03 02` to **prepare for standby**, and `0x3A 03 00` to complete the power-off.  
  * Standalone volume changes are broadcast via `0x01F0 0x1F [B3] [B2] [B1]` (24-bit linear gain).  
  * If a PC app is connected, it takes over and issues these same commands via `0x01FF`.  
  * To power off speakers, the hub issues `0x01F0 0x3A 03 02` twice, followed by `0x3A 03 00` twice. The double sequence disables ISS auto-restart. Issuing `0x3A 03 00` only once turns the speakers off but they restart automatically if ISS is enabled and music is playing.  
  * To reliably wake the speakers, issue the sequence `0x01F0 0x3A 03 0x7F` / `0x01F0 0x3A 03 01` ×3. There is no trailing `03 02`.  
  * **Observed wake paths:** App connect / manual restart: `(3A 03 7F, 3A 03 01) × 3` on `1FF`, followed by FE re-discovery and full re-enumeration (~12–13 s). Apply/group routine: `3A 03 7F, 3A 03 7F, 3A 03 01` (no `02`). Speaker recovered via standby button: **no wake sequence at all** — FE discovery + enumeration alone restored it.  
  * **Standalone boot ramp (reproducible):** `1F0 1F` `000000` → `000089` → `000200` → `000775` → `001BCC` → `00679F` (~610 ms steps, ×3.73 ≈ +11.4 dB/step), landing exactly on the stored startup level. The last value repeats at ~610 ms cadence while standalone.  
  * **Hub/App arbitration:** While a PC app is connected, the hub continuously broadcasts `1F0 1F 00 00 00` (~610 ms); this value is a null/defer (speakers never mute on it — volume comes from `1FF`). When the app disconnects/closes, the hub continues `000000` for ~2.6 s, then takes over with its own volume (timeout ≈ 2.6 s).   
  * **ISS standby:** Speaker-side timer exists (user-confirmed). With the app connected, the app preempts it: it fires `1FF 3A 03 02`×2 + `3A 03 00`×2 at the stored delay after silence. The speaker-side transition's bus signature is uncaptured.  

* **External Master Detection & Yielding (informative, from third-party capture):** When a bus is shared with an OEM GLM adapter, a custom hub may detect foreign traffic (e.g. the recurring `F0 1F 00 00 00` heartbeat) and yield the bus rather than contending. A yield-and-watch strategy that stops transmitting and only snoops volume / mute / power / addressing is safe; interleaving or deferral is not required. This is not part of the OEM protocol, but is worth documenting as a coexistence pattern.

* **Telemetry Polling Loop:**  
  * The Hub must continuously poll active speakers for telemetry using `0x01[SessID] 0x08`.  
  * The Hub must parse the `0x41` response dynamically, iterating through the TLV fields to extract Temperature, VU meters (dBFS), Status, and State Flags. The optional one-byte `0x06` / `0x07` busy marker must be skipped before the TLV walk.  
  * If a `0x09 0x06` response is received instead, the Hub must recognize that a settings write is in progress and wait for normal telemetry to resume.  
  * Polls receiving no data are answered by a bare 1-byte ACK (`09`); telemetry is sent ~1 Hz, on change. Do not retry within the cycle. When a speaker dies (e.g. standby button), expect a rapid 4-frame retry burst (~9 ms apart) to the dead ID, then re-discovery.  

* **Configuration Querying:**  
  * Upon enumeration, the Hub should query `0x19 01` and `0x39` to populate the UI/database with the speaker's Serial Number and System Info string.  
  * `0x63` System Info: parse as `09 [NUL-terminated ASCII]`. Two forms observed: `c-1;model-…;ver-…;hw-…;build-…` and space-delimited. Serial response is 15 ASCII chars, no NUL.  

* **DSP & EQ Management:**  
  * The Hub must format and transmit the 20-band Parametric EQ using the `0x10 0x0E` command, correctly packing 5 IEEE 754 Little-Endian 32-bit floats per filter.  
  * The application must implement standard RBJ biquad calculations **at the device-class-specific rate** (48 kHz for two-way monitors, 12 kHz for subwoofers), invert the signs of $a_1$ and $a_2$, and replace any $0.0\text{ dB}$ peaking filters with the standard bypass vector (`1.0, 0.0, 0.0, 0.0, 0.0`).  
  * The Hub must manage Level Compensation (`0x10 0x01`) and Time of Flight Delay (`0x10 0x02`).  
  * PEQ frames are individually ACKed (< 1 ms latency); pace bursts ~3 ms/frame without blocking.  

* **Mute, Solo & Dim:**  
  * Mute: unicast `2B 03`, always two interleaved rounds to both speakers; the non-target speaker gets `2B 04` state sync. Solo ≡ muting all speakers except the target. Mute state must be tracked in software — telemetry flags do not reflect it.  
  * Dim: no dedicated command — broadcast `1FF 1F [max(current − 20 dB, −120.4 dB)]` as a plain volume frame; dim-off/preset restores via an ordinary volume broadcast.  

* **Flash Commit / Standalone Saving:** (Confidence: ~100%)  
  * To save settings to the speaker's NVRAM, the Hub first writes the Level Boundaries (`0x10 0x01`), then the Standalone Settings (`0x3A`), followed by `0x40` (standalone config/input block), and any other active DSP configurations.  
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
  * **Store/apply duck:** The OEM issues `1FF 1F 00 00 08` followed 1 ms later by `1FF 1F 00 00 02` as the duck prelude. Both values appear in captures; `00 00 02` is the −132.4 dB silence applied for the duration of the burn.

* **Apply / Group ("re-apply configuration"):**  
  1. Duck: `1FF 1F 00 00 08` then `00 00 02` (1 ms apart).  
  2. Wake: `1FF 3A 03 7F, 7F, 01`.  
  3. Per speaker: gen-off sync (`05 04 00`), `2B 04`, `40`.  
  4. `04 BC 84` preambles, then the full DSP block per speaker (`17 01`, `10 02`, `10 01 00`, `10 01 09`, PEQ ×20, `3B`, `3D`, `40`).  
  5. Restore volume (×5 over ~40 ms), `2B 04` + `3D` sync rounds.  

* **Bus reliability notes:** Double-send redundancy for state changes is OEM practice (mute rounds ×2, test-off ×2) — recommended for noisy buses. CRC validation is essential on RX.

---

## Appendix A: DSP Biquad Implementation Details & Optimization Tricks

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

All calculations assume the device-class-specific design rate $f_s$ (see §4): **48,000 Hz** for two-way monitors, **12,000 Hz** for subwoofers. Intermediate calculation terms rely on standard equations where $\omega_0 = 2\pi \frac{f_0}{f_s}$ and $A = 10^{\frac{\text{Gain dB}}{40}}$.

> **Rate matters.** The same physical filter (e.g. a 40 Hz notch) produces very different coefficients at 12 kHz vs 48 kHz. Feeding a 48 kHz-designed coefficient set to a device expecting 12 kHz shifts every filter up by 4×. Always design at the target device's rate.

#### 1. Peaking EQ Filters ("Notch" in UI)

The UI labels manual corrective entries (Bands 5–20) as "Notches", but the underlying DSP engine computes them as standard Peaking/Bell EQ filters capable of handling both positive boost and negative cut values.

$$\alpha = \frac{\sin(\omega_0)}{2Q}$$

$$b_0 = 1 + \alpha A, \quad b_1 = -2\cos(\omega_0), \quad b_2 = 1 - \alpha A$$

$$a_0 = 1 + \frac{\alpha}{A}, \quad a_1 = -2\cos(\omega_0), \quad a_2 = 1 - \frac{\alpha}{A}$$

#### 2. Shelving Filters (Low Shelf & High Shelf)

The system UI conceals the Q/Slope parameter for the low and high shelving bands. Analysis of the coefficient streams proves that the system implements these bands using a constant shelf slope parameter fixed at $S = 1.0$.

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

---

## Appendix B: Cross-Validation & Known Divergences

This specification has been cross-validated against an independent reverse-engineering effort, [espgensam](https://github.com/markbergsma/espgensam), which produced a second capture set (15.7k frames, 7350A sub + two 8330A). The two efforts agree on the load-bearing parts of the protocol (wire format, CRC, addressing, discovery, volume, PEQ, telemetry). The following items remain **open** and are documented here so future captures can settle them.

1. **PEQ design rate on higher-end monitors.** The 83x1 series and similar are reported to run a 96 kHz DSP path. Whether the hub transmits coefficients designed at 48 kHz and converts internally, or designs them at 96 kHz directly, is unresolved. Capture a configuration push from GLM driving an 83x1 model and compare the transmitted `10 0E` coefficients against RBJ calculations at both rates.

2. **`0x2B` LED colour encoding.** The bitfield model (bit 0 mute, bits 1–2 colour, bit 3 pulsing, bit 4 invert) and the value-enum model (`03` mute, `04` unmute/LED on, `08` LED off) disagree on the LED colour bits. Only `2B 04` appears in the captures. Settle by sending `2B 00`, `2B 02`, `2B 04`, `2B 06`, `2B 08` to one monitor and recording the front LED colour and pulse behaviour.

3. **`0x3B` mode vs Hz.** The field is a big-endian 16-bit value. Small values (`00 01`) act as mode selectors (bass management off); values in the 50–120 range encode crossover frequency in Hz. A system with a subwoofer and a system without are needed to confirm each case.

4. **`0x84` first byte in standby.** The device-class mapping (`01` = subwoofer, `02` = two-way) is confirmed. The value `03` has been observed by one project and is consistent with standby, but has not been independently reproduced.

5. **`0x44` midrange tag.** Predicted for three-way models; not observed. Capture any three-way model (8351, 8361, etc.) to confirm.

6. **Subwoofer-specific fields.** `Phase`, `LFE_+10`, `LFE_Channel`, `LFE_CrossoverFrequency`, `SubwooferGroupID` appear in the OEM config but have not been mapped to wire opcodes. Capture a subwoofer configuration change from GLM to identify them.

7. **`0x3C`, `0x3E`, `0x42` payloads.** All observed as `[00 00]` on a 7350A only. Their semantics are unknown; they may be subwoofer-specific configuration acknowledgements.

8. **Time-of-flight sample unit for subwoofers.** The 7350A delay is transmitted as a 32-bit integer (`00 00 01 64` = 356). Whether the unit is samples at 12 kHz or samples at 48 kHz is unresolved and changes the physical delay by 4×.