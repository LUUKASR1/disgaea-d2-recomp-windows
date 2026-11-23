package org.disgaead2.port;

import android.view.Surface;

/** JNI entry points of libd2android (Vulkan surface layer and gamepad state). */
final class NativeBridge {
    static {
        System.loadLibrary("d2android");
    }

    private NativeBridge() {}

    static native void setSurface(Surface surface);

    static native void setInput(int buttons, float lx, float ly, float rx, float ry);

    static native String info();

    /** Custom Vulkan driver (Turnip, Adreno only) used by the next startCore. Empty strings = the system driver. */
    static native void setCustomDriver(String nativeLibDir, String driverDir, String driverName);

    /** Loads libd2core.so and runs the game on a worker thread. Returns an error text, or null when started. */
    static native String startCore(String soPath, String ebootPath, String vfsRoot, String logPath);

    /** Quit gate: 1 when no save is being written (safe to end the process), 0 while one is, -1 without a (new enough) core. */
    static native int requestStop();

    /** Cheat engine (inside the core). cheatsState: "key=value" lines, "ok=0" without a published snapshot. */
    static native String cheatsState();
    /** "key|label|min|max" per field of a scope (0 = general, 1 = character). */
    static native String cheatsFields(int scope);
    static native void cheatsSelectUnit(int unit);
    static native int cheatsSetCharacter(int unit, String key, long value, long generation);
    /** 0 = queued, -1 = refused, -2 = no cheat engine in this core. */
    static native int cheatsSetToggle(String name, boolean on);
    static native int cheatsSetGeneral(String key, long value);
    static native int cheatsSetExp(int multiplier);
    static native int cheatsDisableAll();
    static native int cheatsPreset(boolean save, int slot);

    /** Save slots while the game runs: 1 = the save gate is held (call saveUnlock after), 0 = a save is being written, -1 = old core. */
    static native int saveLock();
    static native void saveUnlock();

    /** Next trophy event as "kind|id" (kind 0 = recorded, 1 = blocked by the trophy lock); null when none or with an older core. */
    static native String trophyEvent();

    /** -1000 while running, otherwise the value returned by the game main. */
    static native int coreResult();
}
