# Phase 0 measurement build (`dx-measure`)

Implementation plan step 0.1. This is **PicoBoot-DX v0.10 for the Pico 2 W**, with three additions to measure what the design couldn't know:

- **Boot timing (design D31):** a passive PIO state machine in `pio1` watches CS. It timestamps the console's first CS after "PIO ready" (the *margin*) and counts every CS pulse. The DMA count gives the 1 KB transfers delivered.
- **Readout (design D32):** after the injection, an access point with a report page, plus load tests for the radio.
- **Boot log:** every boot's numbers go to flash (`0x3F0000`, 64 boots), so you can do a series of power-ons and read them all at once.

It's a test build, not a release. It doesn't touch the payload area, and installing PicoBoot-DX again undoes it.

## What differs from PicoBoot-DX

| | PicoBoot-DX v0.10 | dx-measure |
|---|---|---|
| Up to "PIO ready" | | The same code. Additions: two reset registers read, two flash reads (test switches), the counter set up in `pio1` (a few µs) |
| Injection programs (`picoboot-dx.pio`) | | Byte-identical (checked by the tests) |
| During the injection | Starts the CYW43 for the LED and blinks | Waits and measures; the CYW43 stays off (design D9) |
| After a warm reboot (watchdog, ROM reboot, BOOTSEL) | Injects again | Doesn't inject: design D8's rule, a power reset in CHIP_RESET *and* no session marker in POWMAN scratch. Logged as "skipped" |
| After the injection | LED blinks, then idles | Logs the boot to flash, then starts the access point |

## Build and test

From the repository root, with the Docker image from `docker/Dockerfile`:

```sh
docker build -t picoboot-wifi-build docker/
docker run --rm -v "$PWD":/src -w /src/hwtest/dx-measure picoboot-wifi-build \
    sh -c 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel'
DX_MEASURE_BUILD_DIR=hwtest/dx-measure/build python3 -m unittest discover -s hwtest/dx-measure/tests -v
```

The firmware is `hwtest/dx-measure/build/dx-measure.uf2`.

- **The tests** run the real PIO programs in PicoBoot-DX's simulator. They check that the injection is the same with and without the counter, that the counter sees every CS pulse, and that it flags the first one within a few cycles.

## Using it

1. **Install:** hold BOOTSEL, plug in USB and drop `dx-measure.uf2` on the drive.
   - The payload at `0x10080000` stays as it is.
   - For the Swiss tests, install `swiss_r2119.uf2` (Swiss release, `PicoBoot/` folder) the same way, if it isn't on the board already.
2. **Wi-Fi:** SSID `PicoBoot-Measure-XXXX`, password `picoboot`.
   - A phone usually opens the page by itself. Otherwise browse to `http://192.168.4.1/`.
   - If the phone says "no internet", choose to stay connected.
3. **The report page:**
   - **This boot:** reset reason, payload, margin, injection result, CS count against the expected count (224 + transfers).
   - **The boot log**, newest first.
   - **Buttons:**
     - *Download 20/100 MB* (Tx load);
     - *Upload* (Rx load);
     - *Next boots: built-in gekkoboot* / *flash payload*;
     - *Next boot: Wi-Fi 60 s late*;
     - *Reboot (watchdog)* / *Reboot (ROM)*: warm reboots, to test the D8 rule;
     - *Clear boot log*.
4. **LED** (only once Wi-Fi is up):
   - on: injection complete;
   - fast blink: stalled;
   - slow blink: no console seen.

**Reading the numbers:**

| Field | Meaning |
|---|---|
| **Margin** | Time from "PIO ready" to the console's first CS. It has to be positive. The bigger it is, the more room the loader (phase 1) has |
| **CS at the end** | Should be exactly 224 + transfers. Fewer means pulses were missed: the PIO started too late |
| **Injection** | *complete* = all words delivered and the data line released. *STALLED* = it didn't finish within 10 s |
| **Reset reason** | POR = power-on; BOR = brown-out; WDOG = watchdog/reboot. Whether a cold boot shows up as POR or BOR matters for design D8 |
| **Session marker** | Set in a POWMAN scratch register at every boot. "Not set" means a power-on; "SET" means a warm reboot, even when CHIP_RESET still says POR |
| **Flash 0x3FFF00** | Whether the UF2's RP2350-E10 block (`0xEF` × 256) landed in the last page of flash when you installed. Matters for the layout in phase 1 |

## Test procedure (plan steps 0.2–0.7)

For each series, send me the report page afterwards (a screenshot, or copy the text). Clear the log before each series, to keep them apart.

### 0.2 Bench (USB, no console)

1. Install `dx-measure.uf2`, then *Clear boot log*.
2. Unplug and plug in USB again.
3. Tap *Reboot (watchdog)*, wait for the page to come back.
4. Tap *Reboot (ROM)*, wait again.
5. Run *Download 20 MB* and note the speed the phone shows.
6. Send the page.

**Expected:**
- **The replug:** POR, session marker not set, "no CS (bench?)" and a slowly blinking LED.
- **The two reboots:** each shows "skipped: warm (marker)". Their CHIP_RESET values show whether HAD_POR survives a warm reboot (risk R6); the marker covers it either way.

### 0.3 Cold boots in the console (3V3 wiring as today)

1. **Swiss r2119** as the flash payload, *Next boots: flash payload*: 10 power-ons. Leave the console off for at least 10 s each time, and check that Swiss starts every time.
2. *Next boots: built-in gekkoboot*: 10 more power-ons. Check that gekkoboot starts.
3. **Warm reboot in the console:** start a game from Swiss, then tap *Reboot (watchdog)*. The game must keep running, and the report must show "skipped: warm (marker)" for that boot.
4. Save the report.

### 0.4 Warm board

Play for 30 minutes, then do 5 power cycles (off for about 5 s). Save the report.

### 0.5 Current

Put the meter in series with the Pico's 3.3 V feed, and write down the average reading in each state:

| State | How |
|---|---|
| Radio off | Press *Next boot: Wi-Fi 60 s late*, power-cycle, and read the meter within 60 s after the console has started |
| AP idle, no client | Phone's Wi-Fi off |
| Client connected, idle | Phone connected, page open, nothing loading |
| Download running | *Download 100 MB* on the phone |

### 0.6 Range

With the console closed, read the signal strength on the phone at 1, 3 and 5 m. Also note whether *Download 20 MB* still works at 5 m and at what speed. Then move the board to a second position inside the console and repeat.
- **Android:** a Wi-Fi analyzer app.
- **iPhone:** AirPort Utility's Wi-Fi scanner, which you turn on in Settings.

### 0.7 Stress test

Start *Download 100 MB* (several times in a row, or 500 MB with `/stream?mb=500`). While it runs, load a game in Swiss from the SD2SP2 and play for a few minutes, with the HDMI adapter in use.

What to note:
- glitches or crashes;
- video artefacts;
- Wi-Fi drops.

Afterwards the report shows whether the Pico reset: a new log entry with BOR or WDOG, and the uptime starting again.

## Results

The results go into `docs/research/phase0-results.md` (plan step 0.8).
