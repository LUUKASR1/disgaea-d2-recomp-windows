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
    private static final int PICK_ISO = 1, PICK_CORE = 2, PICK_SAVE_TREE = 3, PICK_SAVE_ZIP = 4, EXPORT_SAVE = 5;
    private static final String PREFS = "d2", KEY_ISO = "iso_uri";

    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView info, iso, coreInfo, logView;
    private LinearLayout panelView;
    private boolean coreStarted;
    // The game core lives as long as the process, but Android may destroy and recreate the Activity while the app is in the
    // background; a new Activity must not offer "Play" again, it must pick the running game back up.
    private static boolean coreRunning;
    private SecondScreen second;
    private DisplayManager displays;
    private int buttons;
    private float lx, ly, rx, ry;
    // On-screen controls (touch) merged with the physical controller.
    private TouchControls touchView;
    private TrophyToast trophyToast;
    private android.app.Dialog menuDialog;
    private int tButtons;
    private float tlx, tly, trx, try_;
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
        info.setTextSize(11);
        iso = new TextView(this);
        iso.setTextColor(0xFFB45F00);
        iso.setTextSize(15);
        iso.setTypeface(null, android.graphics.Typeface.BOLD);
        iso.setSingleLine(false);
        iso.setPadding(dp(16), dp(6), dp(16), dp(6));
        iso.setBackground(glass(0xFFFFFFFF, 0xFF2A2140, 22));
        coreInfo = new TextView(this);
        coreInfo.setTextColor(0xFF4A4263);
        coreInfo.setTextSize(11);
        logView = new TextView(this);
        logView.setTextColor(0xFF6B6285);
        logView.setTextSize(10);
        logView.setTypeface(android.graphics.Typeface.MONOSPACE);

        android.widget.ImageView title = new android.widget.ImageView(this);   // the game's logo instead of a text title
        title.setImageResource(R.drawable.home_title);
        title.setAdjustViewBounds(true);
        title.setScaleType(android.widget.ImageView.ScaleType.FIT_START);
        title.setMaxHeight(dp(96));
        TextView subtitle = new TextView(this);
        subtitle.setText(R.string.home_subtitle);
        subtitle.setTextColor(0xFFD2342F);
        subtitle.setTextSize(16);
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
        Button chooseCore = chunkyButton(getString(R.string.home_core), 0xFFA27BFF, 0xFF6A3FD6, 15);
        chooseCore.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            startActivityForResult(intent, PICK_CORE);
        });
        Button share = chunkyButton(getString(R.string.share_log), 0xFFA27BFF, 0xFF6A3FD6, 15);
        share.setOnClickListener(v -> {
            Intent send = new Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, logTail(120, 200));
            startActivity(Intent.createChooser(send, getString(R.string.share_log)));
        });

        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setPadding(dp(32), dp(16), dp(20), dp(12));
        left.addView(title, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        left.addView(subtitle);
        LinearLayout.LayoutParams pillLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        pillLp.topMargin = dp(10);
        left.addView(iso, pillLp);
        LinearLayout.LayoutParams playLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(72));
        playLp.topMargin = dp(10);
        left.addView(play, playLp);
        LinearLayout row1 = new LinearLayout(this);
        row1.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams half = new LinearLayout.LayoutParams(0, dp(58), 1f);
        half.rightMargin = dp(10);
        row1.addView(choose, half);
        row1.addView(settings, new LinearLayout.LayoutParams(0, dp(58), 1f));
        LinearLayout.LayoutParams row1Lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        row1Lp.topMargin = dp(8);
        left.addView(row1, row1Lp);
        LinearLayout row2 = new LinearLayout(this);
        row2.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams half2 = new LinearLayout.LayoutParams(0, dp(50), 1f);
        half2.rightMargin = dp(10);
        row2.addView(chooseCore, half2);
        row2.addView(share, new LinearLayout.LayoutParams(0, dp(50), 1f));
        LinearLayout.LayoutParams row2Lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        row2Lp.topMargin = dp(8);
        left.addView(row2, row2Lp);
        Button saves = chunkyButton(getString(R.string.saves_button), 0xFF5B9BFF, 0xFF2554C7, 15);
        saves.setOnClickListener(v -> showSaves());
        LinearLayout.LayoutParams savesLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(50));
        savesLp.topMargin = dp(8);
        left.addView(saves, savesLp);
        LinearLayout.LayoutParams coreLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        coreLp.topMargin = dp(10);
        left.addView(coreInfo, coreLp);
        left.addView(logView);
        TextView footer = new TextView(this);
        String version = "";
        try { version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName; } catch (Exception ignored) { }
        footer.setText(getString(R.string.home_footer, version));
        footer.setTextColor(0xFF6B6285);
        footer.setTextSize(11);
        LinearLayout.LayoutParams footerLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        footerLp.topMargin = dp(6);
        left.addView(info);
        left.addView(footer, footerLp);
        android.widget.ScrollView leftScroll = new android.widget.ScrollView(this);
        leftScroll.setFillViewport(true);
        leftScroll.addView(left);
        leftScroll.post(() -> leftScroll.scrollTo(0, 0));   // never open scrolled past the title
        panel.addView(leftScroll, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 5f));
        panel.addView(new ArtView(this), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 6f));
        root.addView(panel, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        touchView = new TouchControls(this);
        touchView.setVisibility(View.GONE);
        touchView.setListener(new TouchControls.Listener() {
            @Override public void onTouchState(int b, float a, float c, float d, float e) {
                tButtons = b; tlx = a; tly = c; trx = d; try_ = e;
                pushInput();
            }
            @Override public void onMenu() { if (coreStarted) showGameMenu(true); }
        });
        root.addView(touchView, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        trophyToast = new TrophyToast(this);
        root.addView(trophyToast, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        applyTouchSettings();
        setContentView(root);
        showIso(getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_ISO, null));
        showCore();
        if (coreRunning) {   // recreated while the game was already running: back to the game, not to the home screen
            coreStarted = true;
            panelView.setVisibility(View.GONE);
        }

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
            updateTouchControls();
            pollTrophyEvents();
            if (second != null) second.setStatus(vulkan + "\n" + getString(R.string.status_input, describeInput()));
            handler.postDelayed(this, 500);
        }
    };

    /** Trophy banners: the core reports each trophy it records (or blocks because of the trophy lock). */
    private void pollTrophyEvents() {
        if (trophyToast == null || !coreStarted) return;
        for (int i = 0; i < 6; i++) {
            String e = NativeBridge.trophyEvent();
            if (e == null) break;
            int bar = e.indexOf('|');
            if (bar < 0) continue;
            showTrophyBanner(e.substring(0, bar).equals("1"), e.substring(bar + 1));
        }
    }

    private void showTrophyBanner(boolean blocked, String id) {
        if (trophyToast == null) return;
        trophyToast.show(getString(blocked ? R.string.trophy_blocked_title : R.string.trophy_unlocked_title),
                getString(blocked ? R.string.trophy_blocked_sub : R.string.trophy_unlocked_sub, id), blocked);
    }

    private int trophyCount() {
        File[] files = new File(getExternalFilesDir(null), "gamedata/trophies").listFiles((d, n) -> n.endsWith(".json"));
        int count = 0;
        if (files != null) for (File f : files) {
            try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(f))) {
                String line;
                while ((line = r.readLine()) != null) if (line.contains("\"id\"")) count++;
            } catch (java.io.IOException ignored) { }
        }
        return count;
    }

    // Reset: with the game running the SDK keeps the trophies in memory and writes them back, so the game is closed through
    // the same quit gate (no save being written) right after the trophy files are deleted; the next start has none.
    private void confirmTrophyReset(final Button label) {
        new android.app.AlertDialog.Builder(this)
                .setTitle(R.string.trophy_reset_title)
                .setMessage(coreStarted ? R.string.trophy_reset_msg_running : R.string.trophy_reset_msg)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(R.string.trophy_reset_do, (d, w) -> doTrophyReset(label))
                .show();
    }

    private void doTrophyReset(final Button label) {
        final boolean running = coreStarted;
        final int[] tries = {0};
        handler.post(new Runnable() {
            @Override public void run() {
                int state = running ? NativeBridge.requestStop() : 1;
                if (state == 0 && ++tries[0] <= 120) { handler.postDelayed(this, 250); return; }   // a save is being written
                File[] files = new File(getExternalFilesDir(null), "gamedata/trophies").listFiles((dir, n) -> n.endsWith(".json"));
                if (files != null) for (File f : files) //noinspection ResultOfMethodCallIgnored
                    f.delete();
                if (running) {
                    android.widget.Toast.makeText(MainActivity.this, R.string.trophy_reset_done_running, android.widget.Toast.LENGTH_LONG).show();
                    handler.postDelayed(() -> {
                        finishAndRemoveTask();
                        android.os.Process.killProcess(android.os.Process.myPid());
                    }, 1500);
                } else {
                    android.widget.Toast.makeText(MainActivity.this, R.string.trophy_reset_done, android.widget.Toast.LENGTH_LONG).show();
                    label.setText(getString(R.string.trophy_reset_button, trophyCount()));
                }
            }
        });
    }

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

    private static final String KEY_SCALE = "scale_pct", KEY_FPS = "show_fps", KEY_EARLY = "early_flip", KEY_TROPHY_LOCK = "trophy_lock";
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

    private boolean trophyLock() { return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_TROPHY_LOCK, false); }

    private String trophyLockLabel() { return getString(trophyLock() ? R.string.trophy_lock_on : R.string.trophy_lock_off); }

    private int scalePct() { return getSharedPreferences(PREFS, MODE_PRIVATE).getInt(KEY_SCALE, 100); }

    private boolean showFps() { return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_FPS, false); }

    private boolean earlyFlip() { return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_EARLY, true); }

    private String earlyLabel() { return getString(earlyFlip() ? R.string.early_on : R.string.early_off); }

    private String resolutionLabel() { return getString(R.string.internal_resolution, scalePct()); }

    private String fpsLabel() { return getString(showFps() ? R.string.fps_on : R.string.fps_off); }

    // The core reads this file from its working directory (next to the log) when the first frame is presented.
    private void writeSettings() {
        try (OutputStream out = new FileOutputStream(new File(getExternalFilesDir(null), "d2_android.cfg"))) {
            StringBuilder text = new StringBuilder("scale=100\nbufscale=" + scalePct() + "\nearlyflip=" + (earlyFlip() ? 1 : 0) + "\nshowfps=" + (showFps() ? 1 : 0) + "\ntrophylock=" + (trophyLock() ? 1 : 0) + "\n");
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
            int profileRows = 0;
            for (String line : all) {
                if (line.startsWith("[rsx] presented guest frame")) { lastFrame = line; continue; }
                // The profiler prints ~100 lines per thread every 30 s; they crowd out everything else. Keep each
                // thread's header and its hottest few rows.
                if (line.startsWith("[profile]")) {
                    if (line.contains(" thread ")) profileRows = 0;
                    if (++profileRows > 7) continue;
                }
                kept.add(line);
            }
            StringBuilder out = new StringBuilder();
            if (from > 0) out.append("(log truncated: only the last 6 MB)\n");
            int head = Math.min(headLines, kept.size());
            for (int i = 0; i < head; i++) out.append(kept.get(i)).append('\n');
            int tailStart = Math.max(head, kept.size() - lines);
            if (tailStart > head) {
                // The omitted middle is where the interesting events usually are (surface lost/recreated, texture cache,
                // errors, profiler results): keep those lines, drop only the routine ones.
                int shown = 0, skipped = 0;
                for (int i = head; i < tailStart; i++) {
                    String line = kept.get(i);
                    boolean key = line.startsWith("[vulkan]") || line.startsWith("[profile]") || line.contains("rror")
                            || line.contains("fail") || line.contains("exhausted") || line.contains("unsupported")
                            || line.contains("texture") || line.contains("surface") || line.startsWith("[cellAudio] AAudio paused")
                            || line.startsWith("[cellAudio] AAudio resumed") || line.startsWith("[d2] trophy")
                            || line.contains("TROPHY UNLOCKED") || line.startsWith("[sceNpTrophy]") || line.startsWith("[D2 cheats]");
                    if (line.startsWith("[vulkan][slow]")) {   // only the real stalls: hundreds of 35 ms lines would fill the cap
                        int p = line.indexOf("period=");
                        key = p >= 0 && parseMs(line, p + 7) >= 150;
                    }
                    if (key && shown < 260 && !line.startsWith("[vulkan][pace]")) { out.append(line).append('\n'); shown++; }
                    else skipped++;
                }
                out.append("... ").append(skipped).append(" routine lines omitted ...\n");
            }
            for (int i = tailStart; i < kept.size(); i++) out.append(kept.get(i)).append('\n');
            if (lastFrame != null) out.append(lastFrame).append('\n');
            return out.toString();
        } catch (java.io.IOException e) {
            return "(log: " + e + ")";
        }
    }

    private static double parseMs(String line, int from) {
        int end = from;
        while (end < line.length() && (Character.isDigit(line.charAt(end)) || line.charAt(end) == '.')) end++;
        try { return Double.parseDouble(line.substring(from, end)); } catch (NumberFormatException e) { return 0; }
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
        try { SaveSlots.autoBackup(getExternalFilesDir(null)); } catch (java.io.IOException | RuntimeException ignored) { }
        String error = NativeBridge.startCore(coreFile().getAbsolutePath(), eboot.getAbsolutePath(),
                root.getAbsolutePath(), logFile().getAbsolutePath());
        if (error != null) { logView.setText(error); return; }
        coreStarted = true;
        coreRunning = true;
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
        if (request == PICK_SAVE_TREE && result == RESULT_OK && data != null && data.getData() != null) {
            importSave(data.getData(), true);
            return;
        }
        if (request == PICK_SAVE_ZIP && result == RESULT_OK && data != null && data.getData() != null) {
            importSave(data.getData(), false);
            return;
        }
        if (request == EXPORT_SAVE && result == RESULT_OK && data != null && data.getData() != null && exportSlot != null) {
            exportSave(data.getData());
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
        tButtons = 0; tlx = tly = trx = try_ = 0;
        if (touchView != null) touchView.releaseAll();
        NativeBridge.setInput(0, 0, 0, 0, 0);
    }

    // Physical controller and touch controls are merged: buttons OR together, a stick follows whichever is moved.
    private void pushInput() {
        float ax = lx != 0 || ly != 0 ? lx : tlx, ay = lx != 0 || ly != 0 ? ly : tly;
        float bx = rx != 0 || ry != 0 ? rx : trx, by = rx != 0 || ry != 0 ? ry : try_;
        NativeBridge.setInput(buttons | hat | tButtons, ax, ay, bx, by);
    }

    private static final String KEY_TC_MODE = "tc_mode", KEY_TC_SIZE = "tc_size", KEY_TC_ALPHA = "tc_alpha",
            KEY_TC_HAPTIC = "tc_haptic", KEY_TC_LSTICK = "tc_lstick", KEY_TC_RSTICK = "tc_rstick";
    private static final int[] TC_SIZES = {80, 100, 120, 140};
    private static final int[] TC_ALPHAS = {30, 45, 60, 80};

    private int tcMode() { return getSharedPreferences(PREFS, MODE_PRIVATE).getInt(KEY_TC_MODE, 0); }   // 0 auto, 1 always, 2 never
    private int tcSize() { return getSharedPreferences(PREFS, MODE_PRIVATE).getInt(KEY_TC_SIZE, 100); }
    private int tcAlpha() { return getSharedPreferences(PREFS, MODE_PRIVATE).getInt(KEY_TC_ALPHA, 45); }
    private boolean tcHaptic() { return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_TC_HAPTIC, true); }
    private boolean tcLeftStick() { return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_TC_LSTICK, false); }
    private boolean tcRightStick() { return getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_TC_RSTICK, false); }

    private void applyTouchSettings() {
        if (touchView != null) touchView.configure(tcSize() / 100f, tcAlpha(), tcHaptic(), tcLeftStick(), tcRightStick());
    }

    /** A real gamepad (handheld controls count) is connected: the on-screen controls are not needed in Auto mode. */
    private boolean hasGamepad() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null || d.isVirtual()) continue;
            int src = d.getSources();
            if ((src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) return true;
        }
        return false;
    }

    private void updateTouchControls() {
        if (touchView == null) return;
        boolean want = coreStarted && panelView.getVisibility() != View.VISIBLE
                && !(gameMenu != null && gameMenu.isShowing())
                && (tcMode() == 1 || (tcMode() == 0 && !hasGamepad()));
        int target = want ? View.VISIBLE : View.GONE;
        if (touchView.getVisibility() != target) {
            if (!want) touchView.releaseAll();
            touchView.setVisibility(target);
        }
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
        private android.graphics.Bitmap picture, logo;
        ArtView(android.content.Context c) {
            super(c);
            setFocusable(false);
            try { picture = android.graphics.BitmapFactory.decodeResource(c.getResources(), R.drawable.home_art); } catch (Throwable ignored) { }
            try { logo = android.graphics.BitmapFactory.decodeResource(c.getResources(), R.drawable.home_logo); } catch (Throwable ignored) { }
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
                canvas.drawBitmap(picture, src, new android.graphics.Rect(0, 0, w, h), paint);   // a blurred gameplay frame
                paint.setColor(0x33140A28);
                canvas.drawRect(0, 0, w, h, paint);   // slight veil so the logo stands out
                if (logo != null) {   // the logo, centred on the visible part of the panel (the left edge is curved)
                    float lw = w * 0.74f, lh = lw * logo.getHeight() / logo.getWidth();
                    if (lh > h * 0.86f) { lh = h * 0.86f; lw = lh * logo.getWidth() / logo.getHeight(); }
                    float cx = w * 0.58f, cy = h * 0.5f;
                    paint.setFilterBitmap(true);   // smooth, not blocky, when the logo is scaled
                    paint.setColor(Color.WHITE);
                    canvas.drawBitmap(logo, null, new android.graphics.RectF(cx - lw / 2, cy - lh / 2, cx + lw / 2, cy + lh / 2), paint);
                }
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

    // ---- Save slots (same idea as the Windows launcher's save selector) ----
    private SaveSlots.Slot exportSlot;
    private android.app.Dialog savesDialog;

    /** Runs a save-folder change. With the game running it holds the game's own save gate for the moment; -1 = old core. */
    private interface SaveJob { void run() throws java.io.IOException; }

    private boolean withSaveGate(SaveJob job) throws java.io.IOException {
        if (!coreRunning) { job.run(); return true; }
        int state = NativeBridge.saveLock();
        for (int i = 0; state == 0 && i < 40; i++) {   // a save is being written: wait up to ~4 s
            try { Thread.sleep(100); } catch (InterruptedException ignored) { }
            state = NativeBridge.saveLock();
        }
        if (state < 0) throw new java.io.IOException(getString(R.string.saves_need_new_core));
        if (state == 0) throw new java.io.IOException(getString(R.string.saves_busy));
        try { job.run(); } finally { NativeBridge.saveUnlock(); }
        return true;
    }

    private void toast(String text) { android.widget.Toast.makeText(this, text, android.widget.Toast.LENGTH_LONG).show(); }

    private android.app.Dialog glassDialog(LinearLayout root) {
        final android.app.Dialog dialog = new android.app.Dialog(this, android.R.style.Theme_Material_Dialog_NoActionBar);
        dialog.setContentView(root);
        android.view.Window window = dialog.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
            window.setDimAmount(0.45f);
            if (android.os.Build.VERSION.SDK_INT >= 31) {
                window.addFlags(android.view.WindowManager.LayoutParams.FLAG_BLUR_BEHIND);
                window.getAttributes().setBlurBehindRadius(24);
            }
        }
        return dialog;
    }

    private void sizeDialog(android.app.Dialog dialog, int heightPercent) {
        android.view.Window window = dialog.getWindow();
        if (window == null) return;
        android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
        window.setLayout(Math.min(dp(520), dm.widthPixels * 9 / 10), dm.heightPixels * heightPercent / 100);
    }

    private void showSaves() {
        if (savesDialog != null && savesDialog.isShowing()) savesDialog.dismiss();
        final int accent = 0xFF7A1B3D;
        final File files = getExternalFilesDir(null);
        final LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(16), dp(20), dp(16));
        root.setBackground(glass(0xCC160B28, 0x55FFFFFF, 24));
        TextView title = new TextView(this);
        title.setText(R.string.saves_title);
        title.setTextColor(Color.WHITE);
        title.setTextSize(22);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        root.addView(title);
        final android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        final LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(content);
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        scrollLp.topMargin = dp(8);
        root.addView(scroll, scrollLp);
        final Button back = glassButton(getString(R.string.saves_back), true, accent);
        addRow(root, back);
        final android.app.Dialog dialog = glassDialog(root);
        savesDialog = dialog;
        final Runnable[] fill = new Runnable[1];
        fill[0] = () -> {
            content.removeAllViews();
            TextView help = new TextView(this);
            help.setText(R.string.saves_help);
            help.setTextColor(0xFFCFC6E6);
            help.setTextSize(13);
            content.addView(help);
            Button saveNow = glassButton(getString(R.string.saves_save_current), false, accent);
            Button importFolder = glassButton(getString(R.string.saves_import_folder), false, accent);
            Button importZip = glassButton(getString(R.string.saves_import_zip), false, accent);
            saveNow.setOnClickListener(v -> askSlotName(files, fill[0]));
            importFolder.setOnClickListener(v -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_SAVE_TREE));
            importZip.setOnClickListener(v -> {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
                startActivityForResult(intent, PICK_SAVE_ZIP);
            });
            addRow(content, saveNow); addRow(content, importFolder); addRow(content, importZip);
            java.util.List<SaveSlots.Slot> slots = SaveSlots.list(files);
            if (slots.isEmpty()) {
                TextView empty = new TextView(this);
                empty.setText(R.string.saves_empty);
                empty.setTextColor(0xFF9C93B8);
                empty.setPadding(0, dp(14), 0, 0);
                content.addView(empty);
            }
            for (SaveSlots.Slot slot : slots) {
                Button b = glassButton(getString(R.string.saves_slot_fmt, slot.title(), slot.saves), false, accent);
                b.setTextSize(14);
                b.setOnClickListener(v -> showSlotActions(files, slot, fill[0]));
                addRow(content, b);
            }
        };
        savesRefresh = fill[0];
        fill[0].run();
        back.setOnClickListener(v -> dialog.dismiss());
        dialog.setOnDismissListener(d -> { if (savesDialog == dialog) { savesDialog = null; savesRefresh = null; } });
        dialog.show();
        sizeDialog(dialog, 85);
        back.requestFocus();
    }

    private Runnable savesRefresh;

    private void askSlotName(File files, Runnable refresh) {
        final android.widget.EditText input = new android.widget.EditText(this);
        input.setSingleLine(true);
        input.setTextColor(Color.WHITE);
        new android.app.AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle(R.string.saves_name_title).setView(input)
                .setPositiveButton(android.R.string.ok, (d, w) -> {
                    try {
                        final java.io.File[] made = {null};
                        withSaveGate(() -> made[0] = SaveSlots.create(files, input.getText().toString()));
                        toast(getString(made[0] == null ? R.string.saves_nothing : R.string.saves_saved));
                    } catch (java.io.IOException | RuntimeException e) {
                        toast(getString(R.string.saves_error, e.getMessage()));
                    }
                    refresh.run();
                })
                .setNegativeButton(android.R.string.cancel, null).show();
    }

    private void showSlotActions(File files, SaveSlots.Slot slot, Runnable refresh) {
        final int accent = 0xFF7A1B3D;
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(16), dp(20), dp(16));
        root.setBackground(glass(0xCC160B28, 0x55FFFFFF, 24));
        TextView title = new TextView(this);
        title.setText(getString(R.string.saves_slot_fmt, slot.title(), slot.saves));
        title.setTextColor(Color.WHITE);
        title.setTextSize(18);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        root.addView(title);
        final android.app.Dialog dialog = glassDialog(root);
        Button load = glassButton(getString(R.string.saves_load), true, accent);
        Button export = glassButton(getString(R.string.saves_export), false, accent);
        final Button delete = glassButton(getString(R.string.saves_delete), false, 0xFFB3261E);
        Button back = glassButton(getString(R.string.saves_back), false, accent);
        addRow(root, load); addRow(root, export); addRow(root, delete); addRow(root, back);
        load.setOnClickListener(v -> {
            try { withSaveGate(() -> SaveSlots.load(files, slot)); toast(getString(coreRunning ? R.string.saves_loaded_ingame : R.string.saves_loaded)); }
            catch (java.io.IOException | RuntimeException e) { toast(getString(R.string.saves_error, e.getMessage())); }
            dialog.dismiss();
            refresh.run();
        });
        export.setOnClickListener(v -> {
            exportSlot = slot;
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                    .setType("application/zip").putExtra(Intent.EXTRA_TITLE, "disgaea-d2-save-" + slot.name + ".zip");
            startActivityForResult(intent, EXPORT_SAVE);
            dialog.dismiss();
        });
        final boolean[] armed = {false};
        delete.setOnClickListener(v -> {
            if (!armed[0]) { armed[0] = true; delete.setText(R.string.saves_delete_confirm); return; }
            try { SaveSlots.delete(files, slot); } catch (java.io.IOException | RuntimeException e) { toast(getString(R.string.saves_error, e.getMessage())); }
            dialog.dismiss();
            refresh.run();
        });
        back.setOnClickListener(v -> dialog.dismiss());
        dialog.show();
        sizeDialog(dialog, 60);
        load.requestFocus();
    }

    private void importSave(Uri uri, boolean tree) {
        final File files = getExternalFilesDir(null);
        String name = null;
        if (!tree) {
            try (Cursor c = getContentResolver().query(uri, null, null, null, null)) {
                if (c != null && c.moveToFirst()) { int i = c.getColumnIndex(OpenableColumns.DISPLAY_NAME); if (i >= 0) name = c.getString(i); }
            } catch (RuntimeException ignored) { }
        } else {
            try (Cursor c = getContentResolver().query(android.provider.DocumentsContract.buildDocumentUriUsingTree(uri,
                    android.provider.DocumentsContract.getTreeDocumentId(uri)), new String[]{android.provider.DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
                if (c != null && c.moveToFirst()) name = c.getString(0);
            } catch (RuntimeException ignored) { }
        }
        final String shownName = name;
        new Thread(() -> {
            String message;
            try {
                int n = tree ? SaveSlots.importTree(getContentResolver(), uri, shownName, files)
                             : SaveSlots.importZip(getContentResolver(), uri, shownName, files);
                message = n == 0 ? getString(R.string.saves_import_none) : getString(R.string.saves_imported, n);
            } catch (java.io.IOException | RuntimeException e) {
                message = getString(R.string.saves_error, e.getMessage());
            }
            final String shown = message;
            handler.post(() -> { toast(shown); if (savesRefresh != null) savesRefresh.run(); });
        }).start();
    }

    private void exportSave(Uri uri) {
        final SaveSlots.Slot slot = exportSlot;
        exportSlot = null;
        new Thread(() -> {
            String message;
            try (OutputStream out = getContentResolver().openOutputStream(uri)) {
                SaveSlots.exportZip(slot, out);
                message = getString(R.string.saves_exported);
            } catch (java.io.IOException | RuntimeException e) {
                message = getString(R.string.saves_error, e.getMessage());
            }
            final String shown = message;
            handler.post(() -> toast(shown));
        }).start();
    }

    // ---- Cheats tab: the engine runs inside the core; this screen only shows its snapshot and queues changes ----
    private Runnable cheatsTick = () -> { };

    private static java.util.Map<String, String> parseState(String text) {
        java.util.Map<String, String> map = new java.util.HashMap<>();
        if (text == null) return map;
        for (String line : text.split("\n")) {
            int eq = line.indexOf('=');
            if (eq > 0) map.put(line.substring(0, eq), line.substring(eq + 1));
        }
        return map;
    }

    private void buildCheatsTab(LinearLayout content, int accent, java.util.function.BooleanSupplier visible) {
        final TextView status = new TextView(this);
        status.setTextColor(0xFFCFC6E6);
        status.setTextSize(13);
        content.addView(status);
        TextView warn = new TextView(this);
        warn.setText(R.string.cheats_warning);
        warn.setTextColor(0xFFFFC857);
        warn.setTextSize(12);
        warn.setPadding(0, dp(6), 0, 0);
        content.addView(warn);
        final Button trophy = glassButton(trophyLockLabel(), false, accent);
        trophy.setOnClickListener(v -> {
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putBoolean(KEY_TROPHY_LOCK, !trophyLock()).apply();
            trophy.setText(trophyLockLabel());
            writeSettings();   // the core notices the new file within a couple of seconds
        });
        addRow(content, trophy);
        final Button trophyTest = glassButton(getString(R.string.trophy_test_button), false, accent);
        trophyTest.setOnClickListener(v -> {
            if (menuDialog != null) menuDialog.dismiss();   // the menu would cover the banner
            handler.postDelayed(() -> showTrophyBanner(false, "1"), 300);
            handler.postDelayed(() -> showTrophyBanner(true, "2"), 900);
        });
        addRow(content, trophyTest);
        final Button trophyReset = glassButton(getString(R.string.trophy_reset_button, trophyCount()), false, accent);
        trophyReset.setOnClickListener(v -> confirmTrophyReset(trophyReset));
        addRow(content, trophyReset);
        TextView trophyNote = new TextView(this);
        trophyNote.setText(R.string.trophy_lock_note);
        trophyNote.setTextColor(0xFFCFC6E6);
        trophyNote.setTextSize(12);
        trophyNote.setPadding(0, 0, 0, dp(6));
        content.addView(trophyNote);

        final String[][] toggles = {
            {"hp_lock", getString(R.string.cheat_hp)}, {"sp_lock", getString(R.string.cheat_sp)},
            {"one_hit", getString(R.string.cheat_onehit)}, {"free_shop", getString(R.string.cheat_shop)}};
        final Button[] toggleButtons = new Button[toggles.length];
        for (int i = 0; i < toggles.length; i++) {
            final String name = toggles[i][0];
            toggleButtons[i] = glassButton(toggles[i][1], false, accent);
            toggleButtons[i].setOnClickListener(v -> {
                java.util.Map<String, String> now = parseState(NativeBridge.cheatsState());
                boolean on = "1".equals(now.get(name));
                cheatsResult(NativeBridge.cheatsSetToggle(name, !on));
            });
            addRow(content, toggleButtons[i]);
        }
        final Button exp = glassButton(getString(R.string.cheat_exp, 1), false, accent);
        exp.setOnClickListener(v -> {
            java.util.Map<String, String> now = parseState(NativeBridge.cheatsState());
            int cur = 1;
            try { cur = Integer.parseInt(now.get("exp")); } catch (RuntimeException ignored) { }
            cheatsResult(NativeBridge.cheatsSetExp(cur >= 1024 ? 1 : cur * 2));
        });
        addRow(content, exp);
        Button characters = glassButton(getString(R.string.cheat_characters), true, accent);
        characters.setOnClickListener(v -> showCharacters());
        addRow(content, characters);

        // General values (HL, Bonus Gauge, Cheat Shop ...) straight from the engine's field list.
        final java.util.List<String[]> fields = new java.util.ArrayList<>();
        final java.util.List<Button> fieldButtons = new java.util.ArrayList<>();
        for (String line : NativeBridge.cheatsFields(0).split("\n")) {
            String[] part = line.split("\\|");
            if (part.length < 4) continue;
            fields.add(part);
            final String[] f = part;
            Button b = glassButton(f[1], false, accent);
            b.setTextSize(14);
            b.setOnClickListener(v -> askCheatValue(f));
            fieldButtons.add(b);
            addRow(content, b);
        }
        Button off = glassButton(getString(R.string.cheat_off), false, 0xFFB3261E);
        off.setOnClickListener(v -> cheatsResult(NativeBridge.cheatsDisableAll()));
        addRow(content, off);

        final java.text.NumberFormat number = java.text.NumberFormat.getInstance();
        cheatsTick = new Runnable() {
            @Override public void run() {
                if (!visible.getAsBoolean()) return;
                java.util.Map<String, String> st = parseState(NativeBridge.cheatsState());
                boolean ok = "1".equals(st.get("ok")), ready = "1".equals(st.get("ready"));
                String line = ok ? getString(R.string.cheats_status, st.get("version") == null ? "?" : st.get("version"),
                        getString("1".equals(st.get("verified")) ? R.string.cheats_verified : R.string.cheats_unverified),
                        getString(ready ? R.string.cheats_ready : R.string.cheats_wait))
                        : getString(R.string.cheats_nocore);
                String detail = st.get("status");
                status.setText(detail == null || detail.isEmpty() ? line : line + "\n" + detail);
                for (int i = 0; i < toggles.length; i++) {
                    boolean on = "1".equals(st.get(toggles[i][0]));
                    toggleButtons[i].setText(toggles[i][1] + ":  " + getString(on ? R.string.cheat_on : R.string.cheat_off_label));
                    toggleButtons[i].setEnabled(ok);
                }
                String e = st.get("exp");
                exp.setText(getString(R.string.cheat_exp, e == null ? 1 : parseInt(e)));
                exp.setEnabled(ok);
                for (int i = 0; i < fields.size(); i++) {
                    String raw = st.get("g." + fields.get(i)[0]);
                    String shown = raw == null ? "-" : raw;
                    try { if (raw != null) shown = number.format(Long.parseLong(raw)); } catch (NumberFormatException ignored) { }
                    fieldButtons.get(i).setText(fields.get(i)[1] + ":  " + shown);
                    fieldButtons.get(i).setEnabled(ok && ready);
                }
                handler.postDelayed(this, 600);
            }
        };
        handler.post(cheatsTick);
    }

    private static int parseInt(String s) { try { return Integer.parseInt(s); } catch (RuntimeException e) { return 1; } }

    private void cheatsResult(int rc) {
        if (rc == -2) toast(getString(R.string.cheats_nocore));
        else if (rc < 0) toast(getString(R.string.cheats_refused));
    }

    private void askCheatValue(String[] field) {
        final android.widget.EditText input = new android.widget.EditText(this);
        input.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        input.setSingleLine(true);
        input.setTextColor(Color.WHITE);
        input.setHint(field[2] + " - " + field[3]);
        new android.app.AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle(field[1]).setView(input)
                .setPositiveButton(android.R.string.ok, (d, w) -> {
                    try { cheatsResult(NativeBridge.cheatsSetGeneral(field[0], Long.parseLong(input.getText().toString().trim()))); }
                    catch (NumberFormatException e) { toast(getString(R.string.cheats_refused)); }
                })
                .setNegativeButton(android.R.string.cancel, null).show();
    }

    // ---- Character editor (party list -> one unit's values; edits go through the engine's generation check) ----
    private android.app.Dialog charsDialog;
    private Runnable charsTick = () -> { };

    private android.app.Dialog darkDialog(LinearLayout root, int heightPercent) {
        final android.app.Dialog dialog = glassDialog(root);
        dialog.show();
        sizeDialog(dialog, heightPercent);
        return dialog;
    }

    private LinearLayout dialogColumn(String title) {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(16), dp(20), dp(16));
        root.setBackground(glass(0xCC160B28, 0x55FFFFFF, 24));
        TextView t = new TextView(this);
        t.setText(title);
        t.setTextColor(Color.WHITE);
        t.setTextSize(20);
        t.setTypeface(null, android.graphics.Typeface.BOLD);
        root.addView(t);
        return root;
    }

    private void showCharacters() {
        final int accent = 0xFF7A1B3D;
        java.util.Map<String, String> st = parseState(NativeBridge.cheatsState());
        if (!"1".equals(st.get("ok")) || !"1".equals(st.get("ready"))) { toast(getString(R.string.cheats_wait)); return; }
        LinearLayout root = dialogColumn(getString(R.string.cheat_characters));
        android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        LinearLayout list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        lp.topMargin = dp(8);
        root.addView(scroll, lp);
        Button back = glassButton(getString(R.string.saves_back), true, accent);
        addRow(root, back);
        int count = parseInt(st.get("party"));
        for (int i = 0; i < count; i++) {
            String[] p = (st.get("p." + i) == null ? "?|0|0" : st.get("p." + i)).split("\\|");
            final int unit = i;
            Button b = glassButton(p[0] + "   Lv " + (p.length > 1 ? p[1] : "?"), false, accent);
            b.setOnClickListener(v -> showCharacter(unit, p[0]));
            addRow(list, b);
        }
        final android.app.Dialog dialog = darkDialog(root, 85);
        charsDialog = dialog;
        back.setOnClickListener(v -> dialog.dismiss());
        back.requestFocus();
    }

    private void showCharacter(int unit, String name) {
        final int accent = 0xFF7A1B3D;
        LinearLayout root = dialogColumn(name);
        final TextView note = new TextView(this);
        note.setTextColor(0xFFCFC6E6);
        note.setTextSize(12);
        root.addView(note);
        android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        LinearLayout list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        lp.topMargin = dp(8);
        root.addView(scroll, lp);
        Button back = glassButton(getString(R.string.saves_back), true, accent);
        addRow(root, back);

        final java.util.List<String[]> fields = new java.util.ArrayList<>();
        final java.util.List<Button> buttons = new java.util.ArrayList<>();
        for (String line : NativeBridge.cheatsFields(1).split("\n")) {
            String[] part = line.split("\\|");
            if (part.length < 4) continue;
            fields.add(part);
            final String[] f = part;
            Button b = glassButton(f[1], false, accent);
            b.setTextSize(14);
            b.setOnClickListener(v -> {
                java.util.Map<String, String> now = parseState(NativeBridge.cheatsState());
                long gen = 0;
                try { gen = Long.parseUnsignedLong(now.get("gen")); } catch (RuntimeException ignored) { }
                askCharValue(unit, f, gen);
            });
            buttons.add(b);
            addRow(list, b);
        }
        NativeBridge.cheatsSelectUnit(unit);
        final android.app.Dialog dialog = darkDialog(root, 90);
        back.setOnClickListener(v -> dialog.dismiss());
        final java.text.NumberFormat number = java.text.NumberFormat.getInstance();
        handler.removeCallbacks(charsTick);
        charsTick = new Runnable() {
            @Override public void run() {
                if (!dialog.isShowing()) return;
                java.util.Map<String, String> st = parseState(NativeBridge.cheatsState());
                boolean ready = "1".equals(st.get("ready")) && String.valueOf(unit).equals(st.get("unit"));
                note.setText(ready ? getString(R.string.cheat_char_hint) : getString(R.string.cheats_wait));
                for (int i = 0; i < fields.size(); i++) {
                    String raw = st.get("c." + fields.get(i)[0]);
                    String shown = raw == null ? "-" : raw;
                    try { if (raw != null) shown = number.format(Long.parseUnsignedLong(raw)); } catch (NumberFormatException ignored) { }
                    buttons.get(i).setText(fields.get(i)[1] + ":  " + shown);
                    buttons.get(i).setEnabled(ready);
                }
                handler.postDelayed(this, 700);
            }
        };
        handler.post(charsTick);
        back.requestFocus();
    }

    private void askCharValue(int unit, String[] field, long generation) {
        final android.widget.EditText input = new android.widget.EditText(this);
        input.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        input.setSingleLine(true);
        input.setTextColor(Color.WHITE);
        input.setHint(field[2] + " - " + field[3]);
        new android.app.AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle(field[1]).setView(input)
                .setPositiveButton(android.R.string.ok, (d, w) -> {
                    try { cheatsResult(NativeBridge.cheatsSetCharacter(unit, field[0], Long.parseLong(input.getText().toString().trim()), generation)); }
                    catch (NumberFormatException e) { toast(getString(R.string.cheats_refused)); }
                })
                .setNegativeButton(android.R.string.cancel, null).show();
    }

    private String tcModeLabel() {
        return getString(R.string.tc_mode, getString(tcMode() == 0 ? R.string.tc_auto : tcMode() == 1 ? R.string.tc_always : R.string.tc_never));
    }

    private void buildControlsTab(LinearLayout content, int accent) {
        TextView note = new TextView(this);
        note.setText(R.string.tc_note);
        note.setTextColor(0xFFCFC6E6);
        note.setTextSize(13);
        content.addView(note);
        final Button mode = glassButton(tcModeLabel(), false, accent);
        final Button size = glassButton(getString(R.string.tc_size, tcSize()), false, accent);
        final Button alpha = glassButton(getString(R.string.tc_alpha, tcAlpha()), false, accent);
        final Button haptic = glassButton(getString(tcHaptic() ? R.string.tc_haptic_on : R.string.tc_haptic_off), false, accent);
        final Button left = glassButton(getString(tcLeftStick() ? R.string.tc_left_stick : R.string.tc_left_dpad), false, accent);
        final Button right = glassButton(getString(tcRightStick() ? R.string.tc_right_on : R.string.tc_right_off), false, accent);
        mode.setOnClickListener(v -> {
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putInt(KEY_TC_MODE, (tcMode() + 1) % 3).apply();
            mode.setText(tcModeLabel());
        });
        size.setOnClickListener(v -> {
            int next = 0;
            for (int i = 0; i < TC_SIZES.length; i++) if (TC_SIZES[i] == tcSize()) next = (i + 1) % TC_SIZES.length;
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putInt(KEY_TC_SIZE, TC_SIZES[next]).apply();
            size.setText(getString(R.string.tc_size, tcSize()));
            applyTouchSettings();
        });
        alpha.setOnClickListener(v -> {
            int next = 0;
            for (int i = 0; i < TC_ALPHAS.length; i++) if (TC_ALPHAS[i] == tcAlpha()) next = (i + 1) % TC_ALPHAS.length;
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putInt(KEY_TC_ALPHA, TC_ALPHAS[next]).apply();
            alpha.setText(getString(R.string.tc_alpha, tcAlpha()));
            applyTouchSettings();
        });
        haptic.setOnClickListener(v -> {
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putBoolean(KEY_TC_HAPTIC, !tcHaptic()).apply();
            haptic.setText(getString(tcHaptic() ? R.string.tc_haptic_on : R.string.tc_haptic_off));
            applyTouchSettings();
        });
        left.setOnClickListener(v -> {
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putBoolean(KEY_TC_LSTICK, !tcLeftStick()).apply();
            left.setText(getString(tcLeftStick() ? R.string.tc_left_stick : R.string.tc_left_dpad));
            applyTouchSettings();
        });
        right.setOnClickListener(v -> {
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putBoolean(KEY_TC_RSTICK, !tcRightStick()).apply();
            right.setText(getString(tcRightStick() ? R.string.tc_right_on : R.string.tc_right_off));
            applyTouchSettings();
        });
        addRow(content, mode); addRow(content, size); addRow(content, alpha);
        addRow(content, haptic); addRow(content, left); addRow(content, right);
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
        menuDialog = dialog;
        gameMenu = dialog;
        final int[] tabNames = {R.string.tab_video, R.string.tab_filters, R.string.tab_controls, R.string.tab_saves, R.string.tab_cheats, R.string.tab_system};
        final int tabCount = inGame ? 6 : 3;
        final Button[] tabButtons = new Button[tabCount];
        final int[] current = {0};
        final Runnable[] fill = new Runnable[1];

        fill[0] = () -> {
            content.removeAllViews();
            handler.removeCallbacks(cheatsTick);
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
            } else if (current[0] == 2) {
                buildControlsTab(content, accent);
            } else if (current[0] == 3) {
                TextView note = new TextView(this);
                note.setText(R.string.saves_ingame_note);
                note.setTextColor(0xFFCFC6E6);
                note.setTextSize(13);
                content.addView(note);
                Button open = glassButton(getString(R.string.saves_button), true, accent);
                open.setOnClickListener(v -> showSaves());
                addRow(content, open);
            } else if (current[0] == 4) {
                buildCheatsTab(content, accent, () -> dialog.isShowing() && current[0] == 4);
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
            t.setSingleLine(true);
            t.setTextSize(14);
            t.setPadding(dp(4), dp(10), dp(4), dp(10));
            t.setOnClickListener(v -> { current[0] = index; fill[0].run(); });
            tabButtons[i] = t;
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(48), 1f);   // same height for every tab
            lp.topMargin = dp(10);
            lp.rightMargin = i < tabCount - 1 ? dp(8) : 0;
            tabs.addView(t, lp);
        }
        fill[0].run();
        resume.setOnClickListener(v -> dialog.dismiss());
        dialog.setContentView(root);
        dialog.setOnDismissListener(d -> { handler.removeCallbacks(cheatsTick); if (gameMenu == dialog) gameMenu = null; });
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
        pushInput();
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
            pushInput();
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
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (coreRunning) { coreStarted = true; panelView.setVisibility(View.GONE); }   // launcher icon while the game runs
    }

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
