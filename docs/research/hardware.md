# Hardware: power, boot timing, range

Research done on 2026-11-08 for PicoBoot-WiFi (Pico 2 W only). **[inf]** marks a conclusion rather than a quoted fact.

**Short answer:**
- **Power:** the radio should work when the board is fed through its 3V3 pin, but at full Tx power it can draw more than the 3V3 pin is meant to carry.
- **Not measured anywhere:** nobody has published the console's boot timing or the Wi-Fi range from inside a closed console.
- All three need checking on hardware before the design is final.

## 1. Pico 2 W power

- **Radio supply:** on the Pico 2 W schematic (sheet 2), 3V3 is the only power net that enters the CYW43439 sheet.
  - VBAT (VDDBAT5V) and VDDIO come off 3V3, through ferrite beads [inf].
  - GPIO23 (WL_ON) switches the radio on.
  - So a board fed through 3V3, as PicoBoot boards are, should also power the radio.
- **Datasheet guidance (Pico 2 W datasheet):**
  - p.8: keep the load on the 3V3 pin "under 300 mA". There is no guidance on feeding the board through 3V3.
  - p.8: 3V3_EN is pulled up to VSYS through 100 kΩ.
  - p.14: the RT6154 buck-boost runs from VSYS.
  - p.17: SoftAP with "up to 4 clients"; WPA3 supported.
  - p.18: metal near the antenna reduces gain and bandwidth.
  - Flash: the datasheet text says 2 MB (W25Q16JV), but the schematic shows a W25Q32RV (4 MB), which is right.
- **RT6154 with 3V3 driven from outside:** in shutdown, "the load is disconnected from the input" (RT6154 datasheet p.11), and its UVLO threshold is 1.7 V.
  - A comment in the upstream PicoBoot `hw.c` says a plain Pico fed through 3V3 reads VSYS as about 0x2cd on the ADC [inf: VSYS back-feeds to about 1.7 V].
  - Whether the regulator stays off cleanly on a Pico 2 W needs a test.
- **CYW43439 current** (datasheet 002-30348 Rev *B):
  - VBAT range is 3.0–4.8 V; full RF performance is only guaranteed above 3.2 V (p.56–57).
  - The console rail measures about 3.43 V, so the margin is small.
  - Table 38 (p.74, at VBAT 3.6 V):

    | State | Current |
    |---|---|
    | Rx listen | 37 mA |
    | Rx active | 37–43 mA |
    | Tx, 19.5 dBm, 1 Mbps | 320 mA |
    | Tx, MCS7, 16 dBm | 271 mA |
    | Off | 3.5 µA |

  - At power-up the chip holds itself in reset for up to 110 ms; wait at least 150 ms before talking to it (p.79).
  - [inf] An access point can't sleep, so expect about 40 mA continuous, plus Tx bursts that exceed the 300 mA guidance at full power.
- **RP2350 current:** about 15 mA at 150 MHz (RP2350 datasheet Table 1693) [inf: 20–30 mA at 250 MHz].
- **Measured Pico W boards** (station mode, 5 V USB; no AP-mode figures found):
  - about 45 mA with Wi-Fi on (stfn.pl);
  - 70–80 mA under a web benchmark (jeffgeerling.com).
- **Ways to save power:**
  - **Tx power:** cyw43-driver has no API for it. MicroPython sets the `qtxpower` iovar in quarter-dBm steps, which we can copy.
  - **Power-save modes:** `cyw43_wifi_pm` only applies in station mode.
  - **For an access point**, the only real saving is turning Wi-Fi off.

## 2. The GameCube's 3.3 V rail

- **Source:** the internal DC-DC board, connector P1 pins 3–4. Board makes are Mitsumi, Sharp and TDK; the first two have leaky electrolytics. The rail measures 3.43 V at controller pin 6 (retrotechwiki).
- **Capacity:** no rated figure found. One forum estimate puts the console's own draw on 3.3 V at about 0.8 A.
- **PicoBoot wiring:**
  - The Pico is fed from a 3V3 pad near P4.
  - The alternative is 5 V to VSYS through a Schottky diode (1N5822 / 1N5819 / SS110). The wiring diagram says "using 5V is recommended, especially on non-recapped systems".
  - With 5 V on VSYS, the RT6154 makes the 3.3 V, and the radio's Tx bursts no longer load the console's 3.3 V rail [inf].
