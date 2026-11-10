/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/firmware/slots.c (design D12), and the loop with the loader's decision: the
// firmware's boot control writes and src/common/bootdecide.c together, boot after boot.
// Built and run by tests/test_host.py.

#include <stdio.h>
#include <string.h>
#include "slots.h"
#include "bootdecide.h"
#include "bootinfo.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static bootctl_t updated_to_b(void) {
    bootctl_t b = BOOTCTL_DEFAULT;          // After a USB install: A installed, running, B empty
    b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_TRIAL;
    b.pending_slot = BOOTCTL_SLOT_B;
    return b;
}

static void test_keep(void) {
    bootctl_t b = updated_to_b();
    CHECK(!slots_keep(&b, BOOTCTL_SLOT_A));                     // Not the one on trial
    CHECK(slots_keep(&b, BOOTCTL_SLOT_B));
    CHECK(b.active_slot == BOOTCTL_SLOT_B && b.pending_slot == BOOTCTL_NONE);
    CHECK(b.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_KEPT);
    CHECK(b.slot_state[BOOTCTL_SLOT_A] == BOOTCTL_STATE_KEPT);  // The one it replaced had run
    CHECK(!slots_keep(&b, BOOTCTL_SLOT_B));                     // Once
}

static void test_keep_leaves_a_failed_slot_failed(void) {
    bootctl_t b = BOOTCTL_DEFAULT;
    b.active_slot = BOOTCTL_SLOT_B;
    b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_FAILED;        // Odd, but don't turn failed into kept
    b.slot_state[BOOTCTL_SLOT_A] = BOOTCTL_STATE_TRIAL;
    b.pending_slot = BOOTCTL_SLOT_A;
    CHECK(slots_keep(&b, BOOTCTL_SLOT_A));
    CHECK(b.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_FAILED);
}

static void test_trial_failed(void) {
    bootctl_t b = updated_to_b();
    CHECK(!slots_at_start(&b, BOOTCTL_SLOT_A, 0));
    CHECK(slots_at_start(&b, BOOTCTL_SLOT_A, BOOTINFO_TRIAL_FAILED));
    CHECK(b.pending_slot == BOOTCTL_NONE && b.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_FAILED);
    CHECK(b.active_slot == BOOTCTL_SLOT_A);
    CHECK(!slots_at_start(&b, BOOTCTL_SLOT_A, BOOTINFO_TRIAL_FAILED));  // Already recorded
}

static void test_crash_fallback(void) {
    bootctl_t b = BOOTCTL_DEFAULT;
    b.active_slot = BOOTCTL_SLOT_B;
    b.slot_state[BOOTCTL_SLOT_A] = BOOTCTL_STATE_KEPT;
    b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_KEPT;
    CHECK(slots_at_start(&b, BOOTCTL_SLOT_A, BOOTINFO_CRASH_FALLBACK));
    CHECK(b.active_slot == BOOTCTL_SLOT_A && b.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_FAILED);
    CHECK(!slots_at_start(&b, BOOTCTL_SLOT_A, BOOTINFO_CRASH_FALLBACK));
}

// --- The loader and the firmware together ---------------------------------------------------------

typedef struct {
    bootctl_t b;
    uint32_t session;
    uint8_t running;
    uint32_t flags;
    bool power_on;
} pico_t;

// One boot: the loader decides (the first choice holds an image), the firmware records what it heard
static void boot(pico_t *p, bool power_on, bool clean) {
    boot_inputs_t in = {.had_por = true, .marker_set = !power_on, .bootctl = &p->b,
                        .session = power_on ? 0 : p->session, .clean_reboot = clean};
    boot_decision_t d = boot_decide(&in);
    p->session = d.session[0];
    p->running = d.slots[0];
    p->flags = d.flags[0];
    p->power_on = d.power_on;
    slots_at_start(&p->b, p->running, p->flags);
}

static void test_update_kept_after_a_power_on(void) {
    pico_t p = {.b = updated_to_b()};
    boot(&p, false, true);                  // The updater's reboot
    CHECK(p.running == BOOTCTL_SLOT_B && (p.flags & BOOTINFO_TRIAL));
    boot(&p, true, false);                  // The console's next start
    CHECK(p.running == BOOTCTL_SLOT_B && p.power_on);
    CHECK(slots_keep(&p.b, p.running));     // A complete injection, then Wi-Fi up
    boot(&p, true, false);
    CHECK(p.running == BOOTCTL_SLOT_B && p.flags == 0 && p.b.pending_slot == BOOTCTL_NONE);
}

