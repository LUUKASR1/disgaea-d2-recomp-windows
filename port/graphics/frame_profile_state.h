#pragma once
/* Render-thread-owned host timing; no GPU timestamps and no per-draw clocks. */
static double d2_profile_wait_ms, d2_profile_prepare_ms, d2_profile_present_ms;
static unsigned d2_profile_wait_calls;
static int d2_lite_enabled(void);
static double d2_lite_now(void);
