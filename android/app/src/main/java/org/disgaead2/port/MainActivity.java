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
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
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
    private static final int PICK_ISO = 1, PICK_CORE = 2;
    private static final String PREFS = "d2", KEY_ISO = "iso_uri";

    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView info, iso, coreInfo, logView;
    private LinearLayout panelView;
    private boolean coreStarted;
    private SecondScreen second;
    private DisplayManager displays;
    private int buttons;
    private float lx, ly, rx, ry;
    private int hat;   // D-pad reported as a hat axis (bits 1024..8192)

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
        // Fix the buffer to the Activity's landscape size: on a portrait-mounted panel the default buffer is portrait and
        // would be stretched, squashing the picture.
        surfaceView = surface;
        root.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> {
            int w = r - l, h = b - t;
            if (w > 0 && h > 0) { viewW = w; viewH = h; applyBufferSize(); }
        });

        // Home screen, laid out like the Windows launcher: title, status pill, a big red PLAY button, blue secondary
        // buttons and purple utility buttons, with original artwork on the right (no game art or logos).
        final int ink = 0xFF2A2140;
        LinearLayout panel = new LinearLayout(this);
        panelView = panel;
        panel.setOrientation(LinearLayout.HORIZONTAL);
        panel.setBackground(new android.graphics.drawable.GradientDrawable(
                android.graphics.drawable.GradientDrawable.Orientation.TL_BR, new int[]{0xFFFFFBF0, 0xFFF3ECFF}));
        info = new TextView(this);
        info.setTextColor(0xFF6B6285);
        info.setTextSize(12);
        iso = new TextView(this);
        iso.setTextColor(0xFFB45F00);
        iso.setTextSize(16);
        iso.setTypeface(null, android.graphics.Typeface.BOLD);
        iso.setSingleLine(false);
        iso.setPadding(dp(18), dp(10), dp(18), dp(10));
        iso.setBackground(glass(0xFFFFFFFF, 0xFF2A2140, 22));
        coreInfo = new TextView(this);
        coreInfo.setTextColor(0xFF4A4263);
        coreInfo.setTextSize(13);
        logView = new TextView(this);
        logView.setTextColor(0xFF6B6285);
        logView.setTextSize(10);
        logView.setTypeface(android.graphics.Typeface.MONOSPACE);

        TextView title = new TextView(this);
        title.setText("DISGAEA D2");
        title.setTextColor(ink);
        title.setTextSize(40);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        TextView subtitle = new TextView(this);
        subtitle.setText(R.string.home_subtitle);
        subtitle.setTextColor(0xFFD2342F);
        subtitle.setTextSize(18);
        subtitle.setTypeface(null, android.graphics.Typeface.BOLD);

        Button play = chunkyButton(getString(R.string.home_play), 0xFFFF5E73, 0xFFD12A3F, 26);
        play.setOnClickListener(v -> startCore());
        Button choose = chunkyButton(getString(R.string.choose_iso), 0xFF5B9BFF, 0xFF2554C7, 18);
        choose.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, PICK_ISO);
        });
        Button settings = chunkyButton(getString(R.string.home_settings), 0xFF5B9BFF, 0xFF2554C7, 18);
        settings.setOnClickListener(v -> showGameMenu(false));
        Button chooseCore = chunkyButton(getString(R.string.choose_core), 0xFFA27BFF, 0xFF6A3FD6, 16);
        chooseCore.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            startActivityForResult(intent, PICK_CORE);
        });
        Button share = chunkyButton(getString(R.string.share_log), 0xFFA27BFF, 0xFF6A3FD6, 16);
        share.setOnClickListener(v -> {
            Intent send = new Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, logTail(120, 200));
            startActivity(Intent.createChooser(send, getString(R.string.share_log)));
        });

        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setPadding(dp(36), dp(28), dp(24), dp(20));
        left.addView(title);
        left.addView(subtitle);
        LinearLayout.LayoutParams pillLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        pillLp.topMargin = dp(18);
        left.addView(iso, pillLp);
        LinearLayout.LayoutParams playLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(84));
        playLp.topMargin = dp(14);
        left.addView(play, playLp);
        LinearLayout row1 = new LinearLayout(this);
        row1.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams half = new LinearLayout.LayoutParams(0, dp(64), 1f);
        half.rightMargin = dp(10);
        row1.addView(choose, half);
        row1.addView(settings, new LinearLayout.LayoutParams(0, dp(64), 1f));
        LinearLayout.LayoutParams row1Lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        row1Lp.topMargin = dp(10);
        left.addView(row1, row1Lp);
        LinearLayout row2 = new LinearLayout(this);
        row2.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams half2 = new LinearLayout.LayoutParams(0, dp(52), 1f);
        half2.rightMargin = dp(10);
        row2.addView(chooseCore, half2);
        row2.addView(share, new LinearLayout.LayoutParams(0, dp(52), 1f));
        LinearLayout.LayoutParams row2Lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        row2Lp.topMargin = dp(8);
        left.addView(row2, row2Lp);
        LinearLayout.LayoutParams coreLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        coreLp.topMargin = dp(16);
        left.addView(coreInfo, coreLp);
        left.addView(logView);
        TextView footer = new TextView(this);
        String version = "";
        try { version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName; } catch (Exception ignored) { }
        footer.setText(getString(R.string.home_footer, version));
        footer.setTextColor(0xFF6B6285);
        footer.setTextSize(12);
        LinearLayout.LayoutParams footerLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        footerLp.topMargin = dp(12);
        left.addView(info);
        left.addView(footer, footerLp);
        android.widget.ScrollView leftScroll = new android.widget.ScrollView(this);
        leftScroll.setFillViewport(true);
        leftScroll.addView(left);
        panel.addView(leftScroll, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 5f));
        panel.addView(new ArtView(this), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 6f));
        root.addView(panel, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        setContentView(root);
        showIso(getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_ISO, null));
        showCore();

        displays = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        displays.registerDisplayListener(this, handler);
        updateSecondScreen();
        handler.post(refresh);
    }

    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            String vulkan = NativeBridge.info();
            info.setText(vulkan);
            if (coreStarted) logView.setText(logTail(0, 10));
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

    private File coreFile() { return new File(getFilesDir(), "libd2core.so"); }

    private File gameRoot() { return new File(getExternalFilesDir(null), "game"); }

    private static final String KEY_SCALE = "scale_pct", KEY_FPS = "show_fps", KEY_EARLY = "early_flip";
    private static final int[] SCALES = {50, 60, 75, 100};

    private SurfaceView surfaceView;
    private int viewW, viewH;

    // Internal resolution: the surface buffer is made smaller than the screen and Android's compositor scales it up
    // (free on the GPU), so the core simply renders into a smaller window.
    private void applyBufferSize() {
        if (surfaceView == null || viewW <= 0 || viewH <= 0) return;
        int pct = scalePct();
        int w = Math.max(64, viewW * pct / 100), h = Math.max(64, viewH * pct / 100);
        surfaceView.getHolder().setFixedSize(w, h);
    }

    // Image filters (same set as the Windows panel): level 0..3 each, FXAA 0/1. Saved under "f_<key>".
    private static final String[] FILTER_KEYS = {"sharpen", "scanlines", "crtmask", "vignette", "vivid", "aa"};

    private int filterLevel(String key) { return getSharedPreferences(PREFS, MODE_PRIVATE).getInt("f_" + key, 0); }

    private String filterLabel(String key) {
        int level = filterLevel(key);
        int name = key.equals("sharpen") ? R.string.filter_sharpen : key.equals("scanlines") ? R.string.filter_scanlines
                : key.equals("crtmask") ? R.string.filter_crt : key.equals("vignette") ? R.string.filter_vignette
                : key.equals("vivid") ? R.string.filter_vivid : R.string.filter_fxaa;
        String value = key.equals("aa") ? getString(level != 0 ? R.string.level_on : R.string.level_off)
                : getString(level == 0 ? R.string.level_off : level == 1 ? R.string.level_low : level == 2 ? R.string.level_mid : R.string.level_high);
        return getString(name) + ": " + value;
    }

    private int scalePct() { return getSharedPreferences(PREFS, MODE_PRIVATE).getInt(KEY_SCALE, 100); }

    private boolean showFps() { return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_FPS, false); }

    private boolean earlyFlip() { return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_EARLY, true); }

    private String earlyLabel() { return getString(earlyFlip() ? R.string.early_on : R.string.early_off); }

    private String resolutionLabel() { return getString(R.string.internal_resolution, scalePct()); }

    private String fpsLabel() { return getString(showFps() ? R.string.fps_on : R.string.fps_off); }

    // The core reads this file from its working directory (next to the log) when the first frame is presented.
    private void writeSettings() {
        try (OutputStream out = new FileOutputStream(new File(getExternalFilesDir(null), "d2_android.cfg"))) {
            StringBuilder text = new StringBuilder("scale=100\nbufscale=" + scalePct() + "\nearlyflip=" + (earlyFlip() ? 1 : 0) + "\nshowfps=" + (showFps() ? 1 : 0) + "\n");
            for (String key : FILTER_KEYS) text.append(key).append('=').append(filterLevel(key)).append('\n');
            out.write(text.toString().getBytes("UTF-8"));
        } catch (java.io.IOException ignored) { }
    }

    private File logFile() { return new File(getExternalFilesDir(null), "d2core.log"); }

    private void showCore() {
        File core = coreFile();
        String text = core.exists() ? getString(R.string.core_ready, String.valueOf(core.length() / (1024 * 1024)))
                : getString(R.string.core_missing);
        coreInfo.setText(text + "\n" + getString(R.string.game_folder, gameRoot().getAbsolutePath()));
    }

    /** Log without the per-frame spam: the first `headLines` interesting lines, a frame counter and the last `lines` lines. */
    private String logTail(int headLines, int lines) {
        File log = logFile();
        if (!log.exists()) return "";
        try (RandomAccessFile f = new RandomAccessFile(log, "r")) {
            long size = f.length(), from = Math.max(0, size - 6L * 1024 * 1024);
            byte[] data = new byte[(int) (size - from)];
            f.seek(from);
            f.readFully(data);
            String[] all = new String(data, StandardCharsets.UTF_8).split("\n");
            java.util.ArrayList<String> kept = new java.util.ArrayList<>();
            String lastFrame = null;
            for (String line : all) {
                if (line.startsWith("[rsx] presented guest frame")) { lastFrame = line; continue; }
                kept.add(line);
            }
            StringBuilder out = new StringBuilder();
            if (from > 0) out.append("(log truncated: only the last 6 MB)\n");
            int head = Math.min(headLines, kept.size());
            for (int i = 0; i < head; i++) out.append(kept.get(i)).append('\n');
            int tailStart = Math.max(head, kept.size() - lines);
            if (tailStart > head) out.append("... ").append(tailStart - head).append(" lines omitted ...\n");
            for (int i = tailStart; i < kept.size(); i++) out.append(kept.get(i)).append('\n');
            if (lastFrame != null) out.append(lastFrame).append('\n');
            return out.toString();
        } catch (java.io.IOException e) {
            return "(log: " + e + ")";
        }
    }

    private static File child(File dir, String name) {
        File[] list = dir.listFiles();
        if (list == null) return null;
        for (File f : list) if (f.getName().equalsIgnoreCase(name)) return f;
        return null;
    }

    /** Finds the folder that contains PS3_GAME (up to 3 levels deep, any letter case). */
    private static File findGameRoot(File dir, int depth) {
        if (child(dir, "PS3_GAME") != null) return dir;
        File[] list = dir.listFiles();
        if (depth <= 0 || list == null) return null;
        for (File f : list) {
            if (!f.isDirectory()) continue;
            File found = findGameRoot(f, depth - 1);
            if (found != null) return found;
        }
        return null;
    }

    private static String listing(File dir, String indent, int depth) {
        StringBuilder out = new StringBuilder();
        File[] list = dir.listFiles();
        if (list == null) return indent + "(cannot read " + dir + ")\n";
        java.util.Arrays.sort(list);
        int shown = 0;
        for (File f : list) {
            if (shown++ >= 12) { out.append(indent).append("...\n"); break; }
            out.append(indent).append(f.getName()).append(f.isDirectory() ? "/" : "").append('\n');
            if (f.isDirectory() && depth > 0) out.append(listing(f, indent + "  ", depth - 1));
        }
        return out.toString();
    }

    private void startCore() {
        if (coreStarted) return;
        if (!coreFile().exists()) { logView.setText(R.string.core_missing); return; }
        File root = findGameRoot(gameRoot(), 3);
        File usr = null, eboot = null;
        if (root != null) {
            File game = child(root, "PS3_GAME");
            usr = game == null ? null : child(game, "USRDIR");
            eboot = usr == null ? null : child(usr, "EBOOT.elf");
        }
        if (eboot == null) {
            logView.setText(getString(R.string.game_missing) + "\n" + gameRoot() + "\n" + listing(gameRoot(), "  ", 2));
            return;
        }
        writeSettings();
        String error = NativeBridge.startCore(coreFile().getAbsolutePath(), eboot.getAbsolutePath(),
                root.getAbsolutePath(), logFile().getAbsolutePath());
        if (error != null) { logView.setText(error); return; }
        coreStarted = true;
        panelView.setVisibility(android.view.View.GONE); // the game gets the whole screen; Back brings the panel back
    }

    private void importCore(Uri uri) {
        new Thread(() -> {
            String message = null;
            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream out = new FileOutputStream(coreFile())) {
                byte[] buffer = new byte[1 << 20];
                int n;
                while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
            } catch (java.io.IOException | RuntimeException e) {
                message = "import: " + e;
            }
            final String shown = message;
            handler.post(() -> { showCore(); if (shown != null) logView.setText(shown); });
        }).start();
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == PICK_CORE && result == RESULT_OK && data != null && data.getData() != null) {
            importCore(data.getData());
            return;
        }
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
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return 16384;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return 32768;
            case KeyEvent.KEYCODE_DPAD_UP: return 1024;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 2048;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 4096;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 8192;
            default: return 0;
        }
    }

    // ---- In-game menu (Back button, or both stick clicks L3+R3 on a handheld) -------------------------------------------------
    private android.app.Dialog gameMenu;

    private void releaseGameInput() {
        buttons = 0; hat = 0; lx = ly = rx = ry = 0;
        NativeBridge.setInput(0, 0, 0, 0, 0);
    }

    private Button menuButton(LinearLayout column, String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        column.addView(b, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        return b;
    }

    // ---- Game menu: dark "glass" panel with tabs (Video / Filters / System), like the Windows panel ----
    // Chunky "game" button: dark outline, glossy top, and a darker slab underneath that the button sinks into when pressed.
    private Button chunkyButton(String text, int top, int bottom, float textSp) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextColor(Color.WHITE);
        b.setTextSize(textSp);
        b.setTypeface(null, android.graphics.Typeface.BOLD);
        b.setShadowLayer(dp(2), 0, dp(1.5f), 0xFF1A1030);
        b.setPadding(dp(12), dp(4), dp(12), dp(10));
        b.setMinHeight(0);
        b.setMinimumHeight(0);
        b.setStateListAnimator(null);
        b.setBackground(new android.graphics.drawable.StateListDrawable() {{
            addState(new int[]{android.R.attr.state_pressed}, chunkyLayers(top, bottom, true, false));
            addState(new int[]{android.R.attr.state_focused}, chunkyLayers(top, bottom, false, true));
            addState(new int[]{}, chunkyLayers(top, bottom, false, false));
        }});
        return b;
    }

    private android.graphics.drawable.Drawable chunkyLayers(int top, int bottom, boolean pressed, boolean focused) {
        android.graphics.drawable.GradientDrawable slab = new android.graphics.drawable.GradientDrawable();
        slab.setColor(0xFF1A1030);
        slab.setCornerRadius(dp(18));
        android.graphics.drawable.GradientDrawable face = new android.graphics.drawable.GradientDrawable(
                android.graphics.drawable.GradientDrawable.Orientation.TOP_BOTTOM, new int[]{top, bottom});
        face.setCornerRadius(dp(18));
        face.setStroke(dp(focused ? 4 : 3), focused ? 0xFFFFFFFF : 0xFF2A2140);
        android.graphics.drawable.GradientDrawable gloss = new android.graphics.drawable.GradientDrawable();
        gloss.setColor(0x33FFFFFF);
        gloss.setCornerRadius(dp(10));
        android.graphics.drawable.LayerDrawable layers = new android.graphics.drawable.LayerDrawable(
                new android.graphics.drawable.Drawable[]{slab, face, gloss});
        int lift = pressed ? dp(1) : dp(5);
        layers.setLayerInset(0, 0, dp(5), 0, 0);
        layers.setLayerInset(1, 0, pressed ? dp(4) : 0, 0, lift);
        layers.setLayerInset(2, dp(12), (pressed ? dp(4) : 0) + dp(6), dp(12), lift + dp(34));
        return layers;
    }

    // Original artwork for the home screen: a purple-to-crimson field with a curved left edge, soft bokeh and sparkles.
    private static final class ArtView extends View {
        private final android.graphics.Paint paint = new android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG);
        private final android.graphics.Path edge = new android.graphics.Path();
        private android.graphics.Bitmap picture;
        ArtView(android.content.Context c) {
            super(c);
            setFocusable(false);
            try { picture = android.graphics.BitmapFactory.decodeResource(c.getResources(), R.drawable.home_art); } catch (Throwable ignored) { }
        }
        @Override protected void onDraw(android.graphics.Canvas canvas) {
            int w = getWidth(), h = getHeight();
            edge.reset();
            edge.moveTo(w * 0.16f, 0);
            edge.lineTo(w, 0);
            edge.lineTo(w, h);
            edge.lineTo(w * 0.04f, h);
            edge.cubicTo(w * 0.30f, h * 0.72f, -w * 0.06f, h * 0.30f, w * 0.16f, 0);
            canvas.save();
            canvas.clipPath(edge);
            paint.setShader(new android.graphics.LinearGradient(0, 0, w, h, 0xFF2A1250, 0xFF9B2350, android.graphics.Shader.TileMode.CLAMP));
            canvas.drawRect(0, 0, w, h, paint);
            paint.setShader(null);
            if (picture != null) {   // the game's title screen, cropped to fill the panel (centred, no stretching)
                float scale = Math.max((float) w / picture.getWidth(), (float) h / picture.getHeight());
                float sw = w / scale, sh = h / scale;
                android.graphics.Rect src = new android.graphics.Rect((int) ((picture.getWidth() - sw) / 2), (int) ((picture.getHeight() - sh) / 2),
                        (int) ((picture.getWidth() + sw) / 2), (int) ((picture.getHeight() + sh) / 2));
                canvas.drawBitmap(picture, src, new android.graphics.Rect(0, 0, w, h), paint);
                canvas.restore();
                return;
            }
            int[] fills = {0x33FFC9E6, 0x2AB8A0FF, 0x26FFD98A, 0x30FF7AA8};
            float[][] orbs = {{0.30f, 0.22f, 0.30f}, {0.78f, 0.30f, 0.38f}, {0.55f, 0.78f, 0.44f}, {0.92f, 0.86f, 0.26f}, {0.42f, 0.50f, 0.16f}};
            for (int i = 0; i < orbs.length; i++) {
                paint.setColor(fills[i % fills.length]);
                canvas.drawCircle(w * orbs[i][0], h * orbs[i][1], Math.min(w, h) * orbs[i][2], paint);
            }
            paint.setColor(0xCCFFFFFF);
            float[][] stars = {{0.34f, 0.12f, 14}, {0.70f, 0.10f, 20}, {0.88f, 0.55f, 16}, {0.52f, 0.34f, 10}, {0.30f, 0.88f, 16}, {0.74f, 0.70f, 12}};
            for (float[] st : stars) {
                float cx = w * st[0], cy = h * st[1], r = st[2] * getResources().getDisplayMetrics().density;
                android.graphics.Path star = new android.graphics.Path();
                star.moveTo(cx, cy - r); star.quadTo(cx, cy, cx + r, cy); star.quadTo(cx, cy, cx, cy + r);
                star.quadTo(cx, cy, cx - r, cy); star.quadTo(cx, cy, cx, cy - r);
                canvas.drawPath(star, paint);
            }
            canvas.restore();
        }
    }

    private int dp(float v) { return Math.round(v * getResources().getDisplayMetrics().density); }

    private android.graphics.drawable.Drawable glass(int fill, int stroke, float radiusDp) {
        android.graphics.drawable.GradientDrawable d = new android.graphics.drawable.GradientDrawable();
        d.setColor(fill);
        d.setCornerRadius(dp(radiusDp));
        d.setStroke(dp(1), stroke);
        return d;
    }

    // Button with a translucent fill; the focused state (D-pad / controller) gets a bright accent outline.
    private Button glassButton(String text, boolean selected, int accent) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextColor(Color.WHITE);
        b.setTextSize(16);
        b.setPadding(dp(16), dp(10), dp(16), dp(10));
        b.setMinHeight(0);
        b.setMinimumHeight(dp(44));
        android.graphics.drawable.StateListDrawable states = new android.graphics.drawable.StateListDrawable();
        states.addState(new int[]{android.R.attr.state_focused}, glass(0x55FFFFFF, 0xFFFFFFFF, 12));
        states.addState(new int[]{android.R.attr.state_pressed}, glass(0x66FFFFFF, 0xCCFFFFFF, 12));
        states.addState(new int[]{}, selected ? glass((accent & 0x00FFFFFF) | 0x99000000, 0x88FFFFFF, 12) : glass(0x22FFFFFF, 0x33FFFFFF, 12));
        b.setBackground(states);
        return b;
    }

    private void addRow(LinearLayout column, Button b) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(8);
        column.addView(b, lp);
    }

    private void showGameMenu(boolean inGame) {
        if (gameMenu != null && gameMenu.isShowing()) return;
        releaseGameInput();   // key releases now go to the dialog, so nothing may stay held in the game
        final int accent = 0xFF7A1B3D;
        final LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(16), dp(20), dp(16));
        root.setBackground(glass(0xCC160B28, 0x55FFFFFF, 24));
        TextView title = new TextView(this);
        title.setText(inGame ? R.string.menu_title : R.string.home_settings);
        title.setTextColor(Color.WHITE);
        title.setTextSize(22);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        root.addView(title);

        final LinearLayout tabs = new LinearLayout(this);
        tabs.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(tabs, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        final android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        final LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(content);
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        scrollLp.topMargin = dp(8);
        root.addView(scroll, scrollLp);   // only the options scroll; the Resume button below always stays visible
        final Button resume = glassButton(getString(R.string.menu_resume), true, accent);
        addRow(root, resume);

        final android.app.Dialog dialog = new android.app.Dialog(this, android.R.style.Theme_Material_Dialog_NoActionBar);
        gameMenu = dialog;
        final int[] tabNames = {R.string.tab_video, R.string.menu_filters, R.string.tab_system};
        final int tabCount = inGame ? 3 : 2;
        final Button[] tabButtons = new Button[tabCount];
        final int[] current = {0};
        final Runnable[] fill = new Runnable[1];

        fill[0] = () -> {
            content.removeAllViews();
            for (int i = 0; i < tabCount; i++) tabButtons[i].setBackground(null);
            for (int i = 0; i < tabCount; i++) {
                Button t = tabButtons[i];
                android.graphics.drawable.StateListDrawable st = new android.graphics.drawable.StateListDrawable();
                st.addState(new int[]{android.R.attr.state_focused}, glass(0x55FFFFFF, 0xFFFFFFFF, 12));
                st.addState(new int[]{}, i == current[0] ? glass((accent & 0x00FFFFFF) | 0xAA000000, 0x99FFFFFF, 12) : glass(0x11FFFFFF, 0x22FFFFFF, 12));
                t.setBackground(st);
            }
            if (current[0] == 0) {
                Button resolution = glassButton(resolutionLabel(), false, accent);
                Button fps = glassButton(fpsLabel(), false, accent);
                Button early = glassButton(earlyLabel(), false, accent);
                resolution.setOnClickListener(v -> {
                    int next = 0;
                    for (int i = 0; i < SCALES.length; i++) if (SCALES[i] == scalePct()) next = (i + 1) % SCALES.length;
                    getSharedPreferences(PREFS, MODE_PRIVATE).edit().putInt(KEY_SCALE, SCALES[next]).apply();
                    resolution.setText(resolutionLabel());
                    applyBufferSize();
                    writeSettings();
                });
                fps.setOnClickListener(v -> {
                    getSharedPreferences(PREFS, MODE_PRIVATE).edit().putBoolean(KEY_FPS, !showFps()).apply();
                    fps.setText(fpsLabel());
                    writeSettings();   // the core notices the new file within a couple of seconds
                });
                early.setOnClickListener(v -> {
                    getSharedPreferences(PREFS, MODE_PRIVATE).edit().putBoolean(KEY_EARLY, !earlyFlip()).apply();
                    early.setText(earlyLabel());
                    writeSettings();
                });
                addRow(content, resolution); addRow(content, fps); addRow(content, early);
            } else if (current[0] == 1) {
                final Button[] rows = new Button[FILTER_KEYS.length];
                for (int i = 0; i < FILTER_KEYS.length; i++) {
                    final String key = FILTER_KEYS[i];
                    final int index = i;
                    rows[i] = glassButton(filterLabel(key), false, accent);
                    rows[i].setOnClickListener(v -> {
                        int max = key.equals("aa") ? 1 : 3;
                        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putInt("f_" + key, (filterLevel(key) + 1) % (max + 1)).apply();
                        rows[index].setText(filterLabel(key));
                        writeSettings();
                    });
                    addRow(content, rows[i]);
                }
                Button reset = glassButton(getString(R.string.filters_reset), false, accent);
                reset.setOnClickListener(v -> {
                    SharedPreferences.Editor e = getSharedPreferences(PREFS, MODE_PRIVATE).edit();
                    for (String key : FILTER_KEYS) e.putInt("f_" + key, 0);
                    e.apply();
                    for (int i = 0; i < FILTER_KEYS.length; i++) rows[i].setText(filterLabel(FILTER_KEYS[i]));
                    writeSettings();
                });
                addRow(content, reset);
            } else {
                Button diagnostics = glassButton(getString(R.string.menu_diagnostics), false, accent);
                Button quit = glassButton(getString(R.string.menu_quit), false, 0xFFB3261E);
                diagnostics.setOnClickListener(v -> { dialog.dismiss(); panelView.setVisibility(View.VISIBLE); });
                final boolean[] armed = {false};
                quit.setOnClickListener(v -> {
                    if (!armed[0]) {
                        armed[0] = true;
                        quit.setText(R.string.menu_quit_confirm);
                        quit.setBackground(glass(0xCCB3261E, 0xFFFFFFFF, 12));
                        return;
                    }
                    quitGame(dialog, root);
                });
                addRow(content, diagnostics); addRow(content, quit);
            }
        };
        for (int i = 0; i < tabCount; i++) {
            final int index = i;
            Button t = glassButton(getString(tabNames[i]), false, accent);
            t.setOnClickListener(v -> { current[0] = index; fill[0].run(); });
            tabButtons[i] = t;
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            lp.topMargin = dp(10);
            lp.rightMargin = i < tabCount - 1 ? dp(8) : 0;
            tabs.addView(t, lp);
        }
        fill[0].run();
        resume.setOnClickListener(v -> dialog.dismiss());
        dialog.setContentView(root);
        dialog.setOnDismissListener(d -> { if (gameMenu == dialog) gameMenu = null; });
        android.view.Window window = dialog.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
            window.setDimAmount(0.45f);
            if (android.os.Build.VERSION.SDK_INT >= 31) {   // real frosted glass where the system supports it
                window.addFlags(android.view.WindowManager.LayoutParams.FLAG_BLUR_BEHIND);
                window.getAttributes().setBlurBehindRadius(24);
            }
        }
        dialog.show();
        if (window != null) {
            android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
            window.setLayout(Math.min(dp(520), dm.widthPixels * 9 / 10), dm.heightPixels * 85 / 100);
        }
        tabButtons[0].requestFocus();
    }

    // Same order as the Windows build: stop new save/load operations, wait until none is being written, then end the process
    // (the game's own threads cannot be joined, so the process itself is what closes).
    private void quitGame(android.app.Dialog dialog, LinearLayout column) {
        column.removeAllViews();
        TextView saving = new TextView(this);
        saving.setText(R.string.menu_quitting);
        saving.setTextColor(Color.WHITE);
        saving.setTextSize(18);
        column.addView(saving);
        dialog.setCancelable(false);
        final int[] tries = {0};
        handler.post(new Runnable() {
            @Override public void run() {
                int state = NativeBridge.requestStop();
                if (state != 0 || ++tries[0] > 120) {   // quiet, an older core without the gate, or ~30 s of waiting
                    finishAndRemoveTask();
                    android.os.Process.killProcess(android.os.Process.myPid());
                    return;
                }
                handler.postDelayed(this, 250);
            }
        });
    }

    @Override
    public void onBackPressed() {
        if (coreStarted) {
            if (panelView.getVisibility() == View.VISIBLE) { panelView.setVisibility(View.GONE); return; }
            showGameMenu(true);
            return;
        }
        super.onBackPressed();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int bit = mask(event.getKeyCode());
        if (bit == 0) return super.dispatchKeyEvent(event);
        if (event.getAction() == KeyEvent.ACTION_DOWN) buttons |= bit;
        else if (event.getAction() == KeyEvent.ACTION_UP) buttons &= ~bit;
        if (coreStarted && (buttons & (16384 | 32768)) == (16384 | 32768)) {   // L3 + R3: open the game menu
            showGameMenu(true);
            return true;
        }
        NativeBridge.setInput(buttons | hat, lx, ly, rx, ry);
        return true;
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                && event.getAction() == MotionEvent.ACTION_MOVE) {
            lx = dead(event.getAxisValue(MotionEvent.AXIS_X));
            ly = dead(event.getAxisValue(MotionEvent.AXIS_Y));
            rx = dead(event.getAxisValue(MotionEvent.AXIS_Z));
            ry = dead(event.getAxisValue(MotionEvent.AXIS_RZ));
            float hx = event.getAxisValue(MotionEvent.AXIS_HAT_X), hy = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
            hat = (hx < -0.5f ? 4096 : 0) | (hx > 0.5f ? 8192 : 0) | (hy < -0.5f ? 1024 : 0) | (hy > 0.5f ? 2048 : 0);
            NativeBridge.setInput(buttons | hat, lx, ly, rx, ry);
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
