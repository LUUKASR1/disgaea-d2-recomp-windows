package org.disgaead2.bootstrap;

import android.app.NativeActivity;
import android.content.Intent;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** NativeActivity owns the surface; the overlay only labels and shares diagnostics. */
public final class BootstrapActivity extends NativeActivity {
    private PopupWindow diagnostics;

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(14), dp(10), dp(14), dp(10));
        panel.setBackgroundColor(0xcc21152c);
        TextView notice = new TextView(this);
        notice.setText(R.string.test_notice);
        notice.setTextColor(0xffffb16a);
        notice.setTextSize(16);
        notice.setMaxWidth(dp(340));
        panel.addView(notice);
        Button report = new Button(this);
        report.setText(R.string.share_report);
        report.setFocusable(false); // Gamepad navigation stays with NativeActivity.
        report.setOnClickListener(v -> shareReport());
        panel.addView(report);
        // NativeActivity.takeSurface gives Vulkan the main window surface.
        // Ordinary addContentView widgets are overwritten by native presents.
        // A non-focusable PopupWindow gets its own compositor surface above it.
        diagnostics = new PopupWindow(panel, WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT, false);
        diagnostics.setBackgroundDrawable(new ColorDrawable(0xee21152c));
        diagnostics.setElevation(dp(8));
        diagnostics.setOutsideTouchable(false);
        diagnostics.setInputMethodMode(PopupWindow.INPUT_METHOD_NOT_NEEDED);
        getWindow().getDecorView().post(this::showDiagnostics);
    }

    private void showDiagnostics() {
        if (diagnostics == null || diagnostics.isShowing() || isFinishing() || isDestroyed()
                || !hasWindowFocus() || getWindow().getDecorView().getWindowToken() == null) return;
        diagnostics.showAtLocation(getWindow().getDecorView(), Gravity.TOP | Gravity.START,
                dp(12), dp(56));
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) getWindow().getDecorView().post(this::showDiagnostics);
    }

    @Override public void onResume() {
        super.onResume();
        getWindow().getDecorView().post(this::showDiagnostics);
    }

    @Override public void onPause() {
        if (diagnostics != null) diagnostics.dismiss();
        super.onPause();
    }

    @Override public void onDestroy() {
        if (diagnostics != null) diagnostics.dismiss();
        super.onDestroy();
    }

    private void shareReport() {
        try {
            String text = new String(Files.readAllBytes(new File(getFilesDir(),
                    "android-vulkan-report.txt").toPath()), StandardCharsets.UTF_8);
            // No storage/network permissions. Sharing happens only on this user click.
            Intent send = new Intent(Intent.ACTION_SEND);
            send.setType("text/plain");
            send.putExtra(Intent.EXTRA_SUBJECT, getString(R.string.report_title));
            send.putExtra(Intent.EXTRA_TEXT, text);
            startActivity(Intent.createChooser(send, getString(R.string.share_report)));
        } catch (java.io.IOException e) {
            Toast.makeText(this, R.string.report_pending, Toast.LENGTH_LONG).show();
        }
    }
}
