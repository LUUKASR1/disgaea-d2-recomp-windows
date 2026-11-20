package org.disgaead2.port;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.view.View;

import java.util.ArrayDeque;

/** PlayStation-style trophy banner (dark blue glass, glowing accent trail, metal cup of the trophy's grade, the game's own icon).
 *  Slides down from the top centre, stays a few seconds, slides back up. Never takes touches. */
final class TrophyToast extends View {
    private static final long IN_MS = 350, HOLD_MS = 3400, OUT_MS = 350;

    private static final class Item {
        final String title, subtitle, grade; final boolean blocked; final Bitmap bitmap;
        Item(String title, String subtitle, boolean blocked, Bitmap bitmap, String grade) {
            this.title = title; this.subtitle = subtitle; this.blocked = blocked; this.bitmap = bitmap; this.grade = grade == null ? "" : grade;
        }
    }

    private final ArrayDeque<Item> queue = new ArrayDeque<>();
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), stroke = new Paint(Paint.ANTI_ALIAS_FLAG),
            text = new Paint(Paint.ANTI_ALIAS_FLAG), metal = new Paint(Paint.ANTI_ALIAS_FLAG),
            bmp = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF box = new RectF(), tmp = new RectF();
    private final Path path = new Path();
    private Item current;
    private float slide;
    private ValueAnimator animator;

    TrophyToast(Context context) {
        super(context);
        setClickable(false);
        setFocusable(false);
        stroke.setStyle(Paint.Style.STROKE);
        setVisibility(GONE);
    }

    @Override public boolean onTouchEvent(android.view.MotionEvent event) { return false; }

    private float dp(float v) { return v * getResources().getDisplayMetrics().density; }

    /** grade: B / S / G / P (anything else = gold). bitmap = the game's own trophy icon, or null. */
    void show(String title, String subtitle, boolean blocked, Bitmap bitmap, String grade) {
        if (queue.size() < 6) queue.add(new Item(title, subtitle, blocked, bitmap, grade));
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
            if (t < IN_MS) slide = 1f - (float) Math.pow(1f - t / (float) IN_MS, 3);
            else if (t < IN_MS + HOLD_MS) slide = 1f;
            else slide = 1f - (float) Math.pow((t - IN_MS - HOLD_MS) / (float) OUT_MS, 2);
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

    /** {light, mid, dark, glow} of the metal for a grade. */
    private static int[] palette(String grade) {
        switch (grade) {
            case "B": return new int[]{0xFFFFD7B0, 0xFFC97B3C, 0xFF6B3512, 0xFFFF8A4C};
            case "S": return new int[]{0xFFFFFFFF, 0xFFC3CCD8, 0xFF6B7686, 0xFFBFD4FF};
            case "P": return new int[]{0xFFF2FBFF, 0xFF8FD0FF, 0xFF2D6FE0, 0xFF5CB6FF};
            default:  return new int[]{0xFFFFF2B5, 0xFFE8A91C, 0xFF8A5A00, 0xFFFFC857};
        }
    }

    @Override protected void onDraw(Canvas canvas) {
        final Item item = current;
        if (item == null || slide <= 0f) return;
        final float w = Math.min(getWidth() * 0.66f, dp(470)), h = dp(76);
        final float left = (getWidth() - w) / 2f;
        final float top = dp(12) - (1f - slide) * (h + dp(30));
        box.set(left, top, left + w, top + h);
        final float radius = dp(16);
        final int[] pal = item.blocked ? new int[]{0xFFFFD0CA, 0xFFE5604F, 0xFF7A1B14, 0xFFFF6A5E} : palette(item.grade);

        // soft outer glow, then the navy glass body
        for (int i = 3; i >= 1; i--) {
            stroke.setStrokeWidth(dp(2.5f * i));
            stroke.setColor((pal[3] & 0x00FFFFFF) | ((18 + (3 - i) * 8) << 24));
            tmp.set(box); tmp.inset(-dp(i), -dp(i));
            canvas.drawRoundRect(tmp, radius + dp(i), radius + dp(i), stroke);
        }
        fill.setShader(new LinearGradient(left, top, left + w, top + h, 0xF2071535, 0xF20F2D66, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(box, radius, radius, fill);
        fill.setShader(new LinearGradient(0, top, 0, top + h * 0.5f, 0x33FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(box, radius, radius, fill);   // top sheen
        fill.setShader(null);
        stroke.setStrokeWidth(dp(1.5f));
        stroke.setColor(0x99FFFFFF & (0x66FFFFFF | 0x00000000));
        stroke.setColor(0x66AFC8FF);
        canvas.drawRoundRect(box, radius, radius, stroke);

        // glowing accent trail with a bright end point, like the light streaks of the trophy artwork
        path.reset();
        path.moveTo(left + w * 0.30f, top + h - dp(4));
        path.quadTo(left + w * 0.62f, top + h + dp(1), left + w - dp(16), top + h * 0.62f);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(dp(1.6f));
        stroke.setShader(new LinearGradient(left + w * 0.30f, 0, left + w - dp(16), 0, pal[3] & 0x00FFFFFF, pal[3], Shader.TileMode.CLAMP));
        canvas.save();
        canvas.clipRect(box);
        canvas.drawPath(path, stroke);
        canvas.restore();
        stroke.setShader(null);
        fill.setColor(0xFFFFFFFF);
        canvas.drawCircle(left + w - dp(16), top + h * 0.62f, dp(2.4f), fill);

        // left: the game's own icon (when the game files are available)
        float x = left + dp(12);
        if (item.bitmap != null) {
            final float s = dp(52);
            tmp.set(x, top + (h - s) / 2f, x + s, top + (h + s) / 2f);
            canvas.drawBitmap(item.bitmap, null, tmp, bmp);
            stroke.setStrokeWidth(dp(1.5f));
            stroke.setColor(0xAAFFFFFF);
            canvas.drawRoundRect(tmp, dp(6), dp(6), stroke);
            if (item.blocked) {
                stroke.setColor(0xFFE5604F);
                stroke.setStrokeWidth(dp(3));
                canvas.drawLine(tmp.left, tmp.bottom, tmp.right, tmp.top, stroke);
            }
            x += s + dp(12);
        } else {
            drawCup(canvas, x + dp(20), top + h / 2f, dp(21), pal, item.blocked);
            x += dp(40) + dp(10);
        }

        // right: the cup of this trophy's grade
        float textRight = left + w - dp(14);
        if (item.bitmap != null) {
            drawCup(canvas, left + w - dp(34), top + h / 2f, dp(20), pal, item.blocked);
            textRight = left + w - dp(34) - dp(26);
        }

        text.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        text.setColor(0xFFFFFFFF);
        text.setTextSize(dp(15));
        canvas.drawText(fit(item.title, textRight - x, text), x, top + h / 2f - dp(4), text);
        text.setTypeface(android.graphics.Typeface.DEFAULT);
        text.setColor(0xFFB9CCF5);
        text.setTextSize(dp(12.5f));
        canvas.drawText(fit(item.subtitle, textRight - x, text), x, top + h / 2f + dp(15), text);
    }

    private static String fit(String s, float width, Paint p) {
        if (p.measureText(s) <= width) return s;
        int n = s.length();
        while (n > 1 && p.measureText(s, 0, n) + p.measureText("…") > width) n--;
        return s.substring(0, n) + "…";
    }

    /** Metal trophy cup (bowl, two handles, stem, base) with a glow behind it; a slash across it when blocked. */
    private void drawCup(Canvas c, float cx, float cy, float s, int[] pal, boolean blocked) {
        fill.setShader(new RadialGradient(cx, cy, s * 1.6f, (pal[3] & 0x00FFFFFF) | 0x66000000, pal[3] & 0x00FFFFFF, Shader.TileMode.CLAMP));
        c.drawCircle(cx, cy, s * 1.6f, fill);
        fill.setShader(null);

        final LinearGradient body = new LinearGradient(cx - s, cy - s, cx + s, cy + s, new int[]{pal[0], pal[1], pal[2]},
                new float[]{0f, 0.5f, 1f}, Shader.TileMode.CLAMP);
        metal.setShader(body);
        metal.setStyle(Paint.Style.FILL);
        path.reset();
        path.moveTo(cx - s * 0.62f, cy - s * 0.85f);
        path.lineTo(cx + s * 0.62f, cy - s * 0.85f);
        path.lineTo(cx + s * 0.50f, cy + s * 0.05f);
        path.quadTo(cx + s * 0.40f, cy + s * 0.45f, cx, cy + s * 0.50f);
        path.quadTo(cx - s * 0.40f, cy + s * 0.45f, cx - s * 0.50f, cy + s * 0.05f);
        path.close();
        c.drawPath(path, metal);
        metal.setStyle(Paint.Style.STROKE);
        metal.setStrokeWidth(s * 0.2f);
        tmp.set(cx - s * 1.15f, cy - s * 0.72f, cx - s * 0.42f, cy - s * 0.05f);
        c.drawArc(tmp, 90, 180, false, metal);
        tmp.set(cx + s * 0.42f, cy - s * 0.72f, cx + s * 1.15f, cy - s * 0.05f);
        c.drawArc(tmp, -90, 180, false, metal);
        metal.setStyle(Paint.Style.FILL);
        c.drawRect(cx - s * 0.12f, cy + s * 0.45f, cx + s * 0.12f, cy + s * 0.85f, metal);
        c.drawRoundRect(cx - s * 0.55f, cy + s * 0.82f, cx + s * 0.55f, cy + s * 1.08f, s * 0.08f, s * 0.08f, metal);
        metal.setShader(null);
        // highlight on the bowl
        fill.setColor(0x66FFFFFF);
        path.reset();
        path.moveTo(cx - s * 0.46f, cy - s * 0.74f);
        path.lineTo(cx - s * 0.22f, cy - s * 0.74f);
        path.lineTo(cx - s * 0.20f, cy - s * 0.05f);
        path.lineTo(cx - s * 0.36f, cy - s * 0.1f);
        path.close();
        c.drawPath(path, fill);
        if (blocked) {
            metal.setColor(0xFFFFFFFF);
            metal.setStyle(Paint.Style.STROKE);
            metal.setStrokeWidth(s * 0.2f);
            c.drawLine(cx - s * 1.2f, cy + s * 1.0f, cx + s * 1.2f, cy - s * 1.0f, metal);
        }
    }
}
