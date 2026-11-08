/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for the loader's decision (src/common/bootdecide.c, design D7, D8).
// Built and run by tests/test_host.py.

#include <stdio.h>
#include "bootdecide.h"
#include "bootinfo.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static boot_decision_t decide(bool had_por, bool marker_set, const bootctl_t *b) {
    boot_inputs_t in = {.had_por = had_por, .marker_set = marker_set, .bootctl = b};
    return boot_decide(&in);
}

static void test_power_on_rule(void) {
    // Design D8; phase 0 found HAD_POR still set after watchdog and ROM reboots
    CHECK(decide(true, false, NULL).power_on);          // Power-on: inject
    CHECK(!decide(true, true, NULL).power_on);          // Warm reboot (HAD_POR sticky, marker set)
    CHECK(!decide(false, false, NULL).power_on);        // Brown-out or RUN: POWMAN cleared, no HAD_POR
    CHECK(!decide(false, true, NULL).power_on);
}

static void test_no_boot_control_means_slot_a_then_b(void) {
    boot_decision_t d = decide(true, false, NULL);
    CHECK(d.slots[0] == BOOTCTL_SLOT_A && d.slots[1] == BOOTCTL_SLOT_B && d.bootctl_default);
}

static void test_active_slot_first_then_the_other(void) {
    bootctl_t b = BOOTCTL_DEFAULT;
    b.active_slot = BOOTCTL_SLOT_B;
    boot_decision_t d = decide(false, true, &b);
    CHECK(d.slots[0] == BOOTCTL_SLOT_B && d.slots[1] == BOOTCTL_SLOT_A && !d.bootctl_default);
    b.active_slot = BOOTCTL_SLOT_A;
    d = decide(false, true, &b);
    CHECK(d.slots[0] == BOOTCTL_SLOT_A && d.slots[1] == BOOTCTL_SLOT_B);
}

static void test_garbage_active_slot_means_slot_a(void) {
    bootctl_t b = BOOTCTL_DEFAULT;
    b.active_slot = 7;
    boot_decision_t d = decide(true, false, &b);
    CHECK(d.slots[0] == BOOTCTL_SLOT_A && d.slots[1] == BOOTCTL_SLOT_B);
}

static void test_bootinfo_check_byte(void) {
    CHECK(bootinfo_valid(BOOTINFO_MAGIC | BOOTINFO_POWER_ON | BOOTINFO_SLOT_B));
    CHECK(!bootinfo_valid(0));
    CHECK(!bootinfo_valid(BOOTINFO_SESSION_MARKER));
}

int main(void) {
    test_power_on_rule();
    test_no_boot_control_means_slot_a_then_b();
    test_active_slot_first_then_the_other();
    test_garbage_active_slot_means_slot_a();
    test_bootinfo_check_byte();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_bootdecide: all checks passed\n");
    return 0;
}
