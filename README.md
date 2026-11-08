# PicoBoot-WiFi

PicoBoot-DX for the Raspberry Pi **Pico 2 W**, with over-the-air updates: a GameCube IPL-injection modchip that also runs a Wi-Fi access point, where you can update its firmware and payload from a phone or computer.

> **Status:** in development (phase 1 of the [implementation plan](docs/implementation-plan.md)). Not ready for use yet. For a working modchip, use [PicoBoot-DX](https://github.com/r3mko/PicoBoot-DX).

## Documentation

- [Design](docs/architecture.md): the decisions, flash layout, boot flow and update model.
- [Implementation plan](docs/implementation-plan.md): phases, steps and their status.
- [Research](docs/research/README.md), including the [phase 0 hardware results](docs/research/phase0-results.md).

## Building

Everything builds in Docker with the same SDK and toolchain as CI (Pico SDK 2.3.1, picotool 2.3.1, arm-none-eabi-gcc 14.2):

```sh
docker build -t picoboot-wifi-build docker/
docker run --rm -v "$PWD":/src -w /src picoboot-wifi-build \
    sh -c 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel'
python3 -m unittest discover -s tests -v
python3 tests/check_layout.py --loader build/src/loader/loader.elf --firmware build/src/firmware/firmware.elf
```

## License

GPL-2.0-only (see [LICENSE](LICENSE)). PicoBoot-WiFi builds on [PicoBoot-DX](https://github.com/r3mko/PicoBoot-DX) and [PicoBoot](https://github.com/webhdx/PicoBoot) by Maciej Kobus, and bundles [gekkoboot](https://github.com/redolution/gekkoboot).
