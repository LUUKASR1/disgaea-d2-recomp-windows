package org.disgaead2.bootstrap;

import android.app.NativeActivity;
import android.content.Intent;
import android.content.res.AssetFileDescriptor;
import android.net.Uri;
import android.os.CancellationSignal;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.nio.file.StandardCopyOption;
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
    // NativeActivity opens its entry library separately. Register it with the
    // Java class loader as well so our descriptor JNI method resolves reliably.
    static { System.loadLibrary("d2_android_bootstrap"); }
    private PopupWindow diagnostics;
    private static final int PICK_DOCUMENT = 41;
    private final ExecutorService reader = Executors.newSingleThreadExecutor();
    private CancellationSignal cancellation;
    private Button select;
    private TextView documentStatus;
    private static native String inspectDescriptor(int fd, long base, long length);

    private void saveDocumentReport(String text) {
        // Small report without filename, URI, paths or file contents.
        try {
            File temporary = new File(getFilesDir(), "selected-document-report.tmp");
            Files.write(temporary.toPath(), text.getBytes(StandardCharsets.UTF_8));
            Files.move(temporary.toPath(), new File(getFilesDir(), "selected-document-report.txt").toPath(),
                    StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        } catch (java.io.IOException e) {
            runOnUiThread(() -> { if (!isDestroyed()) documentStatus.setText(R.string.document_report_error); });
        }
    }

    private void chooseDocument() {
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        pick.putExtra(Intent.EXTRA_LOCAL_ONLY, true);
        pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try { startActivityForResult(pick, PICK_DOCUMENT); }
        catch (android.content.ActivityNotFoundException e) {
            saveDocumentReport("document_status=rejected\ndocument_reason=no-picker\ndocument_game_boot=false\n");
            documentStatus.setText(R.string.document_no_picker);
        }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_DOCUMENT) return;
        if (result != RESULT_OK || data == null || data.getData() == null) {
            saveDocumentReport("document_status=cancelled\ndocument_game_boot=false\n");
            documentStatus.setText(R.string.document_cancelled); return;
        }
        Uri uri = data.getData();
        select.setEnabled(false);
        documentStatus.setText(R.string.document_reading);
        cancellation = new CancellationSignal();
        final CancellationSignal signal = cancellation;
        reader.execute(() -> {
            String report;
            try (AssetFileDescriptor asset = getContentResolver().openAssetFileDescriptor(uri, "r", signal)) {
                if (asset == null) throw new java.io.IOException("No descriptor");
                report = inspectDescriptor(asset.getParcelFileDescriptor().getFd(),
                        asset.getStartOffset(), asset.getDeclaredLength());
            } catch (SecurityException e) {
                report = "document_status=rejected\ndocument_reason=permission-denied\ndocument_game_boot=false\n";
            } catch (Exception e) {
                report = "document_status=rejected\ndocument_reason=provider-or-read-error\ndocument_game_boot=false\n";
            }
            final String completed = report;
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                saveDocumentReport(completed);
                select.setEnabled(true);
                documentStatus.setText(completed.contains("disc_status=base-files-readable\n") ? R.string.disc_readable :
                        completed.contains("disc_status=other-title\n") ? R.string.disc_other_title :
                        completed.contains("disc_status=missing-or-unreadable-base-files\n") ? R.string.disc_missing :
                        completed.contains("disc_status=rejected\n") ? R.string.disc_rejected :
                        completed.contains("document_status=readable\n") ? R.string.document_readable :
                        completed.contains("document_status=empty\n") ? R.string.document_empty : R.string.document_rejected);
            });
        });
    }

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
        select = new Button(this);
        select.setText(R.string.select_document);
        select.setFocusable(false);
        select.setOnClickListener(v -> chooseDocument());
        panel.addView(select);
        documentStatus = new TextView(this);
        documentStatus.setText(R.string.document_notice);
        documentStatus.setTextColor(0xffffffff);
        documentStatus.setMaxWidth(dp(340));
        panel.addView(documentStatus);
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
        if (cancellation != null) cancellation.cancel();
        reader.shutdownNow();
        super.onDestroy();
    }

    private void shareReport() {
        try {
            String text = new String(Files.readAllBytes(new File(getFilesDir(),
                    "android-vulkan-report.txt").toPath()), StandardCharsets.UTF_8);
            File selected = new File(getFilesDir(), "selected-document-report.txt");
            if (selected.isFile()) text += "\n" + new String(Files.readAllBytes(selected.toPath()), StandardCharsets.UTF_8);
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
