package com.kite.zchat.main;

import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.widget.PopupMenu;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

import com.kite.zchat.ChatActivity;
import com.kite.zchat.R;
import com.kite.zchat.chat.ChatEvents;
import com.kite.zchat.chat.ChatMessageDb;
import com.kite.zchat.chat.ChatSync;
import com.kite.zchat.conversation.ConversationPlaceholderStore;

import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class CommunicationFragment extends Fragment {

    private static final String ARG_HOST = "host";
    private static final String ARG_PORT = "port";

    private SwipeRefreshLayout swipeRefresh;
    private RecyclerView recycler;
    private TextView empty;
    private ConversationPlaceholderAdapter adapter;
    @Nullable private String host;
    private int port;
    @Nullable private BroadcastReceiver conversationListReceiver;
    private final ExecutorService io = Executors.newSingleThreadExecutor();

    public static CommunicationFragment newInstance(@Nullable String host, int port) {
        CommunicationFragment f = new CommunicationFragment();
        Bundle b = new Bundle();
        b.putString(ARG_HOST, host);
        b.putInt(ARG_PORT, port);
        f.setArguments(b);
        return f;
    }

    @Nullable
    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Bundle a = getArguments();
        if (a != null) {
            host = a.getString(ARG_HOST);
            port = a.getInt(ARG_PORT, 0);
        }
    }

    @Override
    public View onCreateView(
            @NonNull LayoutInflater inflater,
            @Nullable ViewGroup container,
            @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_communication, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        TextView title = view.findViewById(R.id.tabTitle);
        TextView subtitle = view.findViewById(R.id.tabSubtitle);
        empty = view.findViewById(R.id.conversationsEmpty);
        recycler = view.findViewById(R.id.conversationsRecycler);
        swipeRefresh = view.findViewById(R.id.conversationsSwipeRefresh);
        title.setText(R.string.tab_communication_title);
        subtitle.setText(R.string.tab_communication_subtitle);

        TypedValue tv = new TypedValue();
        requireContext()
                .getTheme()
                .resolveAttribute(com.google.android.material.R.attr.colorPrimary, tv, true);
        swipeRefresh.setColorSchemeColors(tv.data);
        swipeRefresh.setOnRefreshListener(this::onPullToSync);
        updateSwipeEnabled();

        adapter = new ConversationPlaceholderAdapter();
        adapter.setListener(
                row -> {
                    if (host == null || host.isBlank() || port <= 0) {
                        return;
                    }
                    Intent i =
                            ChatActivity.buildIntent(
                                    requireContext(),
                                    host,
                                    port,
                                    row.peerUserIdHex32,
                                    row.displayName);
                    startActivity(i);
                });
        adapter.setLongPressListener(this::showConversationRowMenu);
        recycler.setLayoutManager(new LinearLayoutManager(requireContext()));
        recycler.setAdapter(adapter);
    }

    @Override
    public void onStart() {
        super.onStart();
        conversationListReceiver =
                new BroadcastReceiver() {
                    @Override
                    public void onReceive(Context context, Intent intent) {
                        refreshList();
                    }
                };
        IntentFilter f = new IntentFilter(ChatEvents.ACTION_CONVERSATION_LIST_CHANGED);
        ContextCompat.registerReceiver(
                requireContext(),
                conversationListReceiver,
                f,
                ContextCompat.RECEIVER_NOT_EXPORTED);
    }

    @Override
    public void onStop() {
        if (conversationListReceiver != null) {
            try {
                requireContext().unregisterReceiver(conversationListReceiver);
            } catch (IllegalArgumentException ignored) {
            }
            conversationListReceiver = null;
        }
        super.onStop();
    }

    @Override
    public void onResume() {
        super.onResume();
        updateSwipeEnabled();
        refreshList();
    }

    @Override
    public void onDestroyView() {
        swipeRefresh = null;
        super.onDestroyView();
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        io.shutdownNow();
    }

    private void updateSwipeEnabled() {
        if (swipeRefresh != null) {
            swipeRefresh.setEnabled(host != null && !host.isBlank() && port > 0);
        }
    }

    private void onPullToSync() {
        if (host == null || host.isBlank() || port <= 0) {
            if (swipeRefresh != null) {
                swipeRefresh.setRefreshing(false);
            }
            Toast.makeText(requireContext(), R.string.profile_sync_no_server, Toast.LENGTH_SHORT).show();
            return;
        }
        final int sessionCount =
                ConversationPlaceholderStore.listSessions(requireContext()).size();
        final Context app = requireContext().getApplicationContext();
        io.execute(
                () -> {
                    boolean ok = ChatSync.syncAllOpenSessions(app, host, port, true);
                    if (getActivity() != null && isAdded()) {
                        requireActivity()
                                .runOnUiThread(
                                        () -> {
                                            if (swipeRefresh != null) {
                                                swipeRefresh.setRefreshing(false);
                                            }
                                            refreshList();
                                            if (sessionCount > 0 && !ok) {
                                                Toast.makeText(
                                                                requireContext(),
                                                                R.string.communication_sync_failed,
                                                                Toast.LENGTH_SHORT)
                                                        .show();
                                            }
                                        });
                    }
                });
    }

    private void refreshList() {
        List<ConversationPlaceholderStore.Row> rows =
                ConversationPlaceholderStore.listSessions(requireContext());
        adapter.setItems(rows);
        boolean isEmpty = rows.isEmpty();
        empty.setVisibility(isEmpty ? View.VISIBLE : View.GONE);
        // RecyclerView 保持可见，以便无会话时仍可下拉触发同步
    }

    private void showConversationRowMenu(
            ConversationPlaceholderStore.Row row, View anchor) {
        Context ctx = requireContext();
        PopupMenu pm = new PopupMenu(ctx, anchor);
        pm.getMenuInflater().inflate(R.menu.conversation_row_popup, pm.getMenu());
        pm.setOnMenuItemClickListener(
                item -> {
                    int id = item.getItemId();
                    if (id == R.id.conversation_menu_copy_user_id) {
                        ClipboardManager cm =
                                (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
                        if (cm != null) {
                            cm.setPrimaryClip(
                                    ClipData.newPlainText("userId", row.peerUserIdHex32));
                            Toast.makeText(ctx, R.string.friend_detail_id_copied, Toast.LENGTH_SHORT)
                                    .show();
                        }
                        return true;
                    }
                    if (id == R.id.conversation_menu_remove_from_chats) {
                        new ChatMessageDb(ctx).deleteMessagesForPeer(row.peerUserIdHex32);
                        ConversationPlaceholderStore.removeSession(ctx, row.peerUserIdHex32);
                        Toast.makeText(
                                        ctx,
                                        R.string.conversation_removed_from_chats_placeholder,
                                        Toast.LENGTH_SHORT)
                                .show();
                        refreshList();
                        return true;
                    }
                    return false;
                });
        pm.show();
    }
}