- **Upstream issues:**
  - #104: "slow rise time of 3.3V power rail is problematic on some setups".
  - #75, #145: weak power boards, fixed with 5 V to VSYS.
  - The troubleshooting page: cheap HDMI adapters cause boots to the stock menu.
- **W boards in consoles:**
  - #13: a Pico W works.
  - #148: Pico 2 W installs on PicoBoot v0.5.
  - #142: webhdx recommends the Pico 2 W.
  - NeoPicoBoot runs a Pico W access point inside the console, fed through VSYS from 5 V.
  - No range figures anywhere.

## 3. Boot timing

- **No public measurement** of the time from power-on to the first IPL read, or of how long the 224-pulse phase lasts.
- **Related facts:**
  - BS1 is 0x700 bytes and is fetched in 32-byte bursts (gc-ipl). [inf] 224 × 8 = 0x700, so the 224 skipped pulses are probably the BS1 fetch.
  - gekkoboot r9.1 notes: a ~1.5 MiB IPL transfer takes "about half a second or more"; 64 KiB takes about 20 ms.
  - The RP2350 leaves power-on reset 3–10 µs after its core supply rises. The crystal takes over 1 ms to start.
  - Third-party measurement: about 5 ms from reset to the application, then 1–3 ms to `main()` (see [boot-rom.md](boot-rom.md) §6).
- **Reports of slow or unreliable boots:**
  - #148 (2026): a Pico 2 W batch only booted Swiss on a cold first power-up. webhdx blames a "slow flash chip" and plans to slow flash access.
  - #146: "uses quite high overclocking… Some Picos take it better than others".
  - A fork patches the clock down to 240 MHz for generic boards. PicoLoader runs at 200 MHz "for better compatibility with clone boards".
- **Design consequences [inf]:**
  - Every millisecond added before the PIO starts eats into an unknown margin.
  - The flash clock during the payload DMA is a known weak spot.
  - Both need measuring with a scope: rail rise, "PIO ready", first CS, end of the 224 pulses. Measure on a cold board and on a warm one.

## 4. Range and interference

- **Range:** no data on Wi-Fi from inside a closed DOL-001 or DOL-101.
- **Interference:**
  - gbs-control users report the video board interfering with an ESP8266's Wi-Fi.
  - Whether Wi-Fi traffic disturbs the EXI bus (SD2SP2 in Swiss) or the video is unknown.

## Needs a hardware test

1. **3V3-only power:** with the board fed only through 3V3, check VSYS and 3V3_EN, that the RT6154 stays off, and that CYW43 init and the LED work.
2. **Rail sag:** current on the console's 3.3 V rail with the AP idle, with a client connected and during an upload. Check the rail stays above 3.2 V during Tx bursts, with an SD2SP2, BBA or HDMI adapter fitted.
3. **Range:** RSSI and range from closed DOL-001 and DOL-101 consoles, for several placements and Tx power settings.
4. **Boot timing:** see §3, plus the time a loader and chained image add.
5. **Transfer count:** the number of 1 KB transfers BS1 makes, to confirm the ~1.69 MiB ceiling (see [ecosystem.md](ecosystem.md) §2).
6. **Interference:** whether Wi-Fi traffic disturbs the EXI lines or the video.

## Sources

- [Pico 2 W datasheet](https://datasheets.raspberrypi.com/picow/pico-2-w-datasheet.pdf), [Pico 2 W schematic](https://datasheets.raspberrypi.com/picow/pico-2-w-schematic.pdf), [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
- CYW43439 datasheet 002-30348 Rev *B; RT6154 datasheet DS6154A/B-01
- [GameCube troubleshooting guide (retrotechwiki)](https://retrotechwiki.com/Nintendo_GameCube_Troubleshooting_Guide)
- webhdx/PicoBoot issues #13, #75, #104, #142, #145, #146, #148
- [NeoPicoBoot](https://github.com/paulmreese/NeoPicoBoot), [gc-ipl](https://github.com/ogamespec/gc-ipl)
- [stfn.pl: Pico power consumption](https://stfn.pl/blog/34-pico-power-consumption-solar-panels/), [Jeff Geerling: Pico W](https://www.jeffgeerling.com/blog/2022/raspberry-pi-pico-w-brings-wifi-6)
