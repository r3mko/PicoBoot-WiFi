/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for the loader's decision (src/common/bootdecide.c, design D7, D8, D12).
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

// --- D12: trial, fallback and crash loop, as sequences of boots ----------------------------------

typedef struct {
    bootctl_t b;
    uint32_t session;           // POWMAN scratch as the loader left it
    boot_decision_t d;
} console_t;

static void power_on(console_t *c) {
    c->session = 0;             // POWMAN scratch is cleared
    boot_inputs_t in = {.had_por = true, .marker_set = false, .bootctl = &c->b, .session = c->session};
    c->d = boot_decide(&in);
    c->session = c->d.session[0];
}

// The firmware started last rebooted on purpose (clean) or crashed; the first choice holds an image
static void reboot(console_t *c, bool clean) {
    boot_inputs_t in = {.had_por = true, .marker_set = true, .bootctl = &c->b, .session = c->session,
                        .clean_reboot = clean};
    c->d = boot_decide(&in);
    c->session = c->d.session[0];
}

static console_t updated_to_b(void) {
    console_t c = {.b = BOOTCTL_DEFAULT};
    c.b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_TRIAL;     // What the updater leaves (step 3.3)
    c.b.pending_slot = BOOTCTL_SLOT_B;
    power_on(&c);               // Running A when the update came
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_B && c.d.flags[0] == BOOTINFO_TRIAL);
    return c;
}

static void test_trial_starts_after_the_update_and_survives_clean_reboots(void) {
    console_t c = updated_to_b();
    reboot(&c, true);           // The UI's reboot during the trial
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_B && c.d.flags[0] == BOOTINFO_TRIAL);
    CHECK(c.d.slots[1] == BOOTCTL_SLOT_A && c.d.flags[1] == BOOTINFO_TRIAL_FAILED);
    power_on(&c);               // The console's next start: still on trial (keep comes from the firmware)
    CHECK(c.d.power_on && c.d.slots[0] == BOOTCTL_SLOT_B && c.d.flags[0] == BOOTINFO_TRIAL);
}

static void test_a_crash_ends_the_trial(void) {
    console_t c = updated_to_b();
    reboot(&c, false);          // Watchdog or fault while on trial
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_A && c.d.flags[0] == BOOTINFO_TRIAL_FAILED);
    CHECK(c.d.slots[1] == BOOTCTL_NONE);                       // Never the slot on trial as a fallback
    reboot(&c, true);           // Until A records it, A keeps hearing about it this session
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_A && c.d.flags[0] == BOOTINFO_TRIAL_FAILED);
    c.b.pending_slot = BOOTCTL_NONE;                           // A recorded it
    c.b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_FAILED;
    reboot(&c, true);
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_A && c.d.flags[0] == 0 && c.d.slots[1] == BOOTCTL_NONE);
    power_on(&c);
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_A && c.d.flags[0] == 0);
}

static void test_a_trial_slot_without_an_image_fails_too(void) {
    console_t c = updated_to_b();
    // rom_chain_image() returned for B: the loader starts the second choice with its session word
    CHECK(c.d.session[1] & BOOTINFO_SESSION_FAILED_B);
    c.session = c.d.session[1];
    reboot(&c, true);
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_A && c.d.flags[0] == BOOTINFO_TRIAL_FAILED);
}

static void test_a_crash_on_the_trials_power_on_is_seen_at_the_reboot(void) {
    console_t c = updated_to_b();
    power_on(&c);               // Console on: B on trial injects... and crashes
    CHECK(c.d.power_on && c.d.slots[0] == BOOTCTL_SLOT_B);
    reboot(&c, false);          // The watchdog: a warm boot, so no injection (that console start
    CHECK(!c.d.power_on && c.d.slots[0] == BOOTCTL_SLOT_A && c.d.flags[0] == BOOTINFO_TRIAL_FAILED);
}                               // runs the original IPL, D12)

static console_t running_b_kept(uint8_t state_a) {
    console_t c = {.b = BOOTCTL_DEFAULT};
    c.b.active_slot = BOOTCTL_SLOT_B;
    c.b.slot_state[BOOTCTL_SLOT_A] = state_a;
    c.b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_KEPT;
    power_on(&c);
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_B && c.d.flags[0] == 0);
    return c;
}

