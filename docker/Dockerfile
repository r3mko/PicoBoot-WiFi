# Local build image for PicoBoot-WiFi, with the same SDK, picotool and toolchain as CI.
#
#   docker build -t picoboot-wifi-build docker/
#   docker run --rm -v "$PWD":/src -w /src picoboot-wifi-build <command>

FROM ubuntu:26.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        gcc-arm-none-eabi \
        git \
        libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# Pico SDK 2.3.1 with the submodules the firmware uses (no btstack, no mbedtls)
RUN git clone --depth 1 --branch 2.3.1 https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk \
    && git -C /opt/pico-sdk submodule update --init --depth 1 lib/cyw43-driver lib/lwip lib/tinyusb

# picotool 2.3.1, installed the way CI does it
RUN git clone --depth 1 --branch 2.3.1 https://github.com/raspberrypi/picotool.git /tmp/picotool \
    && cmake -S /tmp/picotool -B /tmp/picotool/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DPICO_SDK_PATH=/opt/pico-sdk \
        -DCMAKE_INSTALL_PREFIX=/opt/picotool \
        -DPICOTOOL_NO_LIBUSB=1 \
        -DPICOTOOL_FLAT_INSTALL=1 \
        -DCMAKE_RULE_MESSAGES=OFF \
    && cmake --build /tmp/picotool/build --parallel \
    && cmake --install /tmp/picotool/build \
    && rm -rf /tmp/picotool

ENV PICO_SDK_PATH=/opt/pico-sdk \
    PICOTOOL_FETCH_FROM_GIT_PATH=/opt/picotool
