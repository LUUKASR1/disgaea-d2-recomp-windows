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
}