static void test_three_crashes_switch_to_the_other_kept_slot(void) {
    console_t c = running_b_kept(BOOTCTL_STATE_KEPT);
    reboot(&c, false);
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_B);                     // 1 crash
    reboot(&c, false);
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_B);                     // 2
    reboot(&c, false);
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_A && c.d.flags[0] == BOOTINFO_CRASH_FALLBACK);   // 3: the owner's choice
    CHECK(c.d.slots[1] == BOOTCTL_SLOT_B);
}

static void test_no_crash_fallback_without_a_kept_slot(void) {
    const uint8_t states[] = {BOOTCTL_STATE_INSTALLED, BOOTCTL_STATE_EMPTY, BOOTCTL_STATE_FAILED, BOOTCTL_STATE_TRIAL};
    for (unsigned i = 0; i < sizeof(states); i++) {
        console_t c = running_b_kept(states[i]);
        for (int n = 0; n < 5; n++) {
            reboot(&c, false);
            CHECK(c.d.slots[0] == BOOTCTL_SLOT_B && c.d.flags[0] == 0);
        }
    }
}

static void test_clean_reboots_reset_the_crash_count(void) {
    console_t c = running_b_kept(BOOTCTL_STATE_KEPT);
    reboot(&c, false);
    reboot(&c, false);
    reboot(&c, true);           // A reboot from the UI in between
    reboot(&c, false);
    reboot(&c, false);
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_B && c.d.flags[0] == 0);
    power_on(&c);               // ...and so does a power-on
    reboot(&c, false);
    reboot(&c, false);
    CHECK(c.d.slots[0] == BOOTCTL_SLOT_B);
}

static void test_the_second_choice_is_never_empty_failed_or_on_trial(void) {
    const struct { uint8_t state; bool used; } cases[] = {
        {BOOTCTL_STATE_INSTALLED, true}, {BOOTCTL_STATE_KEPT, true}, {BOOTCTL_STATE_EMPTY, false},
        {BOOTCTL_STATE_FAILED, false},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        bootctl_t b = BOOTCTL_DEFAULT;
        b.slot_state[BOOTCTL_SLOT_B] = cases[i].state;
        boot_decision_t d = decide(true, false, &b);
        CHECK(d.slots[0] == BOOTCTL_SLOT_A && (d.slots[1] == BOOTCTL_SLOT_B) == cases[i].used);
    }
    // A slot on trial that has failed this session isn't a fallback either
    console_t c = updated_to_b();
    reboot(&c, false);
    CHECK(c.d.slots[1] == BOOTCTL_NONE);
}

static void test_session_word(void) {
    // A garbage word is a new session; crash counts saturate
    boot_inputs_t in = {.had_por = true, .marker_set = true, .session = 0x12345678u};
    boot_decision_t d = boot_decide(&in);
    CHECK((d.session[0] & BOOTINFO_SESSION_CRASHES) == 0);
    in.session = BOOTINFO_SESSION_MAGIC | BOOTINFO_SESSION_STARTED | BOOTINFO_SESSION_CRASHES;
    d = boot_decide(&in);
    CHECK((d.session[0] & BOOTINFO_SESSION_CRASHES) == BOOTINFO_SESSION_CRASHES);
    CHECK((d.session[0] & BOOTINFO_SESSION_MAGIC_MASK) == BOOTINFO_SESSION_MAGIC);
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
    test_trial_starts_after_the_update_and_survives_clean_reboots();
    test_a_crash_ends_the_trial();
    test_a_trial_slot_without_an_image_fails_too();
    test_a_crash_on_the_trials_power_on_is_seen_at_the_reboot();
    test_three_crashes_switch_to_the_other_kept_slot();
    test_no_crash_fallback_without_a_kept_slot();
    test_clean_reboots_reset_the_crash_count();
    test_the_second_choice_is_never_empty_failed_or_on_trial();
    test_session_word();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_bootdecide: all checks passed\n");
    return 0;
}
