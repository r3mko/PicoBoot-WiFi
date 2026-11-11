/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for the payload check (src/firmware/payload.c), which decides how much the injection's
// DMA reads. Built and run by tests/test_host.py.

#include <stdio.h>
#include <string.h>
#include "payload.h"
#include "bootctl.h"
#include "endian.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define REGION_WORDS 1024u                 // A 4 KB region
static uint32_t region[REGION_WORDS + 16]; // Room past the region, to see nothing reads there

// A payload of `size` bytes: header, filler, and the footer where the size says
static void make(uint32_t size) {
    memset(region, 0xA5, sizeof(region));
    region[0] = BigEndian32(0x49504C42);   // "IPLB"
    region[1] = BigEndian32(0x4F4F5420);   // "OOT "
    region[2] = BigEndian32(size);
    if (size >= 4 && size / 4 <= REGION_WORDS) {
        region[size / 4 - 1] = BigEndian32(0x5049434F);   // "PICO"
    }
}

static size_t check(void) {
    return payload_check(region, REGION_WORDS * 4);
}

static void test_valid_payloads_are_rounded_up_to_whole_transfers(void) {
    make(36);                              // The smallest: header and footer only
    CHECK(check() == 256);
    make(1024);
    CHECK(check() == 256);
    make(1028);
    CHECK(check() == 512);
    make(REGION_WORDS * 4);                // Exactly fills the region
    CHECK(check() == REGION_WORDS);
}

static void test_sizes_that_dont_fit_are_refused(void) {
    make(35);                              // Smaller than header and footer
    CHECK(check() == PAYLOAD_INVALID);
    make(REGION_WORDS * 4 + 4);            // Larger than the region
    CHECK(check() == PAYLOAD_INVALID);
    make(0xFFFFFFFCu);                     // A size that would wrap
    CHECK(check() == PAYLOAD_INVALID);

    // Fits, but its last 1K transfer would read past the region
    make(1028);
    CHECK(payload_check(region, 1100) == PAYLOAD_INVALID);
}

static void test_damaged_payloads_are_refused(void) {
    make(1024);
    region[255] = 0;                       // No footer
    CHECK(check() == PAYLOAD_INVALID);
    make(1024);
    region[0] = 0;                         // No "IPLB"
    CHECK(check() == PAYLOAD_INVALID);
    make(1024);
    region[1] = 0;                         // No "OOT "
    CHECK(check() == PAYLOAD_INVALID);
    memset(region, 0xFF, sizeof(region));  // Erased flash
    CHECK(check() == PAYLOAD_INVALID);
    make(36);
    CHECK(payload_check(region, 8) == PAYLOAD_INVALID);   // A region too small for the header
}


// D15, D42: an upload goes into the payload slot that isn't the choice; payload 2 for the built-in
static void test_upload_target(void) {
    CHECK(payload_upload_target(BOOTCTL_PAYLOAD_1) == BOOTCTL_PAYLOAD_2);
    CHECK(payload_upload_target(BOOTCTL_PAYLOAD_2) == BOOTCTL_PAYLOAD_1);
    CHECK(payload_upload_target(BOOTCTL_PAYLOAD_BUILTIN) == BOOTCTL_PAYLOAD_2);
    CHECK(payload_upload_target(77) == BOOTCTL_PAYLOAD_2);
}

static void test_fingerprint(void) {
    make(4000);
    const uint32_t a = payload_fingerprint(region, REGION_WORDS * 4);
    CHECK(a != 0);
    region[500] ^= 1;                       // A sampled word: index 32 * 1000 / 64
    CHECK(payload_fingerprint(region, REGION_WORDS * 4) != a);
    region[500] ^= 1;
    region[990] ^= 1;                       // In the last 256 bytes
    CHECK(payload_fingerprint(region, REGION_WORDS * 4) != a);
    region[990] ^= 1;
    region[100] ^= 1;                       // In the first 256 bytes, past the header, not sampled
    CHECK(payload_fingerprint(region, REGION_WORDS * 4) == a);
    region[100] ^= 1;
    make(3996);                             // Another size
    CHECK(payload_fingerprint(region, REGION_WORDS * 4) != a);
    make(36);                               // The smallest payload
    CHECK(payload_fingerprint(region, REGION_WORDS * 4) != 0);
    make(4000);
    region[999] = 0;                        // No footer: not a payload
    CHECK(payload_fingerprint(region, REGION_WORDS * 4) == 0);
}

