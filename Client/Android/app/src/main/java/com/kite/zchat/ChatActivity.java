package com.kite.zchat;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.OnBackPressedCallback;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsControllerCompat;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.call.VoiceCallCoordinator;
import com.kite.zchat.chat.ChatEvents;
import com.kite.zchat.chat.ChatActivePeer;
import com.kite.zchat.chat.ChatEmojiGridAdapter;
import com.kite.zchat.chat.ChatMessageAdapter;
import com.kite.zchat.chat.ChatMessageDb;
import com.kite.zchat.chat.ChatReplyCodec;
import com.kite.zchat.chat.ChatSync;
import com.kite.zchat.chat.PeerImSession;
import com.kite.zchat.conversation.ConversationPlaceholderStore;
import com.kite.zchat.friends.FriendZspHelper;
import com.kite.zchat.profile.LocalProfileStore;
import com.kite.zchat.profile.ProfileDisplayHelper;
import com.kite.zchat.zsp.ZspProfileCodec;
import com.kite.zchat.zsp.ZspSessionManager;
import com.kite.zchat.zsp.ZspChatWire;

import java.io.File;
import java.lang.ref.WeakReference;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class ChatActivity extends AppCompatActivity {

    public static final String EXTRA_HOST = "host";
    public static final String EXTRA_PORT = "port";
    public static final String EXTRA_PEER_USER_ID_HEX = "peer_user_id_hex";
    public static final String EXTRA_PEER_DISPLAY_NAME = "peer_display_name";

    /** 当前会话增量 SYNC；与主界面轮询配合，缩短间隔以降低聊天内滞后感。 */
    private static final long POLL_MS = 2_000L;

    @Nullable private static WeakReference<ChatActivity> activeChatRef;

    private final ActivityResultLauncher<Intent> chatSettingsLauncher =
            registerForActivityResult(
                    new ActivityResultContracts.StartActivityForResult(),
                    result -> {
                        if (result.getResultCode() == RESULT_OK) {
                            reloadFromDb();
                        }
                    });

    private final ActivityResultLauncher<Intent> friendDetailLauncher =
            registerForActivityResult(
                    new ActivityResultContracts.StartActivityForResult(),
                    result -> {
                        if (result.getResultCode() != RESULT_OK || result.getData() == null) {
                            return;
                        }
                        if (result.getData()
                                .getBooleanExtra(FriendDetailActivity.EXTRA_FRIEND_DELETED, false)) {
                            navigateToCommunicationTabAndFinish();
                        }
                    });

    public static Intent buildIntent(
            Context context,
            String host,
            int port,
            String peerHex32,
            String displayName) {
        Intent i = new Intent(context, ChatActivity.class);
        i.putExtra(EXTRA_HOST, host);
        i.putExtra(EXTRA_PORT, port);
        i.putExtra(EXTRA_PEER_USER_ID_HEX, peerHex32);
        i.putExtra(EXTRA_PEER_DISPLAY_NAME, displayName != null ? displayName : "");
        return i;
    }

    public static void notifyMessagesChangedIfShowing(String peerHex32) {
        if (peerHex32 == null) {
            return;
        }
        WeakReference<ChatActivity> ref = activeChatRef;
        ChatActivity a = ref != null ? ref.get() : null;
        if (a != null && peerHex32.equalsIgnoreCase(a.peerHex)) {
            a.runOnUiThread(a::reloadFromDb);
        }
    }

    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());
    private final Runnable pollRunnable =
            new Runnable() {
                @Override
                public void run() {
                    runSyncPoll();
                    main.postDelayed(this, POLL_MS);
                }
            };

    private String peerHex;
    private String host;
    private int port;
    private String peerDisplayName;

    private ChatMessageDb messageDb;
    private ChatMessageAdapter adapter;
    private RecyclerView recycler;
    private MaterialToolbar toolbar;
    private EditText input;
    private ImageButton sendBtn;
    private RecyclerView emojiPanel;
    private boolean emojiPanelVisible;
    private View replyBar;
    private TextView replyBarText;
    @Nullable private String replyToMsgId;
    @Nullable private String replyToPreview;
    private final OnBackPressedCallback navBackCallback =
            new OnBackPressedCallback(true) {
                @Override
                public void handleOnBackPressed() {
                    if (replyToMsgId != null
                            && replyBar != null
                            && replyBar.getVisibility() == View.VISIBLE) {
                        clearReplyDraft();
                        return;
                    }
                    if (emojiPanelVisible) {
                        hideEmojiPanelShowKeyboard();
                        return;
                    }
                    finish();
                }
            };
    private BroadcastReceiver refreshReceiver;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getOnBackPressedDispatcher().addCallback(this, navBackCallback);
        setContentView(R.layout.activity_chat);

        peerHex = getIntent().getStringExtra(EXTRA_PEER_USER_ID_HEX);
        if (peerHex == null || peerHex.length() != 32) {
            finish();
            return;
        }
        peerHex = peerHex.trim().toLowerCase(Locale.ROOT);
        applyChatNavigationBarAppearance();

        host = getIntent().getStringExtra(EXTRA_HOST);
        port = getIntent().getIntExtra(EXTRA_PORT, 0);
        peerDisplayName = getIntent().getStringExtra(EXTRA_PEER_DISPLAY_NAME);
        if (peerDisplayName == null) {
            peerDisplayName = "";
        }

        toolbar = findViewById(R.id.chatToolbar);
        toolbar.setNavigationOnClickListener(v -> handleNavigateBack());
        toolbar.inflateMenu(R.menu.chat_toolbar);
        toolbar.setOnMenuItemClickListener(
                item -> {
                    if (item.getItemId() == R.id.action_chat_more) {
                        Intent si =
                                ChatSettingsActivity.buildIntent(
                                        this,
                                        host,
                                        port,
                                        peerHex,
                                        toolbar.getTitle() != null
                                                ? toolbar.getTitle().toString()
                                                : peerDisplayName);
                        chatSettingsLauncher.launch(si);
                        return true;
                    }
                    return false;
                });

        messageDb = new ChatMessageDb(this);
        AuthCredentialStore creds = AuthCredentialStore.create(this);
        String selfHex = creds.getUserIdHex();
        String peerStored = ConversationPlaceholderStore.getPeerDisplayNameStored(this, peerHex);
        String peerNickMerged = mergePeerNickname(peerDisplayName, peerStored);
        toolbar.setTitle(ProfileDisplayHelper.chatBubblePeerName(this, peerNickMerged));

        File selfAvatar = LocalProfileStore.avatarFileForUserId(this, selfHex);
        File peerAvatar = ConversationPlaceholderStore.avatarFile(this, peerHex);

        adapter =
                new ChatMessageAdapter(
                        ProfileDisplayHelper.chatBubbleSelfName(this, null),
                        ProfileDisplayHelper.chatBubblePeerName(this, peerNickMerged),
                        selfAvatar,
                        peerAvatar);
        adapter.setOnPeerAvatarClickListener(
                () -> {
                    if (host == null || host.isBlank() || port <= 0) {
                        Toast.makeText(this, R.string.profile_sync_no_server, Toast.LENGTH_SHORT)
                                .show();
                        return;
                    }
                    CharSequence title = toolbar.getTitle();
                    String name =
                            title != null && title.length() > 0
                                    ? title.toString()
                                    : peerDisplayName;
                    friendDetailLauncher.launch(
                            FriendDetailActivity.buildIntent(this, host, port, peerHex, name));
                });
        adapter.setOnReplyToMessageListener(this::beginReplyTo);
        replyBar = findViewById(R.id.chatReplyBar);
        replyBarText = findViewById(R.id.chatReplyBarText);
        View replyDismiss = findViewById(R.id.chatReplyBarDismiss);
        if (replyDismiss != null) {
            replyDismiss.setOnClickListener(v -> clearReplyDraft());
        }
        recycler = findViewById(R.id.chatRecycler);
        LinearLayoutManager lm = new LinearLayoutManager(this);
        lm.setStackFromEnd(true);
        recycler.setLayoutManager(lm);
        recycler.setAdapter(adapter);

        input = findViewById(R.id.chatInput);
        sendBtn = findViewById(R.id.chatSend);
        emojiPanel = findViewById(R.id.chatEmojiPanel);
        ImageButton emojiToggle = findViewById(R.id.chatEmojiToggle);
        ImageButton voiceCallBtn = findViewById(R.id.chatVoiceCall);
        voiceCallBtn.setOnClickListener(v -> showVoiceCallConfirmDialog());

        emojiPanel.setLayoutManager(new GridLayoutManager(this, 8));
        emojiPanel.setAdapter(
                new ChatEmojiGridAdapter(
                        emoji -> {
                            input.append(emoji);
                            updateSendButtonState();
                        }));

        emojiToggle.setOnClickListener(v -> onEmojiToggleClicked());
        sendBtn.setOnClickListener(v -> trySend());
        input.addTextChangedListener(
                new TextWatcher() {
                    @Override
                    public void beforeTextChanged(
                            CharSequence s, int start, int count, int after) {}

                    @Override
                    public void onTextChanged(CharSequence s, int start, int before, int count) {}

                    @Override
                    public void afterTextChanged(Editable s) {
                        updateSendButtonState();
                    }
                });
        input.setOnFocusChangeListener(
                (v, hasFocus) -> {
                    if (hasFocus && emojiPanelVisible) {
                        applyEmojiPanelVisibility(false);
                    }
                });
        recycler.setOnTouchListener(
                (v, e) -> {
                    if (e.getActionMasked() == MotionEvent.ACTION_DOWN) {
                        hideEmojiPanelAndIme();
                    }
                    return false;
                });
        updateSendButtonState();

        loadChatDisplayNamesAsync(peerNickMerged);

        reloadFromDb();

        refreshReceiver =
                new BroadcastReceiver() {
                    @Override
                    public void onReceive(Context context, Intent intent) {
                        if (intent == null) {
                            return;
                        }
                        String p = intent.getStringExtra(ChatEvents.EXTRA_PEER_HEX);
                        if (p != null && p.equalsIgnoreCase(peerHex)) {
                            reloadFromDb();
                        }
                    }
                };
        ContextCompat.registerReceiver(
                this,
                refreshReceiver,
                new IntentFilter(ChatEvents.ACTION_CHAT_MESSAGES_CHANGED),
                ContextCompat.RECEIVER_NOT_EXPORTED);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!ConversationPlaceholderStore.hasSession(this, peerHex)) {
            navigateToCommunicationTabAndFinish();
            return;
        }
        activeChatRef = new WeakReference<>(this);
        ChatActivePeer.setActivePeerHex(peerHex);
        ConversationPlaceholderStore.clearUnread(this, peerHex);
        main.post(pollRunnable);
        io.execute(this::runInitialSync);
    }

    private void navigateToCommunicationTabAndFinish() {
        Intent i = new Intent(this, MainPlaceholderActivity.class);
        if (host != null) {
            i.putExtra(MainPlaceholderActivity.EXTRA_HOST, host);
        }
        i.putExtra(MainPlaceholderActivity.EXTRA_PORT, port);
        i.putExtra(MainPlaceholderActivity.EXTRA_OPEN_COMMUNICATION_TAB, true);
        i.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(i);
        finish();
    }

    @Override
    protected void onPause() {
        super.onPause();
        main.removeCallbacks(pollRunnable);
        WeakReference<ChatActivity> ref = activeChatRef;
        if (ref != null && ref.get() == this) {
            activeChatRef = null;
        }
        ChatActivePeer.setActivePeerHex(null);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (refreshReceiver != null) {
            try {
                unregisterReceiver(refreshReceiver);
            } catch (IllegalArgumentException ignored) {
            }
        }
        io.shutdownNow();
    }

    private static String mergePeerNickname(
            @Nullable String fromIntent, @Nullable String fromStore) {
        String a = fromIntent != null ? fromIntent.trim() : "";
        if (!a.isEmpty()) {
            return a;
        }
        if (fromStore != null) {
            String t = fromStore.trim();
            if (!t.isEmpty()) {
                return t;
            }
        }
        return "";
    }

    /** 导航栏与输入条 {@link com.kite.zchat.R.color#chat_input_bar_bg} 同色，减轻浅色模式下与手势条割裂感。 */
    private void applyChatNavigationBarAppearance() {
        Window w = getWindow();
        WindowCompat.setDecorFitsSystemWindows(w, true);
        int navColor = ContextCompat.getColor(this, com.kite.zchat.R.color.chat_input_bar_bg);
        w.setNavigationBarColor(navColor);
        boolean night =
                (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)
                        == Configuration.UI_MODE_NIGHT_YES;
        WindowInsetsControllerCompat c = new WindowInsetsControllerCompat(w, w.getDecorView());
        c.setAppearanceLightNavigationBars(!night);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            w.setNavigationBarContrastEnforced(false);
        }
    }

    private void handleNavigateBack() {
        if (replyToMsgId != null && replyBar != null && replyBar.getVisibility() == View.VISIBLE) {
            clearReplyDraft();
            return;
        }
        if (emojiPanelVisible) {
            hideEmojiPanelShowKeyboard();
            return;
        }
        finish();
    }

    private void clearReplyDraft() {
        replyToMsgId = null;
        replyToPreview = null;
        if (replyBar != null) {
            replyBar.setVisibility(View.GONE);
        }
    }

    private void beginReplyTo(ChatMessageDb.Row row) {
        if (row.msgIdHex.length() != 32) {
            Toast.makeText(this, R.string.chat_reply_need_msg_id, Toast.LENGTH_SHORT).show();
            return;
        }
        hideEmojiPanelAndIme();
        replyToMsgId = row.msgIdHex;
        String s = ChatReplyCodec.stripForPreview(row.text).replace('\n', ' ').trim();
        if (s.length() > 120) {
            s = s.substring(0, 117) + "…";
        }
        replyToPreview = s;
        if (replyBarText != null) {
            replyBarText.setText(getString(R.string.chat_reply_bar_format, s));
        }
        if (replyBar != null) {
            replyBar.setVisibility(View.VISIBLE);
        }
        input.requestFocus();
        showSoftInputForInput();
    }

    private void updateSendButtonState() {
        CharSequence cs = input.getText();
        boolean has = cs != null && cs.toString().trim().length() > 0;
        sendBtn.setEnabled(has);
        sendBtn.setAlpha(has ? 1f : 0.38f);
    }

    private void applyEmojiPanelVisibility(boolean show) {
        if (emojiPanelVisible == show) {
            return;
        }
        emojiPanelVisible = show;
        emojiPanel.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    /** 显示表情面板：收起系统键盘（与微信/Telegram 等一致，表情与键盘二选一）。 */
    private void showEmojiPanelOverKeyboard() {
        InputMethodManager imm =
                (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (imm != null) {
            imm.hideSoftInputFromWindow(input.getWindowToken(), 0);
        }
        input.clearFocus();
        applyEmojiPanelVisibility(true);
    }

    /** 收起表情面板并弹出键盘（再次点表情按钮、或按返回）。 */
    private void hideEmojiPanelShowKeyboard() {
        if (!emojiPanelVisible) {
            input.requestFocus();
            showSoftInputForInput();
            return;
        }
        applyEmojiPanelVisibility(false);
        input.requestFocus();
        showSoftInputForInput();
    }

    /** 收起表情与键盘（点击消息区域、滚动列表时）。 */
    private void hideEmojiPanelAndIme() {
        if (emojiPanelVisible) {
            applyEmojiPanelVisibility(false);
        }
        InputMethodManager imm =
                (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (imm != null) {
            imm.hideSoftInputFromWindow(input.getWindowToken(), 0);
        }
        input.clearFocus();
    }

    private void showSoftInputForInput() {
        InputMethodManager imm =
                (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (imm == null) {
            return;
        }
        input.post(() -> imm.showSoftInput(input, InputMethodManager.SHOW_IMPLICIT));
    }

    /** 表情按钮：在「表情面板」与「系统键盘」之间切换。 */
    private void onEmojiToggleClicked() {
        if (emojiPanelVisible) {
            hideEmojiPanelShowKeyboard();
        } else {
            showEmojiPanelOverKeyboard();
        }
    }

    private void showVoiceCallConfirmDialog() {
        if (host == null || host.isBlank() || port <= 0) {
            Toast.makeText(this, R.string.profile_sync_no_server, Toast.LENGTH_SHORT).show();
            return;
        }
        if (!VoiceCallCoordinator.canStartOutgoingCall()) {
            Toast.makeText(this, R.string.voice_call_block_other, Toast.LENGTH_SHORT).show();
            return;
        }
        new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.voice_call_confirm_title)
                .setMessage(R.string.voice_call_confirm_message)
                .setNegativeButton(R.string.voice_call_confirm_negative, (d, w) -> d.dismiss())
                .setPositiveButton(
                        R.string.voice_call_confirm_positive,
                        (d, w) ->
                                startActivity(
                                        VoiceCallActivity.buildOutgoingIntent(
                                                ChatActivity.this, host, port, peerHex)))
                .show();
    }

    private void loadChatDisplayNamesAsync(String peerNickMerged) {
        if (host == null || host.isBlank() || port <= 0) {
            return;
        }
        io.execute(
                () -> {
                    if (!FriendZspHelper.ensureSession(this, host, port)) {
                        return;
                    }
                    AuthCredentialStore creds = AuthCredentialStore.create(this);
                    byte[] self = creds.getUserIdBytes();
                    byte[] peer = AuthCredentialStore.hexToBytes(peerHex);
                    if (self.length != 16 || peer.length != 16) {
                        return;
                    }
                    String selfNick = "";
                    ZspProfileCodec.UserProfile sp = ZspSessionManager.get().userProfileGet(self);
                    if (sp != null) {
                        selfNick = sp.nicknameUtf8;
                    }
                    String peerNick = peerNickMerged;
                    ZspProfileCodec.UserProfile fp = ZspSessionManager.get().friendInfoGet(peer);
                    if (fp == null
                            || fp.nicknameUtf8 == null
                            || fp.nicknameUtf8.trim().isEmpty()) {
                        fp = ZspSessionManager.get().userProfileGet(peer);
                    }
                    if (fp != null && fp.nicknameUtf8 != null && !fp.nicknameUtf8.trim().isEmpty()) {
                        peerNick = fp.nicknameUtf8.trim();
                    }
                    final String selfFinal =
                            ProfileDisplayHelper.chatBubbleSelfName(ChatActivity.this, selfNick);
                    final String peerFinal =
                            ProfileDisplayHelper.chatBubblePeerName(ChatActivity.this, peerNick);
                    runOnUiThread(
                            () -> {
                                adapter.setDisplayNames(selfFinal, peerFinal);
                                toolbar.setTitle(peerFinal);
                            });
                });
    }

    private void runInitialSync() {
        if (host == null || host.isBlank() || port <= 0) {
            return;
        }
        ChatSync.syncPeer(getApplicationContext(), host, port, peerHex, true);
        runOnUiThread(this::reloadFromDb);
    }

    private void runSyncPoll() {
        if (host == null || host.isBlank() || port <= 0) {
            return;
        }
        io.execute(
                () -> {
                    ChatSync.syncPeer(getApplicationContext(), host, port, peerHex, false);
                    runOnUiThread(this::reloadFromDb);
                });
    }

    private void reloadFromDb() {
        List<ChatMessageDb.Row> rows = messageDb.listForPeer(peerHex);
        adapter.setItems(rows);
        if (!rows.isEmpty()) {
            recycler.scrollToPosition(rows.size() - 1);
        }
    }

    private void trySend() {
        if (emojiPanelVisible) {
            applyEmojiPanelVisibility(false);
        }
        CharSequence cs = input.getText();
        String text = cs != null ? cs.toString().trim() : "";
        if (TextUtils.isEmpty(text)) {
            return;
        }
        if (host == null || host.isBlank() || port <= 0) {
            Toast.makeText(this, R.string.profile_sync_no_server, Toast.LENGTH_SHORT).show();
            return;
        }
        final boolean wasReply = replyToMsgId != null && replyToMsgId.length() == 32;
        final String wireToSend =
                wasReply
                        ? ChatReplyCodec.encode(
                                replyToMsgId,
                                replyToPreview != null ? replyToPreview : "",
                                text)
                        : text;
        input.setEnabled(false);
        io.execute(
                () -> {
                    AuthCredentialStore creds = AuthCredentialStore.create(this);
                    byte[] self = creds.getUserIdBytes();
                    byte[] peer = AuthCredentialStore.hexToBytes(peerHex);
                    if (self.length != 16 || peer.length != 16) {
                        postUiToast(R.string.error_user_id_hex);
                        return;
                    }
                    if (!FriendZspHelper.ensureSession(this, host, port)) {
                        postUiToast(R.string.contacts_load_failed);
                        return;
                    }
                    byte[] im = PeerImSession.deriveSessionId(self, peer);
                    ZspChatWire.TextSendResult r =
                            ZspSessionManager.get().sendTextMessage(im, peer, wireToSend);
                    if (!r.ok || r.messageId16 == null || r.messageId16.length != 16) {
                        postUiToast(R.string.chat_send_failed);
                        return;
                    }
                    String msgHex = AuthCredentialStore.bytesToHex(r.messageId16);
                    long ts = System.currentTimeMillis();
                    messageDb.insertIfAbsent(peerHex, msgHex, wireToSend, true, ts);
                    String previewRaw = ChatReplyCodec.stripForPreview(wireToSend);
                    String preview =
                            previewRaw.length() > 80
                                    ? previewRaw.substring(0, 80) + "…"
                                    : previewRaw;
                    ConversationPlaceholderStore.updatePreviewAndTime(this, peerHex, preview, ts);
                    runOnUiThread(
                            () -> {
                                if (wasReply) {
                                    clearReplyDraft();
                                }
                                input.setText("");
                                input.setEnabled(true);
                                reloadFromDb();
                                input.requestFocus();
                                showSoftInputForInput();
                            });
                });
    }

    private void postUiToast(int res) {
        runOnUiThread(
                () -> {
                    Toast.makeText(this, res, Toast.LENGTH_SHORT).show();
                    input.setEnabled(true);
                });
    }

    @Override
    public void finish() {
        Intent i = new Intent(ChatEvents.ACTION_CONVERSATION_LIST_CHANGED);
        i.setPackage(getPackageName());
        sendBroadcast(i);
        super.finish();
    }
}
