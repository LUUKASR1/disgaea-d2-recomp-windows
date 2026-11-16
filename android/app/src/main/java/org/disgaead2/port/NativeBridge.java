package org.disgaead2.port;

import android.view.Surface;

/** JNI entry points of libd2android (Vulkan surface layer and gamepad state). */
final class NativeBridge {
    static {
        System.loadLibrary("d2android");
    }

    private NativeBridge() {}

    static native void setSurface(Surface surface);

    static native void setInput(int buttons, float lx, float ly);

    static native String info();

    /** Loads libd2core.so and runs the game on a worker thread. Returns an error text, or null when started. */
    static native String startCore(String soPath, String ebootPath, String vfsRoot, String logPath);

    /** -1000 while running, otherwise the value returned by the game main. */
    static native int coreResult();
}
