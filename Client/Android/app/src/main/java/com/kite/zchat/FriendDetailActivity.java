package com.kite.zchat;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.chat.ChatMessageDb;
import com.kite.zchat.contacts.ContactsCache;
import com.kite.zchat.conversation.ConversationPlaceholderStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.friends.FriendIdentityStore;
import com.kite.zchat.friends.FriendSigning;
import com.kite.zchat.friends.FriendZspHelper;
import com.kite.zchat.main.FriendsListAdapter;
import com.kite.zchat.profile.ProfileDisplayHelper;
import com.kite.zchat.zsp.ZspProfileCodec;
import com.kite.zchat.zsp.ZspSessionManager;

import org.bouncycastle.crypto.params.Ed25519PrivateKeyParameters;

import java.util.Arrays;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class FriendDetailActivity extends AppCompatActivity {

    public static final String EXTRA_HOST = "host";
    public static final String EXTRA_PORT = "port";
    public static final String EXTRA_FRIEND_USER_ID_HEX = "friend_user_id_hex";
    public static final String EXTRA_FRIEND_DISPLAY_NAME = "friend_display_name";
    /** {@link android.app.Activity#setResult(int, android.content.Intent)} 中标识已删除好友。 */
    public static final String EXTRA_FRIEND_DELETED = "friend_deleted";

    public static Intent buildIntent(
            Context context,
            String host,
            int port,
            String friendUserIdHex,
            String displayName) {
        Intent i = new Intent(context, FriendDetailActivity.class);
        i.putExtra(EXTRA_HOST, host);
        i.putExtra(EXTRA_PORT, port);
        i.putExtra(EXTRA_FRIEND_USER_ID_HEX, friendUserIdHex);
        i.putExtra(EXTRA_FRIEND_DISPLAY_NAME, displayName != null ? displayName : "");
        return i;
    }

    private final ExecutorService io = Executors.newSingleThreadExecutor();

    private String host;
    private int port;
    private String friendHex;
    private String displayName;

    private MaterialToolbar toolbar;
    private ImageView avatar;
    private TextView nameView;
    private TextView userIdView;
    private MaterialButton copyBtn;
    private MaterialButton chatBtn;
    private MaterialButton deleteBtn;
    private ProgressBar progress;

    /** 最近一次从服务端拉取的头像原始字节，用于写入通信占位会话头像文件。 */
    private byte[] friendAvatarBytes;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_friend_detail);

        friendHex = getIntent().getStringExtra(EXTRA_FRIEND_USER_ID_HEX);
        if (friendHex == null || friendHex.length() != 32) {
            finish();
            return;
        }
        displayName = getIntent().getStringExtra(EXTRA_FRIEND_DISPLAY_NAME);
        if (displayName == null) {
            displayName = "";
        }
        host = getIntent().getStringExtra(EXTRA_HOST);
        port = getIntent().getIntExtra(EXTRA_PORT, 0);

        toolbar = findViewById(R.id.toolbar);
        toolbar.setNavigationOnClickListener(v -> finish());
        avatar = findViewById(R.id.friendDetailAvatar);
        nameView = findViewById(R.id.friendDetailName);
        userIdView = findViewById(R.id.friendDetailUserId);
        copyBtn = findViewById(R.id.friendDetailCopyId);
        chatBtn = findViewById(R.id.friendDetailChat);
        deleteBtn = findViewById(R.id.friendDetailDelete);
        progress = findViewById(R.id.friendDetailProgress);

        String initialName = ProfileDisplayHelper.effectiveDisplayName(displayName, friendHex);
        toolbar.setTitle(initialName);
        nameView.setText(initialName);
        userIdView.setText(friendHex);

        copyBtn.setOnClickListener(
                v -> {
                    ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                    if (cm != null) {
                        cm.setPrimaryClip(ClipData.newPlainText("accountId", friendHex));
                        Toast.makeText(this, R.string.friend_detail_id_copied, Toast.LENGTH_SHORT).show();
                    }
                });

        chatBtn.setOnClickListener(
                v -> {
                    if (ConversationPlaceholderStore.hasSession(this, friendHex)) {
                        if (host == null || host.isBlank() || port <= 0) {
                            Toast.makeText(this, R.string.profile_sync_no_server, Toast.LENGTH_SHORT).show();
                            return;
                        }
                        startActivity(
                                ChatActivity.buildIntent(
                                        this,
                                        host,
                                        port,
                                        friendHex,
                                        nameView.getText().toString()));
                    } else {
                        ConversationPlaceholderStore.addSession(
                                this,
                                friendHex,
                                nameView.getText().toString(),
                                friendAvatarBytes);
                        Toast.makeText(this, R.string.friend_chat_created_placeholder, Toast.LENGTH_SHORT).show();
                        refreshChatButton();
                    }
                });

        deleteBtn.setOnClickListener(v -> showDeleteFirstConfirm());

        loadProfileFromServer();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshChatButton();
    }

    private void refreshChatButton() {
        boolean has = ConversationPlaceholderStore.hasSession(this, friendHex);
        chatBtn.setText(has ? R.string.friend_chat_continue : R.string.friend_chat_start);
    }

    private void loadProfileFromServer() {
        if (host == null || host.isBlank() || port <= 0) {
            return;
        }
        progress.setVisibility(View.VISIBLE);
        io.execute(
                () -> {
                    try {
                        AuthCredentialStore creds = AuthCredentialStore.create(this);
                        if (!creds.hasCredentials()) {
                            postUi(() -> progress.setVisibility(View.GONE));
                            return;
                        }
                        ServerEndpoint ep = new ServerEndpoint(host, port);
                        byte[] uid = creds.getUserIdBytes();
                        String pw = creds.getPassword();
                        if (uid.length != 16 || pw.isEmpty()) {
                            postUi(() -> progress.setVisibility(View.GONE));
                            return;
                        }
                        if (!ZspSessionManager.get().ensureSession(ep, uid, pw)) {
                            postUi(() -> progress.setVisibility(View.GONE));
                            return;
                        }
                        byte[] fid = AuthCredentialStore.hexToBytes(friendHex);
                        if (fid.length != 16) {
                            postUi(() -> progress.setVisibility(View.GONE));
                            return;
                        }
                        ZspProfileCodec.UserProfile p = ZspSessionManager.get().friendInfoGet(fid);
                        if (p != null
                                && p.nicknameUtf8.isEmpty()
                                && (p.avatarBytes == null || p.avatarBytes.length == 0)) {
                            ZspProfileCodec.UserProfile alt = ZspSessionManager.get().userProfileGet(fid);
                            if (alt != null
                                    && (!alt.nicknameUtf8.isEmpty()
                                            || (alt.avatarBytes != null && alt.avatarBytes.length > 0))) {
                                p = alt;
                            }
                        }
                        String nick =
                                ProfileDisplayHelper.effectiveDisplayName(
                                        p != null ? p.nicknameUtf8 : "", friendHex);
                        byte[] av = p != null ? p.avatarBytes : null;
                        if (av == null || av.length == 0) {
                            av = ZspSessionManager.get().userAvatarGet(fid);
                        } else {
                            av = Arrays.copyOf(av, av.length);
                        }
                        byte[] avFinal = av;
                        postUi(
                                () -> {
                                    progress.setVisibility(View.GONE);
                                    toolbar.setTitle(nick);
                                    nameView.setText(nick);
                                    friendAvatarBytes =
                                            avFinal != null && avFinal.length > 0
                                                    ? Arrays.copyOf(avFinal, avFinal.length)
                                                    : null;
                                    ConversationPlaceholderStore.syncAvatarIfSessionExists(
                                            FriendDetailActivity.this, friendHex, friendAvatarBytes);
                                    if (avFinal != null && avFinal.length > 0) {
                                        android.graphics.Bitmap bmp =
                                                FriendsListAdapter.decodeContactAvatar(avFinal);
                                        if (bmp != null) {
                                            avatar.setImageTintList(null);
                                            avatar.setImageBitmap(bmp);
                                        }
                                    }
                                });
                    } catch (RuntimeException e) {
                        postUi(() -> progress.setVisibility(View.GONE));
                    }
                });
    }

    private void showDeleteFirstConfirm() {
        String name = nameView.getText().toString();
        new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.friend_delete_confirm_title)
                .setMessage(getString(R.string.friend_delete_confirm_message, name))
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(
                        R.string.friend_delete_next_step,
                        (d, w) -> showDeleteSecondConfirm())
                .show();
    }

    private void showDeleteSecondConfirm() {
        new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.friend_delete_confirm_final_title)
                .setMessage(R.string.friend_delete_confirm_final_message)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(
                        R.string.friend_delete_confirm_action,
                        (d, w) -> io.execute(this::runDeleteFriend))
                .show();
    }

    private void runDeleteFriend() {
        postUi(
                () -> {
                    progress.setVisibility(View.VISIBLE);
                    deleteBtn.setEnabled(false);
                });
        try {
            if (!FriendZspHelper.ensureSession(this, host, port)) {
                postUi(this::toastDeleteFailed);
                return;
            }
            if (!FriendZspHelper.publishIdentityEd25519(this)) {
                postUi(this::toastDeleteFailed);
                return;
            }
            AuthCredentialStore creds = AuthCredentialStore.create(this);
            byte[] self = creds.getUserIdBytes();
            byte[] friend = AuthCredentialStore.hexToBytes(friendHex);
            if (self.length != 16 || friend.length != 16) {
                postUi(this::toastDeleteFailed);
                return;
            }
            FriendIdentityStore ids = FriendIdentityStore.create(this);
            Ed25519PrivateKeyParameters sk = ids.getOrCreatePrivateKey(creds.getUserIdHex());
            long ts = System.currentTimeMillis() / 1000L;
            byte[] sig = FriendSigning.signDeleteFriend(sk, self, friend, ts);
            byte[] wire = FriendSigning.buildDeleteFriendWire(self, friend, ts, sig);
            boolean ok = ZspSessionManager.get().friendDelete(wire);
            if (!ok) {
                postUi(this::toastDeleteFailed);
                return;
            }
            ContactsCache.clear();
            new ChatMessageDb(this).deleteMessagesForPeer(friendHex);
            ConversationPlaceholderStore.removeSession(this, friendHex);
            postUi(
                    () -> {
                        Toast.makeText(this, R.string.friend_delete_success, Toast.LENGTH_SHORT).show();
                        Intent data = new Intent();
                        data.putExtra(EXTRA_FRIEND_DELETED, true);
                        setResult(RESULT_OK, data);
                        finish();
                    });
        } finally {
            postUi(
                    () -> {
                        progress.setVisibility(View.GONE);
                        deleteBtn.setEnabled(true);
                    });
        }
    }

    private void toastDeleteFailed() {
        Toast.makeText(this, R.string.friend_delete_failed, Toast.LENGTH_SHORT).show();
    }

    private void postUi(Runnable r) {
        runOnUiThread(
                () -> {
                    if (isFinishing()) {
                        return;
                    }
                    r.run();
                });
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        io.shutdownNow();
    }
}
