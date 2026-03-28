package com.kite.zchat;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.materialswitch.MaterialSwitch;

import com.kite.zchat.chat.ChatEvents;
import com.kite.zchat.chat.ChatMessageDb;
import com.kite.zchat.conversation.ConversationPlaceholderStore;

public final class ChatSettingsActivity extends AppCompatActivity {

    public static final String EXTRA_HOST = "host";
    public static final String EXTRA_PORT = "port";
    public static final String EXTRA_PEER_USER_ID_HEX = "peer_user_id_hex";
    public static final String EXTRA_PEER_DISPLAY_NAME = "peer_display_name";

    public static Intent buildIntent(
            Context context,
            String host,
            int port,
            String peerHex32,
            String displayName) {
        Intent i = new Intent(context, ChatSettingsActivity.class);
        i.putExtra(EXTRA_HOST, host);
        i.putExtra(EXTRA_PORT, port);
        i.putExtra(EXTRA_PEER_USER_ID_HEX, peerHex32);
        i.putExtra(EXTRA_PEER_DISPLAY_NAME, displayName != null ? displayName : "");
        return i;
    }

    private String peerHex;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_chat_settings);

        peerHex = getIntent().getStringExtra(EXTRA_PEER_USER_ID_HEX);
        if (peerHex == null || peerHex.length() != 32) {
            finish();
            return;
        }

        MaterialToolbar toolbar = findViewById(R.id.chatSettingsToolbar);
        toolbar.setNavigationOnClickListener(v -> finish());
        toolbar.setTitle(R.string.chat_settings_title);

        MaterialSwitch mute = findViewById(R.id.chatSettingsMute);
        mute.setChecked(ConversationPlaceholderStore.isPeerMuted(this, peerHex));
        mute.setOnCheckedChangeListener(
                (buttonView, isChecked) ->
                        ConversationPlaceholderStore.setPeerMuted(ChatSettingsActivity.this, peerHex, isChecked));

        MaterialButton clear = findViewById(R.id.chatSettingsClear);
        clear.setOnClickListener(v -> showClearConfirm());
    }

    private void showClearConfirm() {
        new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.chat_settings_clear_confirm_title)
                .setMessage(R.string.chat_settings_clear_confirm_message)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(
                        R.string.chat_settings_clear_confirm_action,
                        (d, w) -> runClearLocalHistory())
                .show();
    }

    private void runClearLocalHistory() {
        new ChatMessageDb(this).deleteMessagesForPeer(peerHex);
        long now = System.currentTimeMillis();
        ConversationPlaceholderStore.updatePreviewAndTime(
                this,
                peerHex,
                getString(R.string.conversation_preview_default),
                now);
        Intent list = new Intent(ChatEvents.ACTION_CONVERSATION_LIST_CHANGED);
        list.setPackage(getPackageName());
        sendBroadcast(list);
        Intent msg = new Intent(ChatEvents.ACTION_CHAT_MESSAGES_CHANGED);
        msg.setPackage(getPackageName());
        msg.putExtra(ChatEvents.EXTRA_PEER_HEX, peerHex);
        sendBroadcast(msg);
        setResult(RESULT_OK);
        finish();
    }
}
