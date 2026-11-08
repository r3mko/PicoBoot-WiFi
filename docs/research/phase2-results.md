# Phase 2 results

Hardware tests from implementation plan phase 2, run by the owner on the `phase-2-wifi` builds.

**Setup:** the Pico 2 W from phases 0 and 1 (SSID suffix C2C4, RP2350 A2).

## Bench check of steps 2.1 and 2.2 (2026-11-08, USB power, no console)

Build `a75e9b7`: settings in flash (D40), lwIP httpd with captive-portal redirects (D18), and test buttons for the channel.

| Check | Result |
|---|---|
| Settings after the install | Defaults: channel 6, country XX, Tx power medium, Wi-Fi off after 10 min, LED on |
| Channel 11 survives a watchdog reboot (boot 109) | **Passed** |
| ...and a power cycle (boot 110) | **Passed** |
| Back to channel 6 (boot 111) | **Passed**; the page shows a stored record, not the defaults |
| iPhone: the page opens by itself after joining | **Passed:** the captive-portal pop-up shows the page |
| Android: "Sign in to network" opens the page | *to do* |
