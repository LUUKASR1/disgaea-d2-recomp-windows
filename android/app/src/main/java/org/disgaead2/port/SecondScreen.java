package org.disgaead2.port;

import android.app.Presentation;
import android.content.Context;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Display;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Content for the secondary display (AYN Thor bottom screen): command map and live status. */
final class SecondScreen extends Presentation {
    private TextView status;

    SecondScreen(Context context, Display display) {
        super(context, display);
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = new LinearLayout(getContext());
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setBackgroundColor(Color.rgb(244, 247, 253));
        root.setPadding(32, 24, 32, 24);
        TextView title = new TextView(getContext());
        title.setText(R.string.second_title);
        title.setTextSize(24);
        title.setTextColor(Color.rgb(29, 37, 71));
        TextView commands = new TextView(getContext());
        commands.setText(R.string.second_commands);
        commands.setTextSize(16);
        commands.setTextColor(Color.rgb(29, 37, 71));
        status = new TextView(getContext());
        status.setTextSize(14);
        status.setTextColor(Color.rgb(194, 106, 0));
        root.addView(title);
        root.addView(commands);
        root.addView(status);
        setContentView(root);
    }

    void setStatus(String text) {
        if (status != null) status.setText(text);
    }
}
