// Android controller bridge: the app (libd2android) forwards the Activity's key / joystick state here and the
// ps3recomp cellPad backend reads it (SDL's gamepad driver needs its Java side, which this build does not have).
#include <stdatomic.h>
#include <stdint.h>

static atomic_uint g_buttons;   // CELL_PAD_CTRL_* bitmask
static atomic_uint g_axes;      // lx | ly << 8 | rx << 16 | ry << 24, 0..255, centre 128
static atomic_int  g_seen;      // set on the first forwarded state: the pad is "connected"

static uint8_t axis(float v) {
    if (v < -1.f) v = -1.f;
    if (v > 1.f) v = 1.f;
    return (uint8_t)(128.f + v * 127.f + 0.5f);
}

// Android-side bits: A1 B2 X4 Y8 L1 16 R1 32 L2 64 R2 128 Start256 Select512 Up1024 Down2048 Left4096 Right8192 L3 16384 R3 32768
__attribute__((visibility("default")))
void d2_android_pad_set(int android_buttons, float lx, float ly, float rx, float ry) {
    unsigned b = 0;
    if (android_buttons & 1) b |= 1u << 14;       // A  -> CROSS
    if (android_buttons & 2) b |= 1u << 13;       // B  -> CIRCLE
    if (android_buttons & 4) b |= 1u << 15;       // X  -> SQUARE
    if (android_buttons & 8) b |= 1u << 12;       // Y  -> TRIANGLE
    if (android_buttons & 16) b |= 1u << 10;      // L1
    if (android_buttons & 32) b |= 1u << 11;      // R1
    if (android_buttons & 64) b |= 1u << 8;       // L2
    if (android_buttons & 128) b |= 1u << 9;      // R2
    if (android_buttons & 256) b |= 1u << 3;      // START
    if (android_buttons & 512) b |= 1u << 0;      // SELECT
    if (android_buttons & 1024) b |= 1u << 4;     // UP
    if (android_buttons & 2048) b |= 1u << 6;     // DOWN
    if (android_buttons & 4096) b |= 1u << 7;     // LEFT
    if (android_buttons & 8192) b |= 1u << 5;     // RIGHT
    if (android_buttons & 16384) b |= 1u << 1;    // L3
    if (android_buttons & 32768) b |= 1u << 2;    // R3
    atomic_store(&g_buttons, b);
    atomic_store(&g_axes, (unsigned)axis(lx) | (unsigned)axis(ly) << 8 | (unsigned)axis(rx) << 16 | (unsigned)axis(ry) << 24);
    atomic_store(&g_seen, 1);
}

// Used by cellPad.c: always reports one connected pad on port 0 (the handheld's built-in controls).
void d2_android_pad_get(unsigned* buttons, unsigned char axes[4]) {
    *buttons = atomic_load(&g_buttons);
    unsigned a = atomic_load(&g_axes);
    if (!atomic_load(&g_seen)) a = 0x80808080u;
    axes[0] = (unsigned char)a; axes[1] = (unsigned char)(a >> 8); axes[2] = (unsigned char)(a >> 16); axes[3] = (unsigned char)(a >> 24);
}
