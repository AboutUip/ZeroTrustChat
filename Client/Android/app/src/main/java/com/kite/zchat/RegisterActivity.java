package com.kite.zchat;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.ImageView;
import android.widget.ProgressBar;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.textfield.TextInputEditText;
import com.google.android.material.textfield.TextInputLayout;
import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.profile.LocalProfileStore;
import com.kite.zchat.ui.ClipboardHelper;
import com.kite.zchat.zsp.ZspLocalAuthClient;
import com.kite.zchat.zsp.ZspProtocolConstants;
import com.kite.zchat.zsp.ZspSessionManager;
import com.kite.zchat.ui.NetworkErrorMessages;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class RegisterActivity extends AppCompatActivity {

    public static final String EXTRA_HOST = LoginActivity.EXTRA_HOST;
    public static final String EXTRA_PORT = LoginActivity.EXTRA_PORT;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final SecureRandom random = new SecureRandom();

    private final ActivityResultLauncher<String> pickAvatar =
            registerForActivityResult(new ActivityResultContracts.GetContent(), this::onAvatarPicked);

    private ServerEndpoint endpoint;
    private byte[] userId16;
    private Uri pendingAvatarUri;
    private ImageView registerAvatarPreview;
    private TextInputEditText inputUserIdHex;
    private TextInputEditText inputRegisterNickname;
    private TextInputEditText inputPassword;
    private TextInputEditText inputPasswordConfirm;
    private TextInputEditText inputRecovery;
    private TextInputLayout layoutRegisterNickname;
    private TextInputLayout layoutPassword;
    private TextInputLayout layoutPasswordConfirm;
    private TextInputLayout layoutRecovery;
    private MaterialButton buttonRegister;
    private ProgressBar progressRegister;

    public static void start(Context context, ServerEndpoint endpoint) {
        Intent i = new Intent(context, RegisterActivity.class);
        i.putExtra(EXTRA_HOST, endpoint.host());
        i.putExtra(EXTRA_PORT, endpoint.port());
        context.startActivity(i);
    }

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_register);

        String host = getIntent().getStringExtra(EXTRA_HOST);
        int port = getIntent().getIntExtra(EXTRA_PORT, 0);
        if (host == null || host.isBlank() || port <= 0) {
            finish();
            return;
        }
        endpoint = new ServerEndpoint(host, port);

        userId16 = new byte[16];
        random.nextBytes(userId16);

        inputUserIdHex = findViewById(R.id.inputUserIdHex);
        inputRegisterNickname = findViewById(R.id.inputRegisterNickname);
        layoutRegisterNickname = findViewById(R.id.layoutRegisterNickname);
        inputPassword = findViewById(R.id.inputPassword);
        inputPasswordConfirm = findViewById(R.id.inputPasswordConfirm);
        inputRecovery = findViewById(R.id.inputRecovery);
        layoutPassword = findViewById(R.id.layoutPassword);
        layoutPasswordConfirm = findViewById(R.id.layoutPasswordConfirm);
        layoutRecovery = findViewById(R.id.layoutRecovery);
        buttonRegister = findViewById(R.id.buttonRegister);
        progressRegister = findViewById(R.id.progressRegister);
        registerAvatarPreview = findViewById(R.id.registerAvatarPreview);
        MaterialButton pickAvatarBtn = findViewById(R.id.buttonPickAvatar);
        MaterialButton clearAvatarBtn = findViewById(R.id.buttonClearAvatar);

        inputUserIdHex.setText(AuthCredentialStore.bytesToHex(userId16));

        pickAvatarBtn.setOnClickListener(v -> pickAvatar.launch("image/*"));
        clearAvatarBtn.setOnClickListener(v -> showDefaultAvatarPreview());

        findViewById(R.id.buttonGoLogin).setOnClickListener(v -> finish());

        findViewById(R.id.buttonChangeServer).setOnClickListener(v -> MainActivity.startEditServer(this));

        findViewById(R.id.buttonCopyUserId)
                .setOnClickListener(
                        v ->
                                ClipboardHelper.copyPlainText(
                                        this,
                                        getString(R.string.copy_account_id),
                                        inputUserIdHex.getText() != null ? inputUserIdHex.getText().toString() : ""));

        buttonRegister.setOnClickListener(v -> onRegisterClicked());
    }

    private void onAvatarPicked(@Nullable Uri uri) {
        if (uri == null) {
            return;
        }
        pendingAvatarUri = uri;
        LocalProfileStore.loadPreviewFromUri(registerAvatarPreview, uri, R.drawable.ic_avatar_default);
    }

    private void showDefaultAvatarPreview() {
        pendingAvatarUri = null;
        registerAvatarPreview.setImageResource(R.drawable.ic_avatar_default);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        executor.shutdownNow();
    }

    private void onRegisterClicked() {
        String password = field(inputPassword);
        String confirm = field(inputPasswordConfirm);
        String recovery = field(inputRecovery);
        String nickRaw = field(inputRegisterNickname).trim();

        layoutRegisterNickname.setError(null);
        layoutPassword.setError(null);
        layoutPasswordConfirm.setError(null);
        layoutRecovery.setError(null);

        if (!nickRaw.isEmpty()) {
            byte[] nb = nickRaw.getBytes(StandardCharsets.UTF_8);
            if (nb.length > ZspProtocolConstants.MM1_USER_DISPLAY_NAME_MAX_BYTES) {
                layoutRegisterNickname.setError(getString(R.string.profile_nickname_too_long));
                return;
            }
        }

        if (password.length() < 8 || password.length() > 512) {
            layoutPassword.setError(getString(R.string.error_password_range));
            return;
        }
        if (!password.equals(confirm)) {
            layoutPasswordConfirm.setError(getString(R.string.error_password_mismatch));
            return;
        }
        if (recovery.length() < 32 || recovery.length() > 8192) {
            layoutRecovery.setError(getString(R.string.error_recovery_range));
            return;
        }

        buttonRegister.setEnabled(false);
        progressRegister.setVisibility(View.VISIBLE);

        executor.execute(
                () -> {
                    try {
                        boolean ok = ZspLocalAuthClient.registerLocalUser(userId16, password, recovery, endpoint);
                        if (!ok) {
                            runOnUiThread(
                                    () -> {
                                        progressRegister.setVisibility(View.GONE);
                                        buttonRegister.setEnabled(true);
                                        layoutPassword.setError(getString(R.string.error_register_failed));
                                    });
                            return;
                        }
                        ZspSessionManager.get().close();
                        boolean sessionOk = ZspSessionManager.get().establishSession(endpoint, userId16, password);
                        if (!sessionOk) {
                            ZspSessionManager.get().close();
                            runOnUiThread(
                                    () -> {
                                        progressRegister.setVisibility(View.GONE);
                                        buttonRegister.setEnabled(true);
                                        layoutPassword.setError(getString(R.string.error_login_after_register));
                                    });
                            return;
                        }
                        String hex = AuthCredentialStore.bytesToHex(userId16);
                        String nickToStore = nickRaw.isEmpty() ? hex : nickRaw;
                        ZspSessionManager.get().userDisplayNameSet(nickToStore);

                        try {
                            if (pendingAvatarUri != null) {
                                LocalProfileStore.saveAvatarFromUri(RegisterActivity.this, pendingAvatarUri, hex);
                            } else {
                                LocalProfileStore.clearAvatar(RegisterActivity.this, hex);
                            }
                        } catch (IOException ignored) {
                            // optional
                        }
                        if (LocalProfileStore.hasCustomAvatar(RegisterActivity.this, hex)) {
                            byte[] av = LocalProfileStore.readAvatarFileBytes(RegisterActivity.this, hex);
                            if (av != null && av.length > 0) {
                                ZspSessionManager.get().userAvatarSet(av);
                            }
                        }
                        AuthCredentialStore.create(RegisterActivity.this)
                                .saveCredentials(RegisterActivity.this, hex, password);
                        runOnUiThread(
                                () -> {
                                    progressRegister.setVisibility(View.GONE);
                                    buttonRegister.setEnabled(true);
                                    MainPlaceholderActivity.start(RegisterActivity.this, endpoint);
                                    finish();
                                });
                    } catch (IOException e) {
                        runOnUiThread(
                                () -> {
                                    progressRegister.setVisibility(View.GONE);
                                    buttonRegister.setEnabled(true);
                                    layoutPassword.setError(NetworkErrorMessages.fromIOException(RegisterActivity.this, e));
                                });
                    }
                });
    }

    private static String field(TextInputEditText e) {
        return e.getText() != null ? e.getText().toString() : "";
    }
}
