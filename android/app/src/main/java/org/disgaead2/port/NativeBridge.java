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

    /** Loads libd2core.so and runs the game on a worker thread. Returns an error text, or null when started. */
    static native String startCore(String soPath, String ebootPath, String vfsRoot, String logPath);

    /** Quit gate: 1 when no save is being written (safe to end the process), 0 while one is, -1 without a (new enough) core. */
    static native int requestStop();

    /** Cheat engine (inside the core). cheatsState: "key=value" lines, "ok=0" without a published snapshot. */
    static native String cheatsState();
    /** "key|label|min|max" per general field. */
    static native String cheatsFields();
    /** 0 = queued, -1 = refused, -2 = no cheat engine in this core. */
    static native int cheatsSetToggle(String name, boolean on);
    static native int cheatsSetGeneral(String key, long value);
    static native int cheatsSetExp(int multiplier);
    static native int cheatsDisableAll();
    static native int cheatsPreset(boolean save, int slot);

    /** Save slots while the game runs: 1 = the save gate is held (call saveUnlock after), 0 = a save is being written, -1 = old core. */
    static native int saveLock();
    static native void saveUnlock();

    /** -1000 while running, otherwise the value returned by the game main. */
    static native int coreResult();
}
