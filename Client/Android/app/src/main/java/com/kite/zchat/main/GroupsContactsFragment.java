package com.kite.zchat.main;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.kite.zchat.R;
import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.contacts.ContactsCache;
import com.kite.zchat.core.ServerEndpoint;
import com.kite.zchat.zsp.ZspContactsCodec;
import com.kite.zchat.zsp.ZspSessionManager;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** 通讯录 — 群聊列表（群名 + 人数；无群头像 API 时使用占位图标）。 */
public final class GroupsContactsFragment extends Fragment {

    private static final String ARG_HOST = "host";
    private static final String ARG_PORT = "port";

    private final ExecutorService io = Executors.newSingleThreadExecutor();

    private RecyclerView recycler;
    private ProgressBar progress;
    private TextView empty;
    private GroupsListAdapter adapter;

    @Nullable
    @Override
    public View onCreateView(
            @NonNull LayoutInflater inflater,
            @Nullable ViewGroup container,
            @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_contacts_list, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        recycler = view.findViewById(R.id.contactsRecycler);
        progress = view.findViewById(R.id.contactsProgress);
        empty = view.findViewById(R.id.contactsEmpty);
        adapter = new GroupsListAdapter();
        recycler.setLayoutManager(new LinearLayoutManager(requireContext()));
        recycler.setAdapter(adapter);
        empty.setText(R.string.contacts_empty_groups);
    }

    @Override
    public void onResume() {
        super.onResume();
        loadGroups();
    }

    private void loadGroups() {
        AuthCredentialStore creds;
        try {
            creds = AuthCredentialStore.create(requireContext());
        } catch (RuntimeException e) {
            progress.setVisibility(View.GONE);
            adapter.setItems(Collections.emptyList());
            empty.setVisibility(View.VISIBLE);
            return;
        }
        Bundle args = getArguments();
        String host = args != null ? args.getString(ARG_HOST) : null;
        int port = args != null ? args.getInt(ARG_PORT, 0) : 0;
        String userHex = creds.getUserIdHex();
        final String cacheKey = ContactsCache.buildKey(host, port, userHex);

        if (host != null
                && port > 0
                && creds.hasCredentials()
                && userHex.length() == 32) {
            List<GroupsListAdapter.ContactGroupItem> cached = ContactsCache.groupsIfFresh(cacheKey);
            if (cached != null) {
                progress.setVisibility(View.GONE);
                adapter.setItems(cached);
                empty.setVisibility(cached.isEmpty() ? View.VISIBLE : View.GONE);
                return;
            }
        }

        progress.setVisibility(View.VISIBLE);
        empty.setVisibility(View.GONE);
        io.execute(
                () -> {
                    AuthCredentialStore credsBg;
                    try {
                        credsBg = AuthCredentialStore.create(requireContext());
                    } catch (RuntimeException e) {
                        postFail();
                        return;
                    }
                    Bundle argsBg = getArguments();
                    String hostBg = argsBg != null ? argsBg.getString(ARG_HOST) : null;
                    int portBg = argsBg != null ? argsBg.getInt(ARG_PORT, 0) : 0;
                    if (hostBg == null || portBg <= 0) {
                        postEmpty();
                        return;
                    }
                    if (!credsBg.hasCredentials()) {
                        postEmpty();
                        return;
                    }
                    ServerEndpoint ep = new ServerEndpoint(hostBg, portBg);
                    byte[] uid = credsBg.getUserIdBytes();
                    String pw = credsBg.getPassword();
                    if (uid.length != 16 || pw.isEmpty()) {
                        postEmpty();
                        return;
                    }
                    if (!ZspSessionManager.get().ensureSession(ep, uid, pw)) {
                        postFail();
                        return;
                    }
                    byte[][] groupIds = ZspSessionManager.get().groupListGet();
                    List<GroupsListAdapter.ContactGroupItem> out = new ArrayList<>();
                    for (byte[] gid : groupIds) {
                        if (gid == null || gid.length != 16) {
                            continue;
                        }
                        String hex = AuthCredentialStore.bytesToHex(gid);
                        ZspContactsCodec.GroupInfo info = ZspSessionManager.get().groupInfoGet(gid);
                        String name = info != null ? info.nameUtf8 : "";
                        int members = info != null ? info.memberCount : 0;
                        out.add(new GroupsListAdapter.ContactGroupItem(hex, name, members));
                    }
                    ContactsCache.putGroups(cacheKey, out);
                    postUi(
                            () -> {
                                if (!isAdded()) {
                                    return;
                                }
                                progress.setVisibility(View.GONE);
                                adapter.setItems(out);
                                empty.setVisibility(out.isEmpty() ? View.VISIBLE : View.GONE);
                            });
                });
    }

    private void postEmpty() {
        postUi(
                () -> {
                    if (!isAdded()) {
                        return;
                    }
                    progress.setVisibility(View.GONE);
                    adapter.setItems(Collections.emptyList());
                    empty.setVisibility(View.VISIBLE);
                });
    }

    private void postFail() {
        postUi(
                () -> {
                    if (!isAdded()) {
                        return;
                    }
                    progress.setVisibility(View.GONE);
                    adapter.setItems(Collections.emptyList());
                    empty.setVisibility(View.VISIBLE);
                    Toast.makeText(requireContext(), R.string.contacts_load_failed, Toast.LENGTH_SHORT)
                            .show();
                });
    }

    private void postUi(Runnable r) {
        if (getActivity() == null) {
            return;
        }
        requireActivity().runOnUiThread(r);
    }
}