static void test_failed_update_goes_back_for_good(void) {
    pico_t p = {.b = updated_to_b()};
    boot(&p, false, true);                  // Into the trial...
    boot(&p, false, false);                 // ...which crashes
    CHECK(p.running == BOOTCTL_SLOT_A && p.b.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_FAILED);
    for (int i = 0; i < 3; i++) {
        boot(&p, true, false);              // Later sessions don't try it again
        CHECK(p.running == BOOTCTL_SLOT_A && p.flags == 0);
    }
}

static void test_crash_loop_after_keeping(void) {
    pico_t p = {.b = updated_to_b()};
    boot(&p, false, true);
    boot(&p, true, false);
    slots_keep(&p.b, p.running);            // B kept, A kept as the one it replaced
    boot(&p, true, false);                  // B runs, then crashes three times
    boot(&p, false, false);
    boot(&p, false, false);
    boot(&p, false, false);
    CHECK(p.running == BOOTCTL_SLOT_A && (p.flags & BOOTINFO_CRASH_FALLBACK));
    CHECK(p.b.active_slot == BOOTCTL_SLOT_A && p.b.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_FAILED);
    boot(&p, true, false);                  // For good
    CHECK(p.running == BOOTCTL_SLOT_A);
}

// The phase 3 console test: a trial fails, then a new upload into the same slot in the same session
// must be tried (it wasn't: the session still said the slot's trial had failed)
static void test_a_new_trial_in_the_same_session(void) {
    pico_t p = {.b = updated_to_b()};
    boot(&p, false, true);                  // Into B's trial...
    boot(&p, false, false);                 // ...which crashes: back on A, B failed
    CHECK(p.running == BOOTCTL_SLOT_A && p.b.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_FAILED);
    // A new upload into B: the updater's boot control and session changes, then its reboot
    p.b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_TRIAL;
    p.b.pending_slot = BOOTCTL_SLOT_B;
    p.session = slots_session_new_trial(p.session, BOOTCTL_SLOT_B);
    boot(&p, false, true);
    CHECK(p.running == BOOTCTL_SLOT_B && (p.flags & BOOTINFO_TRIAL));
    // Without the session change it would have gone straight back to A
    pico_t q = {.b = updated_to_b()};
    boot(&q, false, true);
    boot(&q, false, false);
    q.b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_TRIAL;
    q.b.pending_slot = BOOTCTL_SLOT_B;
    boot(&q, false, true);
    CHECK(q.running == BOOTCTL_SLOT_A);
    // Booting a failed slot again works the same way
    p = (pico_t){.b = updated_to_b()};
    boot(&p, false, true);
    boot(&p, false, false);
    CHECK(slots_switch(&p.b, p.running, BOOTCTL_SLOT_B) == SLOTS_SWITCHED);
    p.session = slots_session_new_trial(p.session, BOOTCTL_SLOT_B);
    boot(&p, false, true);
    CHECK(p.running == BOOTCTL_SLOT_B && (p.flags & BOOTINFO_TRIAL));
    CHECK(slots_session_new_trial(BOOTINFO_SESSION_FAILED_A | BOOTINFO_SESSION_FAILED_B, BOOTCTL_SLOT_A) ==
          BOOTINFO_SESSION_FAILED_B);
}

static void test_switch(void) {
    // To the other, kept slot: it becomes the active one; the running one from a USB install is kept
    bootctl_t b = BOOTCTL_DEFAULT;
    b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_KEPT;
    CHECK(slots_switch(&b, BOOTCTL_SLOT_A, BOOTCTL_SLOT_B) == SLOTS_SWITCHED && b.active_slot == BOOTCTL_SLOT_B);
    CHECK(b.slot_state[BOOTCTL_SLOT_A] == BOOTCTL_STATE_KEPT);
    CHECK(slots_switch(&b, BOOTCTL_SLOT_B, BOOTCTL_SLOT_B) == SLOTS_NOTHING_TO_DO);
    // Back from a trial: the trial is abandoned, unkept, and isn't a fallback
    b = updated_to_b();
    CHECK(slots_switch(&b, BOOTCTL_SLOT_B, BOOTCTL_SLOT_A) == SLOTS_SWITCHED);
    CHECK(b.active_slot == BOOTCTL_SLOT_A && b.pending_slot == BOOTCTL_NONE);
    CHECK(b.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_TRIAL);
    boot_inputs_t in = {.had_por = true, .bootctl = &b};
    boot_decision_t d = boot_decide(&in);
    CHECK(d.slots[0] == BOOTCTL_SLOT_A && d.slots[1] == BOOTCTL_NONE);
    // To an unkept or failed slot: it goes on trial again
    CHECK(slots_switch(&b, BOOTCTL_SLOT_A, BOOTCTL_SLOT_B) == SLOTS_SWITCHED && b.pending_slot == BOOTCTL_SLOT_B);
    b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_FAILED;
    b.pending_slot = BOOTCTL_NONE;
    CHECK(slots_switch(&b, BOOTCTL_SLOT_A, BOOTCTL_SLOT_B) == SLOTS_SWITCHED);
    CHECK(b.pending_slot == BOOTCTL_SLOT_B && b.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_TRIAL);
    CHECK(b.active_slot == BOOTCTL_SLOT_A);
    // To a slot installed over USB (it may hold anything): on trial, not straight to active
    b = BOOTCTL_DEFAULT;
    b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_INSTALLED;
    CHECK(slots_switch(&b, BOOTCTL_SLOT_A, BOOTCTL_SLOT_B) == SLOTS_SWITCHED);
    CHECK(b.active_slot == BOOTCTL_SLOT_A && b.pending_slot == BOOTCTL_SLOT_B &&
          b.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_TRIAL);
    // Never to an empty slot
    b.pending_slot = BOOTCTL_NONE;
    b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_EMPTY;
    b.pending_slot = BOOTCTL_NONE;
    const bootctl_t before = b;
    CHECK(slots_switch(&b, BOOTCTL_SLOT_A, BOOTCTL_SLOT_B) == SLOTS_NOT_BOOTABLE);
    CHECK(memcmp(&b, &before, sizeof(b)) == 0);
}

