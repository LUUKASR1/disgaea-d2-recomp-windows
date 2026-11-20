package org.disgaead2.port;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

/**
 * On-screen gamepad for phones and tablets without a controller. It draws a D-pad (or left stick), the four face buttons,
 * L1/L2/R1/R2, Start/Select, an optional right stick and a menu button, and turns multi-touch into the same bit mask the
 * physical controller path uses (A=1 B=2 X=4 Y=8 L1=16 R1=32 L2=64 R2=128 Start=256 Select=512, D-pad 1024..8192).
 */
public class TouchControls extends View {
    public interface Listener {
        void onTouchState(int buttons, float lx, float ly, float rx, float ry);
        void onMenu();
    }

    static final int UP = 1024, DOWN = 2048, LEFT = 4096, RIGHT = 8192;

    private Listener listener;
    private float scale = 1f;
    private int alpha = 160;
    private boolean haptic = true, leftStick = false, rightStick = false;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();

    // Layout (pixels), recomputed in onSizeChanged / when settings change.
    private float R;
    private float faceX, faceY, dpadX, dpadY, dpadR, rsX, rsY, rsR, menuX, menuY, menuR;
    private final RectF l1 = new RectF(), l2 = new RectF(), r1 = new RectF(), r2 = new RectF(), start = new RectF(), select = new RectF();

    // Current state.
    private int pressed;
    private float lx, ly, rx, ry;
    private float knobLx, knobLy, knobRx, knobRy;
    private int dpadPointer = -1, rsPointer = -1;
    private int lastSentButtons = -1;
    private float lastLx, lastLy, lastRx, lastRy;

