package org.disgaead2.port;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.view.Display;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Milestone 0: Vulkan surface + gamepad + ISO document picker + secondary display (Thor). */
public final class MainActivity extends Activity implements DisplayManager.DisplayListener {
    private static final int PICK_ISO = 1;
    private static final String PREFS = "d2", KEY_ISO = "iso_uri";

    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView info, iso;
    private SecondScreen second;
    private DisplayManager displays;
    private int buttons;
    private float lx, ly;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        FrameLayout root = new FrameLayout(this);
        SurfaceView surface = new SurfaceView(this);
        surface.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder holder) { NativeBridge.setSurface(holder.getSurface()); }
            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                NativeBridge.setSurface(holder.getSurface());
            }
            @Override public void surfaceDestroyed(SurfaceHolder holder) { NativeBridge.setSurface(null); }
        });
        root.addView(surface);

        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(24, 16, 24, 16);
        info = new TextView(this);
        info.setTextColor(Color.WHITE);
        iso = new TextView(this);
        iso.setTextColor(Color.WHITE);
        Button choose = new Button(this);
        choose.setText(R.string.choose_iso);
        choose.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, PICK_ISO);
        });
        panel.addView(info);
        panel.addView(iso);
        panel.addView(choose);
        root.addView(panel, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.START));
        setContentView(root);
        showIso(getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_ISO, null));

        displays = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        displays.registerDisplayListener(this, handler);
        updateSecondScreen();
        handler.post(refresh);
    }

    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            String vulkan = NativeBridge.info();
            info.setText(vulkan);
            if (second != null) second.setStatus(vulkan + "\n" + getString(R.string.status_input, describeInput()));
            handler.postDelayed(this, 500);
        }
    };

    private String describeInput() {
        if (buttons == 0 && lx == 0 && ly == 0) return getString(R.string.no_pad);
        return String.format("0x%X  L(%.2f, %.2f)", buttons, lx, ly);
    }

    private void showIso(String uriText) {
        if (uriText == null) { iso.setText(R.string.no_iso); return; }
        String name = uriText;
        try (Cursor c = getContentResolver().query(Uri.parse(uriText), null, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int column = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (column >= 0) name = c.getString(column);
            }
        } catch (RuntimeException ignored) {
            // Permission may have been revoked; the name just stays as the URI.
        }
        iso.setText(getString(R.string.iso_selected, name));
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ISO || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (RuntimeException ignored) {
            // Some providers do not offer persistable grants; the choice still works for this session.
        }
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_ISO, uri.toString()).apply();
        showIso(uri.toString());
    }

    private static int mask(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BUTTON_A: return 1;
            case KeyEvent.KEYCODE_BUTTON_B: return 2;
            case KeyEvent.KEYCODE_BUTTON_X: return 4;
            case KeyEvent.KEYCODE_BUTTON_Y: return 8;
            case KeyEvent.KEYCODE_BUTTON_L1: return 16;
            case KeyEvent.KEYCODE_BUTTON_R1: return 32;
            case KeyEvent.KEYCODE_BUTTON_L2: return 64;
            case KeyEvent.KEYCODE_BUTTON_R2: return 128;
            case KeyEvent.KEYCODE_BUTTON_START: return 256;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return 512;
            case KeyEvent.KEYCODE_DPAD_UP: return 1024;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 2048;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 4096;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 8192;
            default: return 0;
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int bit = mask(event.getKeyCode());
        if (bit == 0) return super.dispatchKeyEvent(event);
        if (event.getAction() == KeyEvent.ACTION_DOWN) buttons |= bit;
        else if (event.getAction() == KeyEvent.ACTION_UP) buttons &= ~bit;
        NativeBridge.setInput(buttons, lx, ly);
        return true;
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                && event.getAction() == MotionEvent.ACTION_MOVE) {
            lx = dead(event.getAxisValue(MotionEvent.AXIS_X));
            ly = dead(event.getAxisValue(MotionEvent.AXIS_Y));
            NativeBridge.setInput(buttons, lx, ly);
            return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }

    private static float dead(float value) {
        return Math.abs(value) < 0.15f ? 0f : value;
    }

    private void updateSecondScreen() {
        Display[] list = displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        if (second != null && (list.length == 0 || second.getDisplay().getDisplayId() != list[0].getDisplayId())) {
            second.dismiss();
            second = null;
        }
        if (second == null && list.length > 0) {
            second = new SecondScreen(this, list[0]);
            second.show();
        }
    }

    @Override public void onDisplayAdded(int id) { updateSecondScreen(); }
    @Override public void onDisplayRemoved(int id) { updateSecondScreen(); }
    @Override public void onDisplayChanged(int id) {}

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(refresh);
        displays.unregisterDisplayListener(this);
        if (second != null) second.dismiss();
        super.onDestroy();
    }

    @Override
    public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }
}