// Phase 3 review finding 1: updates only from the active firmware, into the other slot
static void test_update_target(void) {
    bootctl_t b = BOOTCTL_DEFAULT;
    uint8_t t = 9;
    CHECK(slots_update_target(&b, BOOTCTL_SLOT_A, &t) == SLOTS_UPDATE_OK && t == BOOTCTL_SLOT_B);
    b = updated_to_b();                     // Running B on trial: A is its fallback
    CHECK(slots_update_target(&b, BOOTCTL_SLOT_B, &t) == SLOTS_UPDATE_ON_TRIAL);
    b = BOOTCTL_DEFAULT;                    // Running B in place of the active A (or a switch under way)
    b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_KEPT;
    CHECK(slots_update_target(&b, BOOTCTL_SLOT_B, &t) == SLOTS_UPDATE_NOT_ACTIVE);
}

// Phase 3 review finding 1, the UI path: "Boot slot B" to a B without an image left A running in
// place of the active B, and the next update went into B without a trial
static void test_running_in_place_of_the_active_slot(void) {
    bootctl_t b = BOOTCTL_DEFAULT;
    b.slot_state[BOOTCTL_SLOT_A] = BOOTCTL_STATE_KEPT;
    b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_KEPT;
    b.active_slot = BOOTCTL_SLOT_B;         // B active, but rom_chain_image() refused it: A starts second
    boot_inputs_t in = {.had_por = true, .bootctl = &b};
    boot_decision_t d = boot_decide(&in);
    CHECK(d.slots[0] == BOOTCTL_SLOT_B && d.slots[1] == BOOTCTL_SLOT_A);
    const uint32_t flags = d.flags[1] | BOOTINFO_FALLBACK;     // As the loader passes it on
    CHECK(slots_at_start(&b, BOOTCTL_SLOT_A, flags));
    CHECK(b.active_slot == BOOTCTL_SLOT_A && b.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_EMPTY);
    uint8_t t;
    CHECK(slots_update_target(&b, BOOTCTL_SLOT_A, &t) == SLOTS_UPDATE_OK && t == BOOTCTL_SLOT_B);
    // ...and an update into B is a trial again
    b.slot_state[BOOTCTL_SLOT_B] = BOOTCTL_STATE_TRIAL;
    b.pending_slot = BOOTCTL_SLOT_B;
    d = boot_decide(&in);
    CHECK(d.slots[0] == BOOTCTL_SLOT_B && (d.flags[0] & BOOTINFO_TRIAL));
    // A trial slot without an image isn't taken over by the fallback rule: the trial just failed
    bootctl_t c = updated_to_b();
    CHECK(slots_at_start(&c, BOOTCTL_SLOT_A, BOOTINFO_FALLBACK | BOOTINFO_TRIAL_FAILED));
    CHECK(c.active_slot == BOOTCTL_SLOT_A && c.slot_state[BOOTCTL_SLOT_B] == BOOTCTL_STATE_FAILED);
}

int main(void) {
    test_keep();
    test_keep_leaves_a_failed_slot_failed();
    test_trial_failed();
    test_crash_fallback();
    test_update_kept_after_a_power_on();
    test_failed_update_goes_back_for_good();
    test_crash_loop_after_keeping();
    test_switch();
    test_a_new_trial_in_the_same_session();
    test_update_target();
    test_running_in_place_of_the_active_slot();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("slots: all tests passed\n");
    return 0;
}
