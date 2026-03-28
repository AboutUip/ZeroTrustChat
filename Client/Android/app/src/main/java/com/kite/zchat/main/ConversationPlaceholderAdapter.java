package com.kite.zchat.main;

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
import androidx.recyclerview.widget.RecyclerView;

import com.kite.zchat.R;
import com.kite.zchat.conversation.ConversationPlaceholderStore;
import com.kite.zchat.profile.ProfileDisplayHelper;

import java.io.File;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.List;

public final class ConversationPlaceholderAdapter extends RecyclerView.Adapter<ConversationPlaceholderAdapter.Holder> {

    public interface Listener {
        void onConversationClick(ConversationPlaceholderStore.Row row);
    }

    public interface LongPressListener {
        /** @param anchor 用于弹出菜单定位的行视图 */
        void onConversationLongPress(ConversationPlaceholderStore.Row row, View anchor);
    }

    private final List<ConversationPlaceholderStore.Row> items = new ArrayList<>();
    @Nullable private Listener listener;
    @Nullable private LongPressListener longPressListener;

    public void setListener(@Nullable Listener listener) {
        this.listener = listener;
    }

    public void setLongPressListener(@Nullable LongPressListener longPressListener) {
        this.longPressListener = longPressListener;
    }

    public void setItems(List<ConversationPlaceholderStore.Row> next) {
        items.clear();
        if (next != null) {
            items.addAll(next);
        }
        notifyDataSetChanged();
    }

    @NonNull
    @Override
    public Holder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View v =
                LayoutInflater.from(parent.getContext())
                        .inflate(R.layout.item_conversation_placeholder, parent, false);
        return new Holder(v);
    }

    @Override
    public void onBindViewHolder(@NonNull Holder h, int position) {
        Context ctx = h.itemView.getContext();
        ConversationPlaceholderStore.Row r = items.get(position);
        String title =
                ProfileDisplayHelper.effectiveDisplayName(r.displayName, r.peerUserIdHex32);
        h.title.setText(title);
        h.subtitle.setText(r.lastMessage);
        h.time.setText(formatSessionTime(ctx, r.lastTimeMs));

        if (r.unreadCount > 0) {
            h.unread.setVisibility(View.VISIBLE);
            int n = r.unreadCount;
            h.unread.setText(n > 99 ? "99+" : String.valueOf(n));
        } else {
            h.unread.setVisibility(View.GONE);
        }

        File avatar = ConversationPlaceholderStore.avatarFile(ctx, r.peerUserIdHex32);
        if (avatar != null && avatar.isFile() && avatar.length() > 0L) {
            Bitmap bmp = BitmapFactory.decodeFile(avatar.getAbsolutePath());
            if (bmp != null) {
                h.avatar.setImageTintList(null);
                h.avatar.setImageBitmap(bmp);
            } else {
                h.avatar.setImageTintList(null);
                h.avatar.setImageResource(R.drawable.ic_avatar_default);
            }
        } else {
            h.avatar.setImageTintList(null);
            h.avatar.setImageResource(R.drawable.ic_avatar_default);
        }

        h.itemView.setOnClickListener(
                v -> {
                    if (listener != null) {
                        listener.onConversationClick(r);
                    }
                });
        h.itemView.setOnLongClickListener(
                v -> {
                    if (longPressListener != null) {
                        longPressListener.onConversationLongPress(r, v);
                        return true;
                    }
                    return false;
                });
    }

    /** 当天显示时间；否则显示日期（按系统区域格式）。 */
    private static String formatSessionTime(Context ctx, long ms) {
        if (ms <= 0L) {
            return "";
        }
        Calendar now = Calendar.getInstance();
        Calendar t = Calendar.getInstance();
        t.setTimeInMillis(ms);
        if (now.get(Calendar.YEAR) == t.get(Calendar.YEAR)
                && now.get(Calendar.DAY_OF_YEAR) == t.get(Calendar.DAY_OF_YEAR)) {
            return android.text.format.DateFormat.getTimeFormat(ctx).format(new Date(ms));
        }
        return android.text.format.DateFormat.getDateFormat(ctx).format(new Date(ms));
    }

    @Override
    public int getItemCount() {
        return items.size();
    }

    static final class Holder extends RecyclerView.ViewHolder {
        final ImageView avatar;
        final TextView title;
        final TextView subtitle;
        final TextView time;
        final TextView unread;

        Holder(@NonNull View itemView) {
            super(itemView);
            avatar = itemView.findViewById(R.id.conversationAvatar);
            title = itemView.findViewById(R.id.conversationTitle);
            subtitle = itemView.findViewById(R.id.conversationSubtitle);
            time = itemView.findViewById(R.id.conversationTime);
            unread = itemView.findViewById(R.id.conversationUnread);
        }
    }
}
