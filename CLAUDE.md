# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**ptSolarHF** is a solar-powered APRS tracker for high-altitude balloon missions. It uses an ATMega328p (3.3V, 8MHz) to transmit GPS position via WSPR (Weak Signal Propagation Reporter) on 2m VHF, then a companion Python script scrapes WSPRnet and relays packets to APRS-IS.

There are two independent components:
- **Firmware** (`/firmware/ptSolarHF/`) — Arduino/AVR C++ sketch
- **Python relay** (`/wspr-to-aprs/`) — WSPRnet scraper + APRS-IS gateway

## Firmware

### Build & Upload
1. Open `/firmware/ptSolarHF/ptSolarHF.ino` in Arduino IDE
2. Add custom board via Board Manager URL: `https://www.projecttraveler.org/downloads/package_projecttraveler_index.json`
3. Select board: **ptSolar Tracker (Arduino as ISP)**
4. Install library: **Si5351 Library by Etherkit**
5. Upload via **Arduino as ISP** programmer
6. On first flash, set fuses with avrdude: `Low:0xDF High:0xD6 Extended:0xFD`

### Serial Configuration (19200 baud)
Send `!` to enter config mode. Key commands:
- `0` — reset to defaults
- `E` — exercise mode (tests LED, buzzer, GPS, RF)
- `t` / `T` — CW carrier for 5s / 30s (frequency verification)
- `p` / `P` — send test WSPR packet on primary / secondary frequency
- `Q` — quit and reboot

### Key Pins
| Pin | Function |
|-----|----------|
| D2 | PTT (transmit enable) |
| D5 | Piezo buzzer |
| D6 | GPS power enable |
| D7/D8 | GPS TX/RX (SoftwareSerial) |
| D13 | Status LED |
| A1 | Battery voltage ADC |
| A4/A5 | I2C → Si5351 oscillator |

### Architecture
- **`ptSolarHF.ino`** — Main loop. Wakes at top-of-2-minute mark, checks GPS lock, picks transmission type, drives WSPR packet (~111s). Config mode entry on `!` character.
- **`GPS.h/cpp`** — NMEA parser (GGA + RMC), Maidenhead grid square calculation, WSPR altitude encoding (coarse + fine).
- **`ptConfig.h/cpp`** — EEPROM-backed config: callsign, frequencies, correction factor, TX mode, voltage thresholds.
- **`ptTracker.h/cpp`** — Hardware abstraction: LED/buzzer Morse annunciation, battery voltage ADC.
- **`wspr_enc.c/h`** — WSPR symbol encoding algorithm.
- **`BoardDef.h`** — Board selection define; currently `TRACKER_PTSOLARHF`.

### Non-Obvious Behavior
- An 8-second hardware watchdog is always enabled; the code must call `wdt_reset()` regularly or the MCU reboots.
- GPS is powered down during WSPR transmission (~111s) to save power.
- Below the battery voltage threshold (~3.2V default), GPS is disabled entirely; 100mV hysteresis prevents chatter.
- `isRFBlackoutZone()` suppresses transmission over certain geographic areas.
- **Type 2/3 WSPR**: callsigns >6 chars or containing `/` require transmitting both Type 2 and Type 3 packets on consecutive 2-minute slots for WSPRnet to decode them correctly.
- Altitude is encoded in the WSPR power field (coarse) and optionally as a fine residual modulation in the tone frequency.

## Python Relay

### Setup & Run
```bash
pip install -r wspr-to-aprs/requirements.txt   # installs bs4
cp wspr-to-aprs/config.json.template wspr-to-aprs/config.json
# edit config.json with callsign, passcode, and flight entries
python wspr-to-aprs/converter.py
```

The script runs as a daemon, polling WSPRnet every N seconds (set by `cycleDelay` in config.json).

### Architecture (`converter.py`)
- `main()` — polling loop
- `getSpot()` — scrapes WSPRnet HTML table for latest spots matching the configured callsign/band
- `convertGridToLatLon()` — Maidenhead → decimal lat/lon
- `calcCoarseAltitude()` — maps WSPR power level to altitude; supports `traveler` and `zachtek` telemetry modes
- `sendToAPRSIS()` — TCP connection to APRS-IS server, injects formatted APRS packet
- Config loaded from `config.json`; `flights` array allows tracking multiple payloads simultaneously

### config.json Structure
```json
{
  "cycleDelay": 120,
  "aprsCallsign": "N0CALL-1",
  "aprsPasscode": "12345",
  "flights": [
    {
      "callsign": "N0CALL",
      "band": "2m",
      "telemetryType": "traveler",
      "lastHeardTime": "",
      "lastHeardLocation": ""
    }
  ]
}
```
