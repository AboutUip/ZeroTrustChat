package com.kite.zchat.profile;

import android.net.Uri;
import android.os.Bundle;
import android.widget.ImageView;
import android.widget.Toast;

import androidx.activity.OnBackPressedCallback;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.textfield.TextInputEditText;
import com.google.android.material.textfield.TextInputLayout;

import com.kite.zchat.R;
import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.ui.ClipboardHelper;
import com.kite.zchat.core.ServerConfigStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.zsp.ZspProfileCodec;
import com.kite.zchat.zsp.ZspSessionManager;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/** 编辑昵称与头像；返回时自动同步到服务端（需已配置并可连接服务器）。 */
public final class ProfileEditActivity extends AppCompatActivity {

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final AtomicBoolean syncInProgress = new AtomicBoolean(false);

    private final ActivityResultLauncher<String> pickImage =
            registerForActivityResult(new ActivityResultContracts.GetContent(), this::onImagePicked);

    private ImageView editAvatarPreview;
    private TextInputEditText inputNickname;
    private TextInputLayout layoutNickname;
    private String userIdHex;
    /** 用户点击了「移除头像」且尚未同步到服务端删除时置 true。 */
    private boolean avatarMarkedForServerDelete;

    private OnBackPressedCallback backCallback;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_profile_edit);

        AuthCredentialStore creds;
        try {
            creds = AuthCredentialStore.create(this);
        } catch (RuntimeException e) {
            Toast.makeText(this, R.string.profile_edit_open_failed, Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        userIdHex = creds.getUserIdHex();
        if (userIdHex.length() != 32) {
            Toast.makeText(this, R.string.profile_edit_invalid_user_id, Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.setNavigationOnClickListener(v -> syncProfileToServerThenFinish());

        backCallback =
                new OnBackPressedCallback(true) {
                    @Override
                    public void handleOnBackPressed() {
                        syncProfileToServerThenFinish();
                    }
                };
        getOnBackPressedDispatcher().addCallback(this, backCallback);

        editAvatarPreview = findViewById(R.id.editAvatarPreview);
        inputNickname = findViewById(R.id.inputNickname);
        layoutNickname = findViewById(R.id.layoutNickname);
        inputNickname.setText(userIdHex);
        TextInputEditText inputUserIdHex = findViewById(R.id.inputProfileUserIdHex);
        inputUserIdHex.setText(userIdHex);
        findViewById(R.id.buttonCopyProfileUserId)
                .setOnClickListener(
                        v ->
                                ClipboardHelper.copyPlainText(
                                        this, getString(R.string.copy_account_id), userIdHex));
        refreshAvatar();

        MaterialButton pick = findViewById(R.id.buttonEditPickAvatar);
        MaterialButton remove = findViewById(R.id.buttonEditRemoveAvatar);
        pick.setOnClickListener(v -> pickImage.launch("image/*"));
        remove.setOnClickListener(v -> onRemoveAvatarClicked());

        executor.execute(this::loadServerProfile);
    }

    @Override
    protected void onDestroy() {
        executor.shutdown();
        super.onDestroy();
    }

    private void loadServerProfile() {
        AuthCredentialStore creds;
        try {
            creds = AuthCredentialStore.create(this);
        } catch (RuntimeException e) {
            runOnUiThread(
                    () -> {
                        if (isFinishing() || isDestroyed()) {
                            return;
                        }
                        inputNickname.setText(userIdHex);
                    });
            return;
        }
        ServerEndpoint ep = new ServerConfigStore(this).getSavedEndpoint();
        if (ep == null || !creds.hasCredentials()) {
            runOnUiThread(
                    () -> {
                        if (isFinishing() || isDestroyed()) {
                            return;
                        }
                        inputNickname.setText(userIdHex);
                    });
            return;
        }
        byte[] uid = creds.getUserIdBytes();
        String pw = creds.getPassword();
        if (uid.length != 16 || pw.isEmpty()) {
            runOnUiThread(
                    () -> {
                        if (isFinishing() || isDestroyed()) {
                            return;
                        }
                        inputNickname.setText(userIdHex);
                    });
            return;
        }
        if (!ZspSessionManager.get().ensureSession(ep, uid, pw)) {
            runOnUiThread(
                    () -> {
                        if (isFinishing() || isDestroyed()) {
                            return;
                        }
                        inputNickname.setText(userIdHex);
                    });
            return;
        }
        ZspProfileCodec.UserProfile p = ZspSessionManager.get().userProfileGet(uid);
        if (p == null) {
            runOnUiThread(
                    () -> {
                        if (isFinishing() || isDestroyed()) {
                            return;
                        }
                        inputNickname.setText(userIdHex);
                    });
            return;
        }
        runOnUiThread(
                () -> {
                    if (isFinishing() || isDestroyed()) {
                        return;
                    }
                    inputNickname.setText(
                            ProfileDisplayHelper.effectiveDisplayName(p.nicknameUtf8, userIdHex));
                    if (p.avatarBytes != null && p.avatarBytes.length > 0) {
                        try {
                            LocalProfileStore.writeAvatarBytes(ProfileEditActivity.this, userIdHex, p.avatarBytes);
                            refreshAvatar();
                        } catch (IOException ignored) {
                        }
                    }
                });
    }

    private void onImagePicked(@Nullable Uri uri) {
        if (uri == null) {
            return;
        }
        try {
            avatarMarkedForServerDelete = false;
            LocalProfileStore.saveAvatarFromUri(this, uri, userIdHex);
            refreshAvatar();
            Toast.makeText(this, R.string.profile_avatar_local_saved_toast, Toast.LENGTH_SHORT).show();
        } catch (IOException e) {
            Toast.makeText(this, R.string.profile_avatar_save_failed, Toast.LENGTH_SHORT).show();
        }
    }

    private void onRemoveAvatarClicked() {
        avatarMarkedForServerDelete = true;
        LocalProfileStore.clearAvatar(this, userIdHex);
        refreshAvatar();
        Toast.makeText(this, R.string.profile_avatar_removed_toast, Toast.LENGTH_SHORT).show();
    }

    private void syncProfileToServerThenFinish() {
        if (!syncInProgress.compareAndSet(false, true)) {
            return;
        }
        if (backCallback != null) {
            backCallback.setEnabled(false);
        }

        String trimmed = inputNickname.getText() != null ? inputNickname.getText().toString().trim() : "";
        final String nickToSave = trimmed.isEmpty() ? userIdHex : trimmed;
        byte[] nickBytes = nickToSave.getBytes(StandardCharsets.UTF_8);
        if (nickBytes.length > com.kite.zchat.zsp.ZspProtocolConstants.MM1_USER_DISPLAY_NAME_MAX_BYTES) {
            layoutNickname.setError(getString(R.string.profile_nickname_too_long));
            syncInProgress.set(false);
            if (backCallback != null) {
                backCallback.setEnabled(true);
            }
            return;
        }
        layoutNickname.setError(null);

        executor.execute(
                () -> {
                    AuthCredentialStore creds;
                    try {
                        creds = AuthCredentialStore.create(ProfileEditActivity.this);
                    } catch (RuntimeException e) {
                        runOnUiThread(
                                () -> {
                                    Toast.makeText(
                                                    ProfileEditActivity.this,
                                                    R.string.profile_edit_open_failed,
                                                    Toast.LENGTH_SHORT)
                                            .show();
                                    finishWithResultCanceled();
                                });
                        return;
                    }
                    ServerEndpoint ep = new ServerConfigStore(ProfileEditActivity.this).getSavedEndpoint();
                    if (ep == null) {
                        runOnUiThread(
                                () -> {
                                    Toast.makeText(
                                                    ProfileEditActivity.this,
                                                    R.string.profile_sync_no_server,
                                                    Toast.LENGTH_SHORT)
                                            .show();
                                    finishWithResultOk();
                                });
                        return;
                    }
                    byte[] uid = creds.getUserIdBytes();
                    String pw = creds.getPassword();
                    if (uid.length != 16 || pw.isEmpty()) {
                        runOnUiThread(
                                () -> {
                                    Toast.makeText(
                                                    ProfileEditActivity.this,
                                                    R.string.profile_sync_failed,
                                                    Toast.LENGTH_SHORT)
                                            .show();
                                    finishWithResultOk();
                                });
                        return;
                    }
                    if (!ZspSessionManager.get().ensureSession(ep, uid, pw)) {
                        runOnUiThread(
                                () -> {
                                    Toast.makeText(
                                                    ProfileEditActivity.this,
                                                    R.string.profile_sync_failed,
                                                    Toast.LENGTH_SHORT)
                                            .show();
                                    finishWithResultOk();
                                });
                        return;
                    }
                    boolean nickOk = ZspSessionManager.get().userDisplayNameSet(nickToSave);
                    boolean avOk = true;
                    if (avatarMarkedForServerDelete) {
                        avOk = ZspSessionManager.get().userAvatarDelete();
                    } else if (LocalProfileStore.hasCustomAvatar(ProfileEditActivity.this, userIdHex)) {
                        byte[] av = LocalProfileStore.readAvatarFileBytes(ProfileEditActivity.this, userIdHex);
                        if (av != null && av.length > 0) {
                            avOk = ZspSessionManager.get().userAvatarSet(av);
                        }
                    }
                    final boolean ok = nickOk && avOk;
                    runOnUiThread(
                            () -> {
                                if (ok) {
                                    Toast.makeText(
                                                    ProfileEditActivity.this,
                                                    R.string.profile_sync_ok,
                                                    Toast.LENGTH_SHORT)
                                            .show();
                                } else {
                                    Toast.makeText(
                                                    ProfileEditActivity.this,
                                                    R.string.profile_sync_failed,
                                                    Toast.LENGTH_SHORT)
                                            .show();
                                }
                                finishWithResultOk();
                            });
                });
    }

    private void finishWithResultOk() {
        if (isFinishing()) {
            return;
        }
        setResult(RESULT_OK);
        finish();
    }

    private void finishWithResultCanceled() {
        if (isFinishing()) {
            return;
        }
        setResult(RESULT_CANCELED);
        finish();
    }

    private void refreshAvatar() {
        LocalProfileStore.loadAvatarInto(editAvatarPreview, userIdHex, R.drawable.ic_avatar_default);
    }
}
