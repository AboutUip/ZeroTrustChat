package com.kite.zchat.main;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.kite.zchat.R;
import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.zsp.ZspFriendCodec;

import java.util.ArrayList;
import java.util.List;

public final class FriendRequestsListAdapter extends RecyclerView.Adapter<FriendRequestsListAdapter.Holder> {

    public interface Listener {
        void onRespond(int position, boolean accept);
    }

    private final List<ZspFriendCodec.PendingRow> rows = new ArrayList<>();
    private final Listener listener;

    public FriendRequestsListAdapter(Listener listener) {
        this.listener = listener;
    }

    public void setRows(List<ZspFriendCodec.PendingRow> list) {
        rows.clear();
        if (list != null) {
            rows.addAll(list);
        }
        notifyDataSetChanged();
    }

    public ZspFriendCodec.PendingRow getRowAt(int position) {
        if (position < 0 || position >= rows.size()) {
            return null;
        }
        return rows.get(position);
    }

    @NonNull
    @Override
    public Holder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View v = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_friend_request_row, parent, false);
        return new Holder(v);
    }

    @Override
    public void onBindViewHolder(@NonNull Holder h, int position) {
        ZspFriendCodec.PendingRow row = rows.get(position);
        String hex = AuthCredentialStore.bytesToHex(row.fromUserId16);
        h.label.setText(h.label.getResources().getString(R.string.friend_request_from_format, hex));
        h.accept.setOnClickListener(
                v -> {
                    int pos = h.getBindingAdapterPosition();
                    if (pos != RecyclerView.NO_POSITION) {
                        listener.onRespond(pos, true);
                    }
                });
        h.reject.setOnClickListener(
                v -> {
                    int pos = h.getBindingAdapterPosition();
                    if (pos != RecyclerView.NO_POSITION) {
                        listener.onRespond(pos, false);
                    }
                });
    }

    @Override
    public int getItemCount() {
        return rows.size();
    }

    static final class Holder extends RecyclerView.ViewHolder {
        final TextView label;
        final View accept;
        final View reject;

        Holder(@NonNull View itemView) {
            super(itemView);
            label = itemView.findViewById(R.id.friendRequestFromLabel);
            accept = itemView.findViewById(R.id.friendRequestAccept);
            reject = itemView.findViewById(R.id.friendRequestReject);
        }
    }
}
