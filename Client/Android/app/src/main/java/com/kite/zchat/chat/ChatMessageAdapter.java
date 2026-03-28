package com.kite.zchat.chat;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.widget.PopupMenu;
import androidx.recyclerview.widget.RecyclerView;

import com.kite.zchat.R;
import com.kite.zchat.call.ChatCallLogHelper;

import java.io.File;
import java.text.DateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public final class ChatMessageAdapter extends RecyclerView.Adapter<RecyclerView.ViewHolder> {

    private static final int TYPE_IN = 0;
    private static final int TYPE_OUT = 1;
    private static final int TYPE_CALL_LOG = 2;

    private final List<ChatMessageDb.Row> items = new ArrayList<>();
    private String selfDisplayName;
    private String peerDisplayName;
    private final File selfAvatarFile;
    private final File peerAvatarFile;
    @Nullable private Runnable onPeerAvatarClick;
    @Nullable private OnReplyToMessageListener onReplyToMessage;

    public interface OnReplyToMessageListener {
        void onReplyToMessage(ChatMessageDb.Row row);
    }

    public ChatMessageAdapter(
            String selfDisplayName,
            String peerDisplayName,
            File selfAvatarFile,
            File peerAvatarFile) {
        this.selfDisplayName = selfDisplayName != null ? selfDisplayName : "";
        this.peerDisplayName = peerDisplayName != null ? peerDisplayName : "";
        this.selfAvatarFile = selfAvatarFile;
        this.peerAvatarFile = peerAvatarFile;
    }

    public void setItems(List<ChatMessageDb.Row> next) {
        items.clear();
        if (next != null) {
            items.addAll(next);
        }
        notifyDataSetChanged();
    }

    public void setDisplayNames(String selfDisplayName, String peerDisplayName) {
        this.selfDisplayName = selfDisplayName != null ? selfDisplayName : "";
        this.peerDisplayName = peerDisplayName != null ? peerDisplayName : "";
        notifyDataSetChanged();
    }

    public void setOnPeerAvatarClickListener(@Nullable Runnable r) {
        this.onPeerAvatarClick = r;
    }

    public void setOnReplyToMessageListener(@Nullable OnReplyToMessageListener l) {
        this.onReplyToMessage = l;
    }

    @Override
    public int getItemViewType(int position) {
        ChatMessageDb.Row row = items.get(position);
        if (ChatCallLogHelper.isCallLog(row.text)) {
            return TYPE_CALL_LOG;
        }
        return row.outgoing ? TYPE_OUT : TYPE_IN;
    }

    @NonNull
    @Override
    public RecyclerView.ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        LayoutInflater inf = LayoutInflater.from(parent.getContext());
        if (viewType == TYPE_CALL_LOG) {
            View v = inf.inflate(R.layout.item_chat_call_log, parent, false);
            return new CallLogHolder(v);
        }
        if (viewType == TYPE_OUT) {
            View v = inf.inflate(R.layout.item_chat_message_out, parent, false);
            return new Holder(v);
        }
        View v = inf.inflate(R.layout.item_chat_message_in, parent, false);
        return new Holder(v);
    }

    @Override
    public void onBindViewHolder(@NonNull RecyclerView.ViewHolder h, int position) {
        ChatMessageDb.Row row = items.get(position);
        if (h instanceof CallLogHolder) {
            CallLogHolder cl = (CallLogHolder) h;
            String line =
                    ChatCallLogHelper.formatBubbleLine(
                            cl.itemView.getContext(), ChatCallLogHelper.jsonPayload(row.text));
            cl.text.setText(line);
            cl.time.setText(formatTime(row.tsMs));
            return;
        }
        Holder x = (Holder) h;
        x.name.setText(row.outgoing ? selfDisplayName : peerDisplayName);
        ChatReplyCodec.Parsed p = ChatReplyCodec.parse(row.text);
        if (x.quoteBlock != null) {
            if (p.hasQuote()) {
                x.quoteBlock.setVisibility(View.VISIBLE);
                if (x.quote != null) {
                    x.quote.setText(
                            p.refPreview != null && !p.refPreview.isEmpty()
                                    ? p.refPreview
                                    : x.quote.getContext().getString(R.string.chat_reply_action));
                }
            } else {
                x.quoteBlock.setVisibility(View.GONE);
            }
        } else if (x.quote != null) {
            x.quote.setVisibility(View.GONE);
        }
        x.text.setText(p.body);
        x.time.setText(formatTime(row.tsMs));
        loadAvatar(x.avatar, row.outgoing ? selfAvatarFile : peerAvatarFile);
        if (row.outgoing) {
            x.avatar.setOnClickListener(null);
            x.avatar.setClickable(false);
        } else {
            x.avatar.setClickable(true);
            x.avatar.setOnClickListener(
                    v -> {
                        if (onPeerAvatarClick != null) {
                            onPeerAvatarClick.run();
                        }
                    });
        }
        x.itemView.setLongClickable(true);
        View.OnLongClickListener rowMenu =
                v -> {
                    if (ChatCallLogHelper.isCallLog(row.text)) {
                        return false;
                    }
                    showMessageRowMenu(v, row, p);
                    return true;
                };
        x.itemView.setOnLongClickListener(rowMenu);
        if (x.bubbleCard != null) {
            x.bubbleCard.setOnLongClickListener(rowMenu);
        }
    }

    private void showMessageRowMenu(View anchor, ChatMessageDb.Row row, ChatReplyCodec.Parsed p) {
        Context ctx = anchor.getContext();
        PopupMenu pm = new PopupMenu(ctx, anchor);
        pm.getMenuInflater().inflate(R.menu.chat_message_popup, pm.getMenu());
        boolean canReply = row.msgIdHex.length() == 32 && onReplyToMessage != null;
        pm.getMenu().findItem(R.id.chat_message_reply).setVisible(canReply);
        pm.setOnMenuItemClickListener(
                item -> {
                    int id = item.getItemId();
                    if (id == R.id.chat_message_reply) {
                        if (onReplyToMessage != null) {
                            onReplyToMessage.onReplyToMessage(row);
                        }
                        return true;
                    }
                    if (id == R.id.chat_message_copy) {
                        String body = p.body != null ? p.body : "";
                        ClipboardManager cm =
                                (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
                        if (cm != null) {
                            cm.setPrimaryClip(ClipData.newPlainText("message", body));
                        }
                        return true;
                    }
                    return false;
                });
        pm.show();
    }

    private static String formatTime(long tsMs) {
        DateFormat tf = DateFormat.getTimeInstance(DateFormat.SHORT, Locale.getDefault());
        return tf.format(new Date(tsMs));
    }

    private static void loadAvatar(ImageView iv, File f) {
        if (f != null && f.isFile() && f.length() > 0L) {
            Bitmap bmp = BitmapFactory.decodeFile(f.getAbsolutePath());
            if (bmp != null) {
                iv.setImageTintList(null);
                iv.setImageBitmap(bmp);
                return;
            }
        }
        iv.setImageTintList(null);
        iv.setImageResource(R.drawable.ic_avatar_default);
    }

    @Override
    public int getItemCount() {
        return items.size();
    }

    static final class Holder extends RecyclerView.ViewHolder {
        final ImageView avatar;
        final TextView name;
        @Nullable final View quoteBlock;
        @Nullable final TextView quote;
        final TextView text;
        final TextView time;
        @Nullable final View bubbleCard;

        Holder(@NonNull View itemView) {
            super(itemView);
            avatar = itemView.findViewById(R.id.chatBubbleAvatar);
            name = itemView.findViewById(R.id.chatBubbleName);
            quoteBlock = itemView.findViewById(R.id.chatBubbleQuoteBlock);
            quote = itemView.findViewById(R.id.chatBubbleQuote);
            text = itemView.findViewById(R.id.chatBubbleText);
            time = itemView.findViewById(R.id.chatBubbleTime);
            bubbleCard = itemView.findViewById(R.id.chatBubbleCard);
        }
    }

    static final class CallLogHolder extends RecyclerView.ViewHolder {
        final TextView text;
        final TextView time;

        CallLogHolder(@NonNull View itemView) {
            super(itemView);
            text = itemView.findViewById(R.id.chatCallLogText);
            time = itemView.findViewById(R.id.chatCallLogTime);
        }
    }
}
