#pragma once
/* Render-thread-owned host timing; no GPU timestamps and no per-draw clocks. */
static double d2_profile_wait_ms, d2_profile_prepare_ms, d2_profile_present_ms;
static unsigned d2_profile_wait_calls;
/* Expensive API calls only: cache-hit draws do not read the clock. */
static double d2_profile_setup_ms, d2_profile_targets_ms, d2_profile_draws_ms;
static double d2_profile_resource_ms, d2_profile_shader_ms, d2_profile_pso_ms;
static unsigned d2_profile_textures, d2_profile_buffers, d2_profile_resource_failures;
static unsigned d2_profile_shader_calls, d2_profile_pso_calls, d2_profile_pso_failures;
static int d2_lite_enabled(void);
static double d2_lite_now(void);
