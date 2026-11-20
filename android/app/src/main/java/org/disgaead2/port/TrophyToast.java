package org.disgaead2.port;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.view.View;

import java.util.ArrayDeque;

/** PlayStation-style trophy banner: slides down from the top centre, stays a few seconds, slides back up. Never takes touches. */
final class TrophyToast extends View {
    private static final long IN_MS = 350, HOLD_MS = 3800, OUT_MS = 350;

    private static final class Item {
        final String title, subtitle; final boolean blocked; final android.graphics.Bitmap bitmap; final int color;
        Item(String title, String subtitle, boolean blocked, android.graphics.Bitmap bitmap, int color) {
            this.title = title; this.subtitle = subtitle; this.blocked = blocked; this.bitmap = bitmap; this.color = color;
        }
    }

    private final ArrayDeque<Item> queue = new ArrayDeque<>();
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), stroke = new Paint(Paint.ANTI_ALIAS_FLAG),
            text = new Paint(Paint.ANTI_ALIAS_FLAG), icon = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF box = new RectF(), box2 = new RectF();
    private final Paint icon2 = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Path path = new Path();
    private Item current;
    private float slide;   // 0 = hidden above the screen, 1 = fully shown
    private ValueAnimator animator;

    TrophyToast(Context context) {
        super(context);
        setClickable(false);
        setFocusable(false);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(dp(1.5f));
        text.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        setVisibility(GONE);
    }

    @Override public boolean onTouchEvent(android.view.MotionEvent event) { return false; }

    private float dp(float v) { return v * getResources().getDisplayMetrics().density; }

    /** Queues a banner; several trophies in a row are shown one after the other. */
    void show(String title, String subtitle, boolean blocked) { show(title, subtitle, blocked, null, 0); }

    /** bitmap = the game's own trophy icon (null = drawn cup); color = grade colour (0 = default). */
    void show(String title, String subtitle, boolean blocked, android.graphics.Bitmap bitmap, int color) {
        if (queue.size() < 6) queue.add(new Item(title, subtitle, blocked, bitmap, color));
        if (current == null) next();
    }

    private void next() {
        current = queue.poll();
        if (current == null) { setVisibility(GONE); return; }
        setVisibility(VISIBLE);
        if (animator != null) animator.cancel();
        final long total = IN_MS + HOLD_MS + OUT_MS;
        animator = ValueAnimator.ofFloat(0f, 1f);
        animator.setDuration(total);
        animator.setInterpolator(null);
        animator.addUpdateListener(a -> {
            final long t = (long) (((float) a.getAnimatedValue()) * total);
            if (t < IN_MS) slide = 1f - (float) Math.pow(1f - t / (float) IN_MS, 3);          // ease-out
            else if (t < IN_MS + HOLD_MS) slide = 1f;
            else slide = 1f - (float) Math.pow((t - IN_MS - HOLD_MS) / (float) OUT_MS, 2);    // ease-in
            invalidate();
        });
        animator.addListener(new android.animation.AnimatorListenerAdapter() {
            private boolean cancelled;
            @Override public void onAnimationCancel(android.animation.Animator a) { cancelled = true; }
            @Override public void onAnimationEnd(android.animation.Animator a) { if (!cancelled) { current = null; next(); } }
        });
        slide = 0f;
        animator.start();
    }

    @Override protected void onDetachedFromWindow() {
        if (animator != null) animator.cancel();
        super.onDetachedFromWindow();
    }

    @Override protected void onDraw(Canvas canvas) {
        final Item item = current;
        if (item == null || slide <= 0f) return;
        final float w = Math.min(getWidth() * 0.62f, dp(430)), h = dp(64);
        final float left = (getWidth() - w) / 2f;
        final float top = dp(14) - (1f - slide) * (h + dp(30));
        box.set(left, top, left + w, top + h);
        final int accent = item.blocked ? 0xFFE5604F : (item.color != 0 ? item.color : 0xFFFFC857);
        fill.setColor(0xE60E0A1C);
        canvas.drawRoundRect(box, dp(14), dp(14), fill);
        stroke.setColor((accent & 0x00FFFFFF) | 0x99000000);
        canvas.drawRoundRect(box, dp(14), dp(14), stroke);

        // cup icon in a round badge
        final float cx = left + dp(12) + dp(24), cy = top + h / 2f, r = dp(24);
        fill.setColor((accent & 0x00FFFFFF) | 0x33000000);
        canvas.drawCircle(cx, cy, r, fill);
        if (item.bitmap != null) {
            final float half = dp(24);
            box2.set(cx - half, cy - half, cx + half, cy + half);
            canvas.drawBitmap(item.bitmap, null, box2, icon2);
            if (item.blocked) {
                icon.setColor(0xFFE5604F);
                icon.setStyle(Paint.Style.STROKE);
                icon.setStrokeWidth(dp(3));
                canvas.drawLine(cx - half, cy + half, cx + half, cy - half, icon);
            }
        } else {
            drawCup(canvas, cx, cy, r * 0.62f, accent, item.blocked);
        }

        final float tx = cx + r + dp(12);
        text.setColor(0xFFFFFFFF);
        text.setTextSize(dp(15));
        canvas.drawText(fit(item.title, left + w - dp(12) - tx, text), tx, top + h / 2f - dp(3), text);
        text.setTypeface(android.graphics.Typeface.DEFAULT);
        text.setColor(0xFFCFC6E6);
        text.setTextSize(dp(12.5f));
        canvas.drawText(fit(item.subtitle, left + w - dp(12) - tx, text), tx, top + h / 2f + dp(16), text);
        text.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
    }

    private static String fit(String s, float width, Paint p) {
        if (p.measureText(s) <= width) return s;
        int n = s.length();
        while (n > 1 && p.measureText(s, 0, n) + p.measureText("…") > width) n--;
        return s.substring(0, n) + "…";
    }

    /** A simple trophy cup: bowl, two handles, stem and base; a slash across it when the trophy was blocked. */
    private void drawCup(Canvas c, float cx, float cy, float s, int color, boolean blocked) {
        icon.setColor(color);
        icon.setStyle(Paint.Style.FILL);
        path.reset();
        path.moveTo(cx - s * 0.62f, cy - s * 0.85f);
        path.lineTo(cx + s * 0.62f, cy - s * 0.85f);
        path.lineTo(cx + s * 0.50f, cy + s * 0.05f);
        path.quadTo(cx + s * 0.40f, cy + s * 0.45f, cx, cy + s * 0.50f);
        path.quadTo(cx - s * 0.40f, cy + s * 0.45f, cx - s * 0.50f, cy + s * 0.05f);
        path.close();
        c.drawPath(path, icon);
        icon.setStyle(Paint.Style.STROKE);
        icon.setStrokeWidth(s * 0.22f);
        final RectF handle = new RectF();
        handle.set(cx - s * 1.15f, cy - s * 0.72f, cx - s * 0.45f, cy - s * 0.05f);
        c.drawArc(handle, 90, 180, false, icon);
        handle.set(cx + s * 0.45f, cy - s * 0.72f, cx + s * 1.15f, cy - s * 0.05f);
        c.drawArc(handle, -90, 180, false, icon);
        icon.setStyle(Paint.Style.FILL);
        c.drawRect(cx - s * 0.12f, cy + s * 0.45f, cx + s * 0.12f, cy + s * 0.85f, icon);
        c.drawRoundRect(cx - s * 0.52f, cy + s * 0.82f, cx + s * 0.52f, cy + s * 1.05f, s * 0.08f, s * 0.08f, icon);
        if (blocked) {
            icon.setColor(0xFFFFFFFF);
            icon.setStyle(Paint.Style.STROKE);
            icon.setStrokeWidth(s * 0.2f);
            c.drawLine(cx - s * 1.2f, cy + s * 1.0f, cx + s * 1.2f, cy - s * 1.0f, icon);
        }
    }
}
