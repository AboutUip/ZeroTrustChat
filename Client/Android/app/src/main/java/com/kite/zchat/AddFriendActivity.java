package com.kite.zchat;

import android.os.Bundle;
import android.view.View;
import android.widget.ProgressBar;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.textfield.TextInputEditText;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.friends.FriendIdentityStore;
import com.kite.zchat.friends.FriendSigning;
import com.kite.zchat.friends.FriendZspHelper;
import com.kite.zchat.zsp.ZspSessionManager;

import org.bouncycastle.crypto.params.Ed25519PrivateKeyParameters;

import java.util.Arrays;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class AddFriendActivity extends AppCompatActivity {

    public static final String EXTRA_HOST = "host";
    public static final String EXTRA_PORT = "port";

    private final ExecutorService io = Executors.newSingleThreadExecutor();

    private TextInputEditText targetInput;
    private MaterialButton sendBtn;
    private ProgressBar progress;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_add_friend);

        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.setNavigationOnClickListener(v -> finish());

        targetInput = findViewById(R.id.addFriendTargetHex);
        sendBtn = findViewById(R.id.addFriendSend);
        progress = findViewById(R.id.addFriendProgress);

        String host = getIntent().getStringExtra(EXTRA_HOST);
        int port = getIntent().getIntExtra(EXTRA_PORT, 0);

        sendBtn.setOnClickListener(
                v -> {
                    String hex = targetInput.getText() != null ? targetInput.getText().toString().trim() : "";
                    io.execute(() -> runSend(host, port, hex));
                });
    }

    private void runSend(String host, int port, String targetHex) {
        postUi(
                () -> {
                    progress.setVisibility(View.VISIBLE);
                    sendBtn.setEnabled(false);
                });
        try {
            if (!FriendZspHelper.ensureSession(this, host, port)) {
                postUi(this::toastFailed);
                return;
            }
            if (!FriendZspHelper.publishIdentityEd25519(this)) {
                postUi(this::toastFailed);
                return;
            }
            AuthCredentialStore creds = AuthCredentialStore.create(this);
            byte[] from = creds.getUserIdBytes();
            byte[] to = AuthCredentialStore.hexToBytes(targetHex);
            if (from.length != 16 || to.length != 16) {
                postUi(
                        () ->
                                Toast.makeText(this, R.string.error_user_id_hex, Toast.LENGTH_SHORT)
                                        .show());
                return;
            }
            if (Arrays.equals(from, to)) {
                postUi(
                        () ->
                                Toast.makeText(this, R.string.error_user_id_hex, Toast.LENGTH_SHORT)
                                        .show());
                return;
            }
            for (byte[] fid : ZspSessionManager.get().friendListGet()) {
                if (fid != null && fid.length == 16 && Arrays.equals(fid, to)) {
                    postUi(
                            () ->
                                    Toast.makeText(
                                                    this,
                                                    R.string.add_friend_already_friend,
                                                    Toast.LENGTH_SHORT)
                                            .show());
                    return;
                }
            }
            FriendIdentityStore ids = FriendIdentityStore.create(this);
            Ed25519PrivateKeyParameters sk = ids.getOrCreatePrivateKey(creds.getUserIdHex());
            long ts = System.currentTimeMillis() / 1000L;
            byte[] sig = FriendSigning.signSendFriendRequest(sk, from, to, ts);
            byte[] wire = FriendSigning.buildFriendRequestWire(from, to, ts, sig);
            byte[] reqId = ZspSessionManager.get().friendRequestSend(wire);
            if (reqId != null && reqId.length >= 16) {
                boolean duplicatePending = reqId.length >= 17 && reqId[16] != 0;
                postUi(
                        () -> {
                            if (duplicatePending) {
                                Toast.makeText(
                                                this,
                                                R.string.add_friend_duplicate_pending,
                                                Toast.LENGTH_SHORT)
                                        .show();
                            } else {
                                Toast.makeText(this, R.string.add_friend_ok, Toast.LENGTH_SHORT).show();
                            }
                        });
            } else {
                postUi(this::toastFailed);
            }
        } finally {
            postUi(
                    () -> {
                        progress.setVisibility(View.GONE);
                        sendBtn.setEnabled(true);
                    });
        }
    }

    private void toastFailed() {
        Toast.makeText(this, R.string.add_friend_failed, Toast.LENGTH_SHORT).show();
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