// D17 over a sequence of power-ons: payload_choose(), the injection, payload_record()
static uint8_t power_on(bootctl_t *b, bool stored, uint32_t fingerprint, bool complete, bool *usb) {
    const uint8_t chosen = payload_choose(b, stored, fingerprint, usb);
    payload_record(b, fingerprint, *usb, complete);
    return chosen;
}

static void test_usb_rule(void) {
    bootctl_t b;
    memset(&b, 0, sizeof(b));
    bool usb;

    // Recorded payload 1, payload 2 chosen: nothing to do
    b.payload1_fingerprint = 111;
    b.active_payload = BOOTCTL_PAYLOAD_2;
    strcpy(b.payload_name[0], "Swiss");
    CHECK(power_on(&b, true, 111, true, &usb) == BOOTCTL_PAYLOAD_2 && !usb);

    // A USB install: the drag-and-drop's reboot on USB power (no console), then the console's power-on
    CHECK(power_on(&b, true, 222, false, &usb) == BOOTCTL_PAYLOAD_1 && usb);
    CHECK(b.active_payload == BOOTCTL_PAYLOAD_1 && b.payload_name[0][0] == '\0' && b.payload1_fingerprint == 111);
    CHECK(power_on(&b, true, 222, true, &usb) == BOOTCTL_PAYLOAD_1 && usb);     // The page says so here
    CHECK(b.payload1_fingerprint == 222);
    CHECK(power_on(&b, true, 222, true, &usb) == BOOTCTL_PAYLOAD_1 && !usb);    // Settled

    // Original IPL once (not complete) keeps it pending
    CHECK(power_on(&b, true, 333, false, &usb) == BOOTCTL_PAYLOAD_1 && usb && b.payload1_fingerprint == 222);
    CHECK(power_on(&b, true, 333, true, &usb) == BOOTCTL_PAYLOAD_1 && usb && b.payload1_fingerprint == 333);

    // A choice over Wi-Fi while one is pending settles it (payloads.c records the fingerprint)
    CHECK(power_on(&b, true, 444, false, &usb) == BOOTCTL_PAYLOAD_1 && usb);
    b.active_payload = BOOTCTL_PAYLOAD_2;
    b.payload1_fingerprint = 444;
    CHECK(power_on(&b, true, 444, true, &usb) == BOOTCTL_PAYLOAD_2 && !usb);

    // Payload 1 deleted or cut off (fingerprint 0): no USB install; 0 is recorded
    CHECK(power_on(&b, true, 0, true, &usb) == BOOTCTL_PAYLOAD_2 && !usb && b.payload1_fingerprint == 0);
    CHECK(power_on(&b, true, 0, true, &usb) == BOOTCTL_PAYLOAD_2 && !usb);

    // Blank boot control (a USB install of the firmware): payload 1 is the default choice, recorded
    // without a USB-install notice (phase 4 review)
    memset(&b, 0, sizeof(b));
    CHECK(power_on(&b, false, 555, true, &usb) == BOOTCTL_PAYLOAD_1 && !usb && b.payload1_fingerprint == 555);
    CHECK(power_on(&b, true, 555, true, &usb) == BOOTCTL_PAYLOAD_1 && !usb);
    CHECK(!payload_record(&b, 555, false, true));                               // Nothing to store
}

int main(void) {
    test_fingerprint();
    test_usb_rule();
    test_upload_target();
    test_valid_payloads_are_rounded_up_to_whole_transfers();
    test_sizes_that_dont_fit_are_refused();
    test_damaged_payloads_are_refused();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("payload: all tests passed\n");
    return 0;
}
