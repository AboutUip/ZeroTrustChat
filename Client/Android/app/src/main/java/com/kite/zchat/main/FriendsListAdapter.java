package com.kite.zchat.main;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.kite.zchat.R;

import java.io.ByteArrayInputStream;
import java.util.ArrayList;
import java.util.List;

public final class FriendsListAdapter extends RecyclerView.Adapter<FriendsListAdapter.Holder> {

    /**
     * 与 {@link com.kite.zchat.profile.LocalProfileStore} 上传上限（1024px 边）对齐；若解码仍用 512 会把 1024 图再缩一半导致发糊。
     * 超大图仍限制边长，避免 OOM。
     */
    private static final int MAX_DECODE_SIDE_PX = 2048;

    public static final class ContactFriendItem {
        public final String userIdHex;
        public final String displayName;
        public final byte[] avatarBytes;

        public ContactFriendItem(String userIdHex, String displayName, byte[] avatarBytes) {
            this.userIdHex = userIdHex != null ? userIdHex : "";
            this.displayName = displayName != null ? displayName : "";
            this.avatarBytes = avatarBytes;
        }
    }

    public interface Listener {
        void onFriendClick(ContactFriendItem item);
    }

    private final List<ContactFriendItem> items = new ArrayList<>();
    private Listener listener;

    public void setListener(Listener listener) {
        this.listener = listener;
    }

    public void setItems(List<ContactFriendItem> next) {
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
                        .inflate(R.layout.item_contact_friend, parent, false);
        return new Holder(v);
    }

    @Override
    public void onBindViewHolder(@NonNull Holder h, int position) {
        ContactFriendItem it = items.get(position);
        h.name.setText(it.displayName);
        if (it.avatarBytes != null && it.avatarBytes.length > 0) {
            Bitmap bmp = decodeContactAvatar(it.avatarBytes);
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
                        listener.onFriendClick(it);
                    }
                });
    }

    /** 通讯录行头像：大图降采样避免 OOM；部分机型 decodeByteArray 失败时尝试 decodeStream。 */
    public static Bitmap decodeContactAvatar(byte[] bytes) {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeByteArray(bytes, 0, bytes.length, bounds);
        int w = bounds.outWidth;
        int h = bounds.outHeight;
        BitmapFactory.Options opts = new BitmapFactory.Options();
        opts.inPreferredConfig = Bitmap.Config.ARGB_8888;
        if (w > 0 && h > 0) {
            int inSampleSize = 1;
            while (Math.max(w, h) / inSampleSize > MAX_DECODE_SIDE_PX) {
                inSampleSize *= 2;
            }
            opts.inSampleSize = Math.max(1, inSampleSize);
        }
        Bitmap bmp = BitmapFactory.decodeByteArray(bytes, 0, bytes.length, opts);
        if (bmp != null) {
            return bmp;
        }
        // ByteArrayInputStream 无需 close；避免 try-with-resources 对 close() 的 IOException 检查
        return BitmapFactory.decodeStream(new ByteArrayInputStream(bytes));
    }

    @Override
    public int getItemCount() {
        return items.size();
    }

    static final class Holder extends RecyclerView.ViewHolder {
        final ImageView avatar;
        final TextView name;

        Holder(@NonNull View itemView) {
            super(itemView);
            avatar = itemView.findViewById(R.id.contactFriendAvatar);
            name = itemView.findViewById(R.id.contactFriendName);
        }
    }
}
