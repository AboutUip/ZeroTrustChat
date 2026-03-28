package com.kite.zchat.chat;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.kite.zchat.R;

public final class ChatEmojiGridAdapter extends RecyclerView.Adapter<ChatEmojiGridAdapter.VH> {

    public interface OnEmojiClickListener {
        void onEmojiClick(String emoji);
    }

    private final OnEmojiClickListener listener;

    public ChatEmojiGridAdapter(OnEmojiClickListener listener) {
        this.listener = listener;
    }

    @NonNull
    @Override
    public VH onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View v =
                LayoutInflater.from(parent.getContext())
                        .inflate(R.layout.item_chat_emoji_cell, parent, false);
        return new VH(v);
    }

    @Override
    public void onBindViewHolder(@NonNull VH h, int position) {
        String e = EmojiPalette.EMOJIS[position];
        h.text.setText(e);
        h.itemView.setOnClickListener(
                v -> {
                    if (listener != null) {
                        listener.onEmojiClick(e);
                    }
                });
    }

    @Override
    public int getItemCount() {
        return EmojiPalette.EMOJIS.length;
    }

    static final class VH extends RecyclerView.ViewHolder {
        final TextView text;

        VH(@NonNull View itemView) {
            super(itemView);
            text = (TextView) itemView;
        }
    }
}
