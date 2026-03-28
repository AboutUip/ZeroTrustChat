package com.kite.zchat;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.core.ServerConfigStore;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.core.ZspConnectivityChecker;
import com.kite.zchat.zsp.ZspSessionManager;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends AppCompatActivity {

    public static final String EXTRA_EDIT_SERVER = "edit_server";

    private static final int DEFAULT_ZSP_PORT = 8848;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private ServerConfigStore configStore;
    private ZspConnectivityChecker connectivityChecker;

    private View checkingCard;
    private View connectionCard;
    private View readyCard;
    private ProgressBar checkingProgress;
    private ProgressBar connectProgress;
    private ProgressBar progressReady;
    private TextView checkingStatus;
    private TextView connectStatus;
    private TextView readySummary;
    private EditText hostInput;
    private EditText portInput;
    private Button checkButton;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        configStore = new ServerConfigStore(this);
        connectivityChecker = new ZspConnectivityChecker(5000, 30000);

        bindViews();
        if (getIntent().getBooleanExtra(EXTRA_EDIT_SERVER, false)) {
            showEditServerForm();
        } else {
            startBootstrapFlow();
        }
    }

    /** 从登录/注册/主界面进入，仅展示服务器表单并预填已保存地址。 */
    public static void startEditServer(Context context) {
        Intent i = new Intent(context, MainActivity.class);
        i.putExtra(EXTRA_EDIT_SERVER, true);
        if (!(context instanceof Activity)) {
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        }
        context.startActivity(i);
        if (context instanceof Activity) {
            ((Activity) context).finish();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        executor.shutdownNow();
    }

    private void bindViews() {
        checkingCard = findViewById(R.id.cardChecking);
        connectionCard = findViewById(R.id.cardConnection);
        readyCard = findViewById(R.id.cardReady);

        checkingProgress = findViewById(R.id.progressChecking);
        connectProgress = findViewById(R.id.progressConnect);
        progressReady = findViewById(R.id.progressReady);
        checkingStatus = findViewById(R.id.textCheckingStatus);
        connectStatus = findViewById(R.id.textConnectStatus);
        readySummary = findViewById(R.id.textReadySummary);

        hostInput = findViewById(R.id.inputHost);
        portInput = findViewById(R.id.inputPort);
        checkButton = findViewById(R.id.buttonCheckAndSave);

        checkButton.setOnClickListener(v -> onCheckAndSaveClicked());
    }

    private void startBootstrapFlow() {
        showChecking(getString(R.string.checking_saved_server));
        ServerEndpoint saved = configStore.getSavedEndpoint();
        if (saved == null) {
            showConnectionForm(getString(R.string.server_not_configured), null);
            return;
        }
        attemptConnect(saved, true);
    }

    private void onCheckAndSaveClicked() {
        String host = hostInput.getText().toString().trim();
        String portRaw = portInput.getText().toString().trim();

        if (host.isBlank()) {
            connectStatus.setText(R.string.error_host_required);
            return;
        }
        int port;
        try {
            port = Integer.parseInt(portRaw);
        } catch (NumberFormatException ex) {
            connectStatus.setText(R.string.error_port_invalid);
            return;
        }
        if (port < 1 || port > 65535) {
            connectStatus.setText(R.string.error_port_invalid);
            return;
        }

        attemptConnect(new ServerEndpoint(host, port), false);
    }

    private void attemptConnect(ServerEndpoint endpoint, boolean fromStartupCheck) {
        if (fromStartupCheck) {
            showChecking(getString(R.string.checking_saved_server_trying, endpoint.host(), endpoint.port()));
        } else {
            connectProgress.setVisibility(View.VISIBLE);
            checkButton.setEnabled(false);
            connectStatus.setText(getString(R.string.checking_zsp_protocol));
        }

        executor.execute(
                () -> {
                    ZspConnectivityChecker.ZspProbeOutcome outcome = connectivityChecker.probeZsp(endpoint);
                    runOnUiThread(() -> onConnectivityChecked(endpoint, outcome, fromStartupCheck));
                });
    }

    private void onConnectivityChecked(
            ServerEndpoint endpoint, ZspConnectivityChecker.ZspProbeOutcome outcome, boolean fromStartupCheck) {
        if (fromStartupCheck) {
            if (outcome.result == ZspConnectivityChecker.ProbeResult.ZSP_OK) {
                proceedAfterServerReady(endpoint);
            } else if (outcome.result == ZspConnectivityChecker.ProbeResult.ZSP_HANDSHAKE_FAILED) {
                showConnectionForm(formatZspHandshakeMessage(outcome.detail), endpoint);
            } else {
                showConnectionForm(formatUnreachableMessage(outcome.detail, true), endpoint);
            }
            return;
        }

        connectProgress.setVisibility(View.GONE);
        checkButton.setEnabled(true);
        if (outcome.result == ZspConnectivityChecker.ProbeResult.ZSP_OK) {
            configStore.saveEndpoint(endpoint);
            proceedAfterServerReady(endpoint);
        } else if (outcome.result == ZspConnectivityChecker.ProbeResult.ZSP_HANDSHAKE_FAILED) {
            connectStatus.setText(formatZspHandshakeMessage(outcome.detail));
        } else {
            connectStatus.setText(formatUnreachableMessage(outcome.detail, false));
        }
    }

    private String formatZspHandshakeMessage(@Nullable String detail) {
        String base = getString(R.string.error_zsp_handshake_failed);
        if (detail == null || detail.isBlank()) {
            return base;
        }
        return base + "\n\n" + getString(R.string.error_zsp_probe_technical, detail);
    }

    private String formatUnreachableMessage(@Nullable String detail, boolean fromStartupCheck) {
        int baseRes =
                fromStartupCheck ? R.string.saved_server_unreachable : R.string.error_cannot_connect;
        String base = getString(baseRes);
        if (detail == null || detail.isBlank()) {
            return base;
        }
        return base + "\n\n" + getString(R.string.error_zsp_probe_technical, detail);
    }

    /** 服务器可达后：有已保存口令则尝试自动登录；否则进入登录页。 */
    private void proceedAfterServerReady(ServerEndpoint endpoint) {
        AuthCredentialStore store = AuthCredentialStore.create(this);
        if (store.hasCredentials()) {
            showReadyAutoLogin(endpoint, store);
        } else {
            LoginActivity.start(this, endpoint, null);
            finish();
        }
    }

    private void showReadyAutoLogin(ServerEndpoint endpoint, AuthCredentialStore store) {
        checkingCard.setVisibility(View.GONE);
        connectionCard.setVisibility(View.GONE);
        readyCard.setVisibility(View.VISIBLE);
        progressReady.setVisibility(View.VISIBLE);
        readySummary.setText(getString(R.string.auto_login_message, endpoint.host(), endpoint.port()));

        executor.execute(
                () -> {
                    byte[] uid = store.getUserIdBytes();
                    String password = store.getPassword();
                    if (uid.length != 16 || password.isEmpty()) {
                        runOnUiThread(
                                () -> {
                                    LoginActivity.start(MainActivity.this, endpoint, getString(R.string.error_auto_login_failed));
                                    finish();
                                });
                        return;
                    }
                    boolean sessionOk = ZspSessionManager.get().establishSession(endpoint, uid, password);
                    runOnUiThread(
                            () -> {
                                progressReady.setVisibility(View.GONE);
                                if (sessionOk) {
                                    MainPlaceholderActivity.start(MainActivity.this, endpoint);
                                    finish();
                                } else {
                                    ZspSessionManager.get().close();
                                    LoginActivity.start(MainActivity.this, endpoint, getString(R.string.error_auto_login_failed));
                                    finish();
                                }
                            });
                });
    }

    private void showEditServerForm() {
        checkingCard.setVisibility(View.GONE);
        readyCard.setVisibility(View.GONE);
        connectionCard.setVisibility(View.VISIBLE);
        connectProgress.setVisibility(View.GONE);
        checkButton.setEnabled(true);
        connectStatus.setText(R.string.change_server_hint);
        ServerEndpoint saved = configStore.getSavedEndpoint();
        if (saved != null) {
            hostInput.setText(saved.host());
            portInput.setText(String.valueOf(saved.port()));
        } else {
            hostInput.setText("");
            portInput.setText(String.valueOf(DEFAULT_ZSP_PORT));
        }
    }

    private void showChecking(String message) {
        checkingCard.setVisibility(View.VISIBLE);
        connectionCard.setVisibility(View.GONE);
        readyCard.setVisibility(View.GONE);
        checkingProgress.setVisibility(View.VISIBLE);
        checkingStatus.setText(message);
    }

    private void showConnectionForm(String status, @Nullable ServerEndpoint prefill) {
        checkingCard.setVisibility(View.GONE);
        connectionCard.setVisibility(View.VISIBLE);
        readyCard.setVisibility(View.GONE);
        connectProgress.setVisibility(View.GONE);
        checkButton.setEnabled(true);
        connectStatus.setText(status);

        if (prefill != null) {
            hostInput.setText(prefill.host());
            portInput.setText(String.valueOf(prefill.port()));
            return;
        }
        hostInput.setText("");
        if (portInput.getText().toString().isBlank()) {
            portInput.setText(String.valueOf(DEFAULT_ZSP_PORT));
        }
    }
}
