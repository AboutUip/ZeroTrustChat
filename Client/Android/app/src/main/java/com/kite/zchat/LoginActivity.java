package com.kite.zchat;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.textfield.TextInputEditText;
import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.ui.ClipboardHelper;
import com.kite.zchat.zsp.ZspSessionManager;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Pattern;

public class LoginActivity extends AppCompatActivity {

    public static final String EXTRA_HOST = "host";
    public static final String EXTRA_PORT = "port";
    public static final String EXTRA_HINT = "hint";

    private static final Pattern HEX32 = Pattern.compile("(?i)[0-9a-f]{32}");

    private final ExecutorService executor = Executors.newSingleThreadExecutor();

    private ServerEndpoint endpoint;
    private TextInputEditText inputUserIdHex;
    private TextInputEditText inputPassword;
    private MaterialButton buttonLogin;
    private ProgressBar progressLogin;
    private TextView textHint;

    public static void start(Context context, ServerEndpoint endpoint, @Nullable String hintMessage) {
        Intent i = new Intent(context, LoginActivity.class);
        i.putExtra(EXTRA_HOST, endpoint.host());
        i.putExtra(EXTRA_PORT, endpoint.port());
        if (hintMessage != null) {
            i.putExtra(EXTRA_HINT, hintMessage);
        }
        context.startActivity(i);
    }

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_login);

        String host = getIntent().getStringExtra(EXTRA_HOST);
        int port = getIntent().getIntExtra(EXTRA_PORT, 0);
        if (host == null || host.isBlank() || port <= 0) {
            finish();
            return;
        }
        endpoint = new ServerEndpoint(host, port);

        inputUserIdHex = findViewById(R.id.inputUserIdHex);
        inputPassword = findViewById(R.id.inputPassword);
        buttonLogin = findViewById(R.id.buttonLogin);
        progressLogin = findViewById(R.id.progressLogin);
        textHint = findViewById(R.id.textLoginHint);

        String hint = getIntent().getStringExtra(EXTRA_HINT);
        if (hint != null && !hint.isBlank()) {
            textHint.setText(hint);
            textHint.setVisibility(View.VISIBLE);
        }

        AuthCredentialStore store = AuthCredentialStore.create(this);
        if (store.hasCredentials()) {
            inputUserIdHex.setText(store.getUserIdHex());
        }

        findViewById(R.id.buttonGoRegister).setOnClickListener(v -> RegisterActivity.start(this, endpoint));

        findViewById(R.id.buttonChangeServer).setOnClickListener(v -> MainActivity.startEditServer(this));

        findViewById(R.id.buttonCopyUserId)
                .setOnClickListener(
                        v ->
                                ClipboardHelper.copyPlainText(
                                        this,
                                        getString(R.string.copy_account_id),
                                        inputUserIdHex.getText() != null ? inputUserIdHex.getText().toString() : ""));

        buttonLogin.setOnClickListener(v -> onLoginClicked());
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        executor.shutdownNow();
    }

    private void onLoginClicked() {
        String hex = inputUserIdHex.getText() != null ? inputUserIdHex.getText().toString().trim() : "";
        String password = inputPassword.getText() != null ? inputPassword.getText().toString() : "";
        if (!HEX32.matcher(hex).matches()) {
            textHint.setText(R.string.error_user_id_hex);
            textHint.setVisibility(View.VISIBLE);
            return;
        }
        if (password.length() < 8) {
            textHint.setText(R.string.error_password_min);
            textHint.setVisibility(View.VISIBLE);
            return;
        }

        byte[] uid = AuthCredentialStore.hexToBytes(hex);
        if (uid.length != 16) {
            textHint.setText(R.string.error_user_id_hex);
            textHint.setVisibility(View.VISIBLE);
            return;
        }

        textHint.setVisibility(View.GONE);
        buttonLogin.setEnabled(false);
        progressLogin.setVisibility(View.VISIBLE);

        executor.execute(
                () -> {
                    ZspSessionManager.get().close();
                    boolean sessionOk = ZspSessionManager.get().establishSession(endpoint, uid, password);
                    runOnUiThread(
                            () -> {
                                progressLogin.setVisibility(View.GONE);
                                buttonLogin.setEnabled(true);
                                if (sessionOk) {
                                    AuthCredentialStore.create(LoginActivity.this)
                                            .saveCredentials(LoginActivity.this, hex, password);
                                    android.os.Bundle pending =
                                            com.kite.zchat.push.PendingNavigation.consume();
                                    if (pending != null && !pending.isEmpty()) {
                                        Intent main = new Intent(LoginActivity.this, MainPlaceholderActivity.class);
                                        main.putExtras(pending);
                                        main.addFlags(
                                                Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
                                        startActivity(main);
                                    } else {
                                        MainPlaceholderActivity.start(LoginActivity.this, endpoint);
                                    }
                                    finish();
                                } else {
                                    ZspSessionManager.get().close();
                                    textHint.setText(R.string.error_login_failed);
                                    textHint.setVisibility(View.VISIBLE);
                                }
                            });
                });
    }
}