    public TouchControls(Context context) {
        super(context);
        setFocusable(false);
        line.setStyle(Paint.Style.STROKE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
    }

    public void setListener(Listener l) { listener = l; }

    public void configure(float sizeScale, int alphaPct, boolean vibrate, boolean useLeftStick, boolean useRightStick) {
        scale = sizeScale;
        alpha = Math.max(20, Math.min(255, alphaPct * 255 / 100));
        haptic = vibrate;
        leftStick = useLeftStick;
        rightStick = useRightStick;
        layout(getWidth(), getHeight());
        invalidate();
    }

    /** Lets go of everything (menu opened, view hidden, app paused). */
    public void releaseAll() {
        pressed = 0; lx = ly = rx = ry = 0; knobLx = knobLy = knobRx = knobRy = 0;
        dpadPointer = rsPointer = -1;
        publish();
        invalidate();
    }

    @Override protected void onSizeChanged(int w, int h, int ow, int oh) { layout(w, h); }

    private void layout(int w, int h) {
        if (w <= 0 || h <= 0) return;
        R = h * 0.085f * scale;
        float m = R * 0.6f;
        faceX = w - m - 2.4f * R;
        faceY = h - m - 2.4f * R;
        dpadR = 2.4f * R;
        dpadX = m + dpadR;
        dpadY = h - m - dpadR;
        rsR = 1.4f * R;
        rsX = w - m - rsR - 0.2f * R;
        rsY = faceY - 2.4f * R - rsR - 0.3f * R;
        float bw = 2.3f * R, bh = 1.05f * R, gap = 0.2f * R;
        l2.set(m, m, m + bw, m + bh);
        l1.set(m + bw + gap, m, m + 2 * bw + gap, m + bh);
        r2.set(w - m - bw, m, w - m, m + bh);
        r1.set(w - m - 2 * bw - gap, m, w - m - bw - gap, m + bh);
        float sw = 1.9f * R, sh = 0.85f * R;
        select.set(w / 2f - sw - gap, h - m - sh, w / 2f - gap, h - m);
        start.set(w / 2f + gap, h - m - sh, w / 2f + sw + gap, h - m);
        menuR = 0.6f * R;
        menuX = w / 2f;
        menuY = m + menuR;
    }

    private float faceCx(int i) { return faceX + (i == 2 ? -1.5f * R : i == 3 ? 1.5f * R : 0); }   // 0 up(Y) 1 down(A) 2 left(X) 3 right(B)
    private float faceCy(int i) { return faceY + (i == 0 ? -1.5f * R : i == 1 ? 1.5f * R : 0); }
    private static final int[] FACE_BIT = {8, 1, 4, 2};

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        final int action = e.getActionMasked();
        final int actionIndex = e.getActionIndex();
        if (action == MotionEvent.ACTION_CANCEL) { releaseAll(); return true; }
        int newButtons = 0;
        for (int i = 0; i < e.getPointerCount(); i++) {
            final boolean lifted = (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) && i == actionIndex;
            final int id = e.getPointerId(i);
            if (lifted) {
                if (id == dpadPointer) { dpadPointer = -1; lx = ly = 0; knobLx = knobLy = 0; }
                if (id == rsPointer) { rsPointer = -1; rx = ry = 0; knobRx = knobRy = 0; }
                continue;
            }
            final float x = e.getX(i), y = e.getY(i);
            final boolean down = (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) && i == actionIndex;
            if (down && dist(x, y, menuX, menuY) <= menuR * 1.4f) { if (listener != null) listener.onMenu(); continue; }
            if (id == dpadPointer) { newButtons |= dpadOrStick(x, y); continue; }
            if (id == rsPointer) { rightStickMove(x, y); continue; }
            if (down) {
                if (dist(x, y, dpadX, dpadY) <= dpadR * (leftStick ? 1.3f : 1.35f)) { dpadPointer = id; newButtons |= dpadOrStick(x, y); continue; }
                if (rightStick && dist(x, y, rsX, rsY) <= rsR * 1.4f) { rsPointer = id; rightStickMove(x, y); continue; }
            }
            newButtons |= buttonsAt(x, y);
        }
        // A pointer that ended without ever being captured leaves nothing behind; captured sticks were cleared above.
        if (action == MotionEvent.ACTION_UP) { dpadPointer = rsPointer = -1; lx = ly = rx = ry = 0; knobLx = knobLy = knobRx = knobRy = 0; }
        final int added = newButtons & ~pressed;
        pressed = newButtons;
        if (haptic && added != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        publish();
        invalidate();
        return true;
    }

    private static float dist(float x, float y, float cx, float cy) { return (float) Math.hypot(x - cx, y - cy); }

    private int buttonsAt(float x, float y) {
        int bits = 0;
        float best = R * 1.3f;
        int bestBit = 0;
        for (int i = 0; i < 4; i++) {
            float d = dist(x, y, faceCx(i), faceCy(i));
            if (d < best) { best = d; bestBit = FACE_BIT[i]; }
        }
        bits |= bestBit;
        if (hit(l1, x, y)) bits |= 16;
        if (hit(r1, x, y)) bits |= 32;
        if (hit(l2, x, y)) bits |= 64;
        if (hit(r2, x, y)) bits |= 128;
        if (hit(start, x, y)) bits |= 256;
        if (hit(select, x, y)) bits |= 512;
        return bits;
    }

    private static boolean hit(RectF r, float x, float y) {
        final float s = 0.25f * r.height();
        return x >= r.left - s && x <= r.right + s && y >= r.top - s && y <= r.bottom + s;
    }

    /** D-pad: 8 directions from the angle; left-stick mode: proportional axes. Returns the bits to merge in. */
    private int dpadOrStick(float x, float y) {
        float dx = x - dpadX, dy = y - dpadY;
        if (leftStick) {
            float r = dpadR * 0.75f;
            float len = (float) Math.hypot(dx, dy);
            if (len > r) { dx = dx / len * r; dy = dy / len * r; }
            lx = dx / r; ly = dy / r;
            knobLx = dx; knobLy = dy;
            return 0;
        }
        lx = ly = 0;
        float len = (float) Math.hypot(dx, dy);
        if (len < dpadR * 0.3f) return 0;
        double a = Math.atan2(dy, dx);   // screen y grows downwards
        int bits = 0;
        if (Math.cos(a) > 0.38) bits |= RIGHT;
        if (Math.cos(a) < -0.38) bits |= LEFT;
        if (Math.sin(a) > 0.38) bits |= DOWN;
        if (Math.sin(a) < -0.38) bits |= UP;
        return bits;
    }

    private void rightStickMove(float x, float y) {
        float dx = x - rsX, dy = y - rsY;
        float r = rsR * 0.8f;
        float len = (float) Math.hypot(dx, dy);
        if (len > r) { dx = dx / len * r; dy = dy / len * r; }
        rx = dx / r; ry = dy / r;
        knobRx = dx; knobRy = dy;
    }

    private void publish() {
        if (listener == null) return;
        if (pressed == lastSentButtons && lx == lastLx && ly == lastLy && rx == lastRx && ry == lastRy) return;
        lastSentButtons = pressed; lastLx = lx; lastLy = ly; lastRx = rx; lastRy = ry;
        listener.onTouchState(pressed, lx, ly, rx, ry);
    }

    // ---- Drawing ------------------------------------------------------------------------------------------------------

    private void body(float cx, float cy, float r, boolean on) {
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(on ? 0xFFFFFFFF : 0xFF2A2140);
        fill.setAlpha(on ? Math.min(255, alpha + 60) : alpha);
        canvas.drawCircle(cx, cy, r, fill);
        line.setColor(0xFFFFFFFF);
        line.setAlpha(Math.min(255, alpha + 40));
        line.setStrokeWidth(Math.max(2f, R * 0.06f));
        canvas.drawCircle(cx, cy, r, line);
    }

    private void pill(RectF r, boolean on, String label) {
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(on ? 0xFFFFFFFF : 0xFF2A2140);
        fill.setAlpha(on ? Math.min(255, alpha + 60) : alpha);
        canvas.drawRoundRect(r, r.height() / 2, r.height() / 2, fill);
        line.setColor(0xFFFFFFFF);
        line.setAlpha(Math.min(255, alpha + 40));
        line.setStrokeWidth(Math.max(2f, R * 0.06f));
        canvas.drawRoundRect(r, r.height() / 2, r.height() / 2, line);
        label(label, r.centerX(), r.centerY(), r.height() * 0.5f, on);
    }

    private void label(String s, float cx, float cy, float size, boolean on) {
        text.setTextSize(size);
        text.setColor(on ? 0xFF2A2140 : 0xFFFFFFFF);
        text.setAlpha(Math.min(255, alpha + 80));
        canvas.drawText(s, cx, cy - (text.descent() + text.ascent()) / 2f, text);
    }

    private void symbol(int index, float cx, float cy, boolean on) {
        line.setColor(on ? 0xFF2A2140 : 0xFFFFFFFF);
        line.setAlpha(Math.min(255, alpha + 90));
        line.setStrokeWidth(Math.max(3f, R * 0.11f));
        line.setStrokeCap(Paint.Cap.ROUND);
        final float s = R * 0.38f;
        switch (index) {
            case 0: path.reset(); path.moveTo(cx, cy - s); path.lineTo(cx + s, cy + s * 0.8f); path.lineTo(cx - s, cy + s * 0.8f); path.close(); canvas.drawPath(path, line); break;   // triangle (Y)
            case 1: canvas.drawLine(cx - s, cy - s, cx + s, cy + s, line); canvas.drawLine(cx + s, cy - s, cx - s, cy + s, line); break;   // cross (A)
            case 2: canvas.drawRect(cx - s, cy - s, cx + s, cy + s, line); break;   // square (X)
            default: canvas.drawCircle(cx, cy, s, line); break;   // circle (B)
        }
    }

    private Canvas canvas;

    @Override
    protected void onDraw(Canvas c) {
        canvas = c;
        if (R <= 0) layout(getWidth(), getHeight());
        // D-pad or left stick
        if (leftStick) {
            body(dpadX, dpadY, dpadR, false);
            body(dpadX + knobLx, dpadY + knobLy, R * 0.9f, dpadPointer >= 0);
        } else {
            drawDpad();
        }
        if (rightStick) {
            body(rsX, rsY, rsR, false);
            body(rsX + knobRx, rsY + knobRy, R * 0.65f, rsPointer >= 0);
        }
        for (int i = 0; i < 4; i++) {
            boolean on = (pressed & FACE_BIT[i]) != 0;
            body(faceCx(i), faceCy(i), R * 0.95f, on);
            symbol(i, faceCx(i), faceCy(i), on);
        }
        pill(l1, (pressed & 16) != 0, "L1");
        pill(l2, (pressed & 64) != 0, "L2");
        pill(r1, (pressed & 32) != 0, "R1");
        pill(r2, (pressed & 128) != 0, "R2");
        pill(select, (pressed & 512) != 0, "SELECT");
        pill(start, (pressed & 256) != 0, "START");
        body(menuX, menuY, menuR, false);
        line.setColor(0xFFFFFFFF);
        line.setAlpha(Math.min(255, alpha + 90));
        line.setStrokeWidth(Math.max(3f, R * 0.09f));
        for (int i = -1; i <= 1; i++) canvas.drawLine(menuX - menuR * 0.45f, menuY + i * menuR * 0.32f, menuX + menuR * 0.45f, menuY + i * menuR * 0.32f, line);
    }

    private void drawDpad() {
        final float a = dpadR * 0.36f, l = dpadR * 0.95f;   // arm half-width and length
        fill.setStyle(Paint.Style.FILL);
        arm(dpadX - a, dpadY - l, dpadX + a, dpadY - a * 0.2f, (pressed & UP) != 0);
        arm(dpadX - a, dpadY + a * 0.2f, dpadX + a, dpadY + l, (pressed & DOWN) != 0);
        arm(dpadX - l, dpadY - a, dpadX - a * 0.2f, dpadY + a, (pressed & LEFT) != 0);
        arm(dpadX + a * 0.2f, dpadY - a, dpadX + l, dpadY + a, (pressed & RIGHT) != 0);
    }

    private void arm(float x0, float y0, float x1, float y1, boolean on) {
        fill.setColor(on ? 0xFFFFFFFF : 0xFF2A2140);
        fill.setAlpha(on ? Math.min(255, alpha + 60) : alpha);
        RectF r = new RectF(x0, y0, x1, y1);
        float rad = R * 0.25f;
        canvas.drawRoundRect(r, rad, rad, fill);
        line.setColor(0xFFFFFFFF);
        line.setAlpha(Math.min(255, alpha + 40));
        line.setStrokeWidth(Math.max(2f, R * 0.06f));
        canvas.drawRoundRect(r, rad, rad, line);
    }
}
