package org.disgaead2.bootstrap;

import android.app.NativeActivity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** NativeActivity owns the surface; the overlay only labels and shares diagnostics. */
public final class BootstrapActivity extends NativeActivity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(18, 10, 18, 10);
        panel.setBackgroundColor(0xcc21152c);
        TextView notice = new TextView(this);
        notice.setText(R.string.test_notice);
        notice.setTextColor(0xffffb16a);
        notice.setTextSize(16);
        panel.addView(notice);
        Button report = new Button(this);
        report.setText(R.string.share_report);
        report.setFocusable(false); // Gamepad navigation stays with NativeActivity.
        report.setOnClickListener(v -> shareReport());
        panel.addView(report);
        android.widget.FrameLayout.LayoutParams layout = new android.widget.FrameLayout.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.START);
        addContentView(panel, layout);
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
