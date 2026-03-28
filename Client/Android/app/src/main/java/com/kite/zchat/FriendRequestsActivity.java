package com.kite.zchat;

import android.os.Bundle;
import android.view.View;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.appbar.MaterialToolbar;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.contacts.ContactsCache;
import com.kite.zchat.friends.FriendIdentityStore;
import com.kite.zchat.friends.FriendSigning;
import com.kite.zchat.friends.FriendZspHelper;
import com.kite.zchat.main.FriendRequestsListAdapter;
import com.kite.zchat.zsp.ZspFriendCodec;
import com.kite.zchat.zsp.ZspSessionManager;

import org.bouncycastle.crypto.params.Ed25519PrivateKeyParameters;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class FriendRequestsActivity extends AppCompatActivity implements FriendRequestsListAdapter.Listener {

    public static final String EXTRA_HOST = "host";
    public static final String EXTRA_PORT = "port";

    private final ExecutorService io = Executors.newSingleThreadExecutor();

    private RecyclerView recycler;
    private ProgressBar progress;
    private TextView empty;
    private FriendRequestsListAdapter adapter;

    private String host;
    private int port;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_friend_requests);

        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.setNavigationOnClickListener(v -> finish());

        host = getIntent().getStringExtra(EXTRA_HOST);
        port = getIntent().getIntExtra(EXTRA_PORT, 0);

        recycler = findViewById(R.id.friendRequestsRecycler);
        progress = findViewById(R.id.friendRequestsProgress);
        empty = findViewById(R.id.friendRequestsEmpty);
        recycler.setLayoutManager(new LinearLayoutManager(this));
        adapter = new FriendRequestsListAdapter(this);
        recycler.setAdapter(adapter);

        loadList();
    }

    private void loadList() {
        progress.setVisibility(View.VISIBLE);
        empty.setVisibility(View.GONE);
        io.execute(
                () -> {
                    if (!FriendZspHelper.ensureSession(this, host, port)) {
                        postFailLoad();
                        return;
                    }
                    byte[] raw = ZspSessionManager.get().friendPendingListGet();
                    List<ZspFriendCodec.PendingRow> rows = ZspFriendCodec.parsePendingRows(raw);
                    postUi(
                            () -> {
                                progress.setVisibility(View.GONE);
                                adapter.setRows(rows);
                                empty.setVisibility(rows.isEmpty() ? View.VISIBLE : View.GONE);
                                empty.setText(R.string.friend_requests_empty);
                            });
                });
    }

    private void postFailLoad() {
        postUi(
                () -> {
                    progress.setVisibility(View.GONE);
                    empty.setVisibility(View.VISIBLE);
                    empty.setText(R.string.friend_requests_load_failed);
                    adapter.setRows(new ArrayList<>());
                });
    }

    @Override
    public void onRespond(int position, boolean accept) {
        ZspFriendCodec.PendingRow row = adapter.getRowAt(position);
        if (row == null) {
            return;
        }
        io.execute(
                () -> {
                    if (!FriendZspHelper.ensureSession(this, host, port)) {
                        postToast(R.string.friend_request_failed);
                        return;
                    }
                    if (!FriendZspHelper.publishIdentityEd25519(this)) {
                        postToast(R.string.friend_request_failed);
                        return;
                    }
                    AuthCredentialStore creds = AuthCredentialStore.create(this);
                    byte[] self = creds.getUserIdBytes();
                    if (self.length != 16) {
                        postToast(R.string.friend_request_failed);
                        return;
                    }
                    FriendIdentityStore ids = FriendIdentityStore.create(this);
                    Ed25519PrivateKeyParameters sk = ids.getOrCreatePrivateKey(creds.getUserIdHex());
                    long ts = System.currentTimeMillis() / 1000L;
                    byte[] sig =
                            FriendSigning.signRespondFriendRequest(sk, row.requestId16, accept, self, ts);
                    byte[] wire =
                            FriendSigning.buildFriendResponseWire(row.requestId16, accept, self, ts, sig);
                    boolean ok = ZspSessionManager.get().friendRequestRespond(wire);
                    if (!ok) {
                        postToast(R.string.friend_request_failed);
                        return;
                    }
                    if (accept) {
                        ContactsCache.clear();
                    }
                    int msg =
                            accept ? R.string.friend_request_ok_accept : R.string.friend_request_ok_reject;
                    postUi(
                            () -> {
                                Toast.makeText(this, msg, Toast.LENGTH_SHORT).show();
                                loadList();
                            });
                });
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

    private void postToast(int res) {
        postUi(() -> Toast.makeText(this, res, Toast.LENGTH_SHORT).show());
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        io.shutdownNow();
    }
}
