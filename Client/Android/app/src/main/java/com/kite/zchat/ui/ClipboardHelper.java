package com.kite.zchat.ui;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.widget.Toast;

import com.kite.zchat.R;

public final class ClipboardHelper {

    private ClipboardHelper() {}

    public static void copyPlainText(Context context, CharSequence label, CharSequence text) {
        if (text == null || text.length() == 0) {
            return;
        }
        ClipboardManager cm = (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm == null) {
            return;
        }
        cm.setPrimaryClip(ClipData.newPlainText(label, text));
        Toast.makeText(context, R.string.copied_to_clipboard, Toast.LENGTH_SHORT).show();
    }
}
