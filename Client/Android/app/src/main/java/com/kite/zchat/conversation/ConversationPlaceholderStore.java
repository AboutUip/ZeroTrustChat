package com.kite.zchat.conversation;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.annotation.Nullable;

import com.kite.zchat.R;
import com.kite.zchat.auth.AuthCredentialStore;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * 通信 Tab 占位会话：按<strong>当前登录用户</strong>分域持久化（避免切换账号后仍看到他人会话）。
 */
public final class ConversationPlaceholderStore {

    private static final String PREF_PREFIX = "zchat_conversation_placeholder_v2_";
    private static final String HAS_PREFIX = "has_";
    private static final String NAME_PREFIX = "name_";
    private static final String LAST_MSG_PREFIX = "lastmsg_";
    private static final String LAST_TIME_PREFIX = "lasttime_";
    private static final String UNREAD_PREFIX = "unread_";
    private static final String MUTE_PREFIX = "mute_";

    private static final String AVATAR_SUBDIR = "conv_avatars";

    public static final class Row {
        public final String peerUserIdHex32;
        public final String displayName;
        public final String lastMessage;
        public final long lastTimeMs;
        public final int unreadCount;

        public Row(
                String peerUserIdHex32,
                String displayName,
                String lastMessage,
                long lastTimeMs,
                int unreadCount) {
            this.peerUserIdHex32 = peerUserIdHex32 != null ? peerUserIdHex32 : "";
            this.displayName = displayName != null ? displayName : "";
            this.lastMessage = lastMessage != null ? lastMessage : "";
            this.lastTimeMs = lastTimeMs;
            this.unreadCount = unreadCount;
        }
    }

    private ConversationPlaceholderStore() {}

    /**
     * 会话对方 userId 统一为小写 32 位 hex，与 {@link com.kite.zchat.chat.ChatMessageDb} 及 Intent 传参一致。
     */
    public static String normalizePeer32(String peerUserIdHex32) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return peerUserIdHex32;
        }
        return peerUserIdHex32.trim().toLowerCase(Locale.ROOT);
    }

    /** 将旧的大小写混用键迁移到 canonical 小写键（含头像文件）。 */
    private static void migratePeerBucketToCanonical(
            Context app, SharedPreferences p, String fromHex, String toHex) {
        if (fromHex.equals(toHex)) {
            return;
        }
        SharedPreferences.Editor ed = p.edit();
        ed.putBoolean(HAS_PREFIX + toHex, true);
        String name = p.getString(NAME_PREFIX + fromHex, "");
        if (name == null) {
            name = "";
        }
        if (!p.contains(NAME_PREFIX + toHex) || p.getString(NAME_PREFIX + toHex, "").isEmpty()) {
            ed.putString(NAME_PREFIX + toHex, name);
        }
        long tFrom = p.getLong(LAST_TIME_PREFIX + fromHex, 0L);
        long tTo = p.getLong(LAST_TIME_PREFIX + toHex, 0L);
        ed.putLong(LAST_TIME_PREFIX + toHex, Math.max(tFrom, tTo));
        String msgFrom = p.getString(LAST_MSG_PREFIX + fromHex, "");
        String msgTo = p.getString(LAST_MSG_PREFIX + toHex, "");
        if (msgTo == null || msgTo.isEmpty()) {
            ed.putString(LAST_MSG_PREFIX + toHex, msgFrom != null ? msgFrom : "");
        } else if (tFrom >= tTo && msgFrom != null && !msgFrom.isEmpty()) {
            ed.putString(LAST_MSG_PREFIX + toHex, msgFrom);
        }
        int u = p.getInt(UNREAD_PREFIX + fromHex, 0) + p.getInt(UNREAD_PREFIX + toHex, 0);
        ed.putInt(UNREAD_PREFIX + toHex, u);
        boolean mute = p.getBoolean(MUTE_PREFIX + fromHex, false) || p.getBoolean(MUTE_PREFIX + toHex, false);
        ed.putBoolean(MUTE_PREFIX + toHex, mute);
        ed.remove(HAS_PREFIX + fromHex)
                .remove(NAME_PREFIX + fromHex)
                .remove(LAST_MSG_PREFIX + fromHex)
                .remove(LAST_TIME_PREFIX + fromHex)
                .remove(UNREAD_PREFIX + fromHex)
                .remove(MUTE_PREFIX + fromHex)
                .apply();
        File oldA = avatarFile(app, fromHex);
        File newA = avatarFile(app, toHex);
        if (oldA != null && oldA.isFile() && newA != null) {
            if (!newA.isFile()) {
                //noinspection ResultOfMethodCallIgnored
                oldA.renameTo(newA);
            } else {
                //noinspection ResultOfMethodCallIgnored
                oldA.delete();
            }
        }
    }

    /** 当前登录用户 32 位 hex；未登录或无效时返回 null。 */
    @Nullable
    private static String ownerHex32(Context context) {
        try {
            AuthCredentialStore c = AuthCredentialStore.create(context);
            String h = c.getUserIdHex();
            if (h != null && h.length() == 32) {
                return h.toLowerCase(Locale.ROOT);
            }
        } catch (RuntimeException ignored) {
        }
        return null;
    }

    private static String prefName(Context context) {
        String o = ownerHex32(context);
        if (o == null) {
            return PREF_PREFIX + "_none";
        }
        return PREF_PREFIX + o;
    }

    private static SharedPreferences prefs(Context context) {
        return context.getApplicationContext()
                .getSharedPreferences(prefName(context), Context.MODE_PRIVATE);
    }

    /** 头像文件：{@code files/conv_avatars/{ownerHex32}/{peerHex32}.bin} */
    public static File avatarFile(Context context, String peerUserIdHex32) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return null;
        }
        peerUserIdHex32 = normalizePeer32(peerUserIdHex32);
        String owner = ownerHex32(context);
        if (owner == null) {
            return null;
        }
        File dir = new File(context.getApplicationContext().getFilesDir(), AVATAR_SUBDIR + "/" + owner);
        return new File(dir, peerUserIdHex32 + ".bin");
    }

    /**
     * 新建或覆盖占位会话：写入显示名、头像文件、预览与时间；未读为 0（真实未读仅由新消息 {@link #incrementUnread} 累加，避免与「刚建会话」叠成 2）。
     */
    public static void addSession(
            Context context,
            String peerUserIdHex32,
            @Nullable String displayName,
            @Nullable byte[] avatarBytes) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return;
        }
        peerUserIdHex32 = normalizePeer32(peerUserIdHex32);
        if (ownerHex32(context) == null) {
            return;
        }
        Context app = context.getApplicationContext();
        long now = System.currentTimeMillis();
        String preview = app.getString(R.string.conversation_preview_connected);
        SharedPreferences p = prefs(context);
        p.edit()
                .putBoolean(HAS_PREFIX + peerUserIdHex32, true)
                .putString(NAME_PREFIX + peerUserIdHex32, displayName != null ? displayName : "")
                .putString(LAST_MSG_PREFIX + peerUserIdHex32, preview)
                .putLong(LAST_TIME_PREFIX + peerUserIdHex32, now)
                .putInt(UNREAD_PREFIX + peerUserIdHex32, 0)
                .apply();
        writeAvatarFile(context, peerUserIdHex32, avatarBytes);
    }

    /** 若已存在会话且拿到新头像，更新本地头像文件（供列表展示）。 */
    public static void syncAvatarIfSessionExists(
            Context context, String peerUserIdHex32, @Nullable byte[] avatarBytes) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return;
        }
        peerUserIdHex32 = normalizePeer32(peerUserIdHex32);
        if (!hasSession(context, peerUserIdHex32)) {
            return;
        }
        writeAvatarFile(context, peerUserIdHex32, avatarBytes);
    }

    public static boolean hasSession(Context context, String peerUserIdHex32) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return false;
        }
        if (ownerHex32(context) == null) {
            return false;
        }
        String want = normalizePeer32(peerUserIdHex32);
        SharedPreferences p = prefs(context);
        if (p.getBoolean(HAS_PREFIX + want, false)) {
            return true;
        }
        Context app = context.getApplicationContext();
        for (String key : p.getAll().keySet()) {
            if (!key.startsWith(HAS_PREFIX)) {
                continue;
            }
            String h = key.substring(HAS_PREFIX.length());
            if (h.length() != 32) {
                continue;
            }
            if (!normalizePeer32(h).equals(want)) {
                continue;
            }
            if (p.getBoolean(key, false)) {
                if (!h.equals(want)) {
                    migratePeerBucketToCanonical(app, p, h, want);
                }
                return true;
            }
        }
        return false;
    }

    /** 已保存的最后一条消息预览（须已存在会话）。 */
    public static String getLastMessagePreview(Context context, String peerUserIdHex32) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return "";
        }
        peerUserIdHex32 = normalizePeer32(peerUserIdHex32);
        if (ownerHex32(context) == null) {
            return "";
        }
        String s = prefs(context).getString(LAST_MSG_PREFIX + peerUserIdHex32, "");
        return s != null ? s : "";
    }

    /** 会话列表里保存的对方显示名（可能为空）。 */
    public static String getPeerDisplayNameStored(Context context, String peerUserIdHex32) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return "";
        }
        peerUserIdHex32 = normalizePeer32(peerUserIdHex32);
        if (ownerHex32(context) == null) {
            return "";
        }
        String n = prefs(context).getString(NAME_PREFIX + peerUserIdHex32, "");
        return n != null ? n : "";
    }

    public static void clearUnread(Context context, String peerUserIdHex32) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return;
        }
        peerUserIdHex32 = normalizePeer32(peerUserIdHex32);
        if (!hasSession(context, peerUserIdHex32)) {
            return;
        }
        prefs(context).edit().putInt(UNREAD_PREFIX + peerUserIdHex32, 0).apply();
    }

    /** 更新列表预览与排序时间（须已存在会话）。 */
    public static void updatePreviewAndTime(
            Context context, String peerUserIdHex32, String lastMessagePreview, long lastTimeMs) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return;
        }
        peerUserIdHex32 = normalizePeer32(peerUserIdHex32);
        if (!hasSession(context, peerUserIdHex32)) {
            return;
        }
        prefs(context)
                .edit()
                .putString(LAST_MSG_PREFIX + peerUserIdHex32, lastMessagePreview != null ? lastMessagePreview : "")
                .putLong(LAST_TIME_PREFIX + peerUserIdHex32, lastTimeMs)
                .apply();
    }

    public static void incrementUnread(Context context, String peerUserIdHex32) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return;
        }
        peerUserIdHex32 = normalizePeer32(peerUserIdHex32);
        if (!hasSession(context, peerUserIdHex32)) {
            return;
        }
        if (isPeerMuted(context, peerUserIdHex32)) {
            return;
        }
        SharedPreferences p = prefs(context);
        int u = p.getInt(UNREAD_PREFIX + peerUserIdHex32, 0);
        p.edit().putInt(UNREAD_PREFIX + peerUserIdHex32, u + 1).apply();
    }

    /** 免打扰：不增加未读角标（仍会同步消息与预览）。 */
    public static boolean isPeerMuted(Context context, String peerUserIdHex32) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return false;
        }
        peerUserIdHex32 = normalizePeer32(peerUserIdHex32);
        if (ownerHex32(context) == null) {
            return false;
        }
        return prefs(context).getBoolean(MUTE_PREFIX + peerUserIdHex32, false);
    }

    public static void setPeerMuted(Context context, String peerUserIdHex32, boolean muted) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return;
        }
        peerUserIdHex32 = normalizePeer32(peerUserIdHex32);
        if (!hasSession(context, peerUserIdHex32)) {
            return;
        }
        prefs(context).edit().putBoolean(MUTE_PREFIX + peerUserIdHex32, muted).apply();
    }

    public static void removeSession(Context context, String peerUserIdHex32) {
        if (peerUserIdHex32 == null || peerUserIdHex32.length() != 32) {
            return;
        }
        peerUserIdHex32 = normalizePeer32(peerUserIdHex32);
        if (ownerHex32(context) == null) {
            return;
        }
        SharedPreferences p = prefs(context);
        p.edit()
                .remove(HAS_PREFIX + peerUserIdHex32)
                .remove(NAME_PREFIX + peerUserIdHex32)
                .remove(LAST_MSG_PREFIX + peerUserIdHex32)
                .remove(LAST_TIME_PREFIX + peerUserIdHex32)
                .remove(UNREAD_PREFIX + peerUserIdHex32)
                .remove(MUTE_PREFIX + peerUserIdHex32)
                .apply();
        File f = avatarFile(context, peerUserIdHex32);
        if (f != null && f.isFile()) {
            //noinspection ResultOfMethodCallIgnored
            f.delete();
        }
    }

    public static List<Row> listSessions(Context context) {
        Context app = context.getApplicationContext();
        if (ownerHex32(context) == null) {
            return Collections.emptyList();
        }
        SharedPreferences p = prefs(context);
        Map<String, ?> all = p.getAll();
        List<Row> out = new ArrayList<>();
        SharedPreferences.Editor mig = null;
        Set<String> seenPeer = new HashSet<>();
        for (Map.Entry<String, ?> e : all.entrySet()) {
            String k = e.getKey();
            if (!k.startsWith(HAS_PREFIX)) {
                continue;
            }
            if (!(e.getValue() instanceof Boolean) || !Boolean.TRUE.equals(e.getValue())) {
                continue;
            }
            String rawHex = k.substring(HAS_PREFIX.length());
            if (rawHex.length() != 32) {
                continue;
            }
            String peerId = normalizePeer32(rawHex);
            if (!rawHex.equals(peerId)) {
                migratePeerBucketToCanonical(app, p, rawHex, peerId);
                p = prefs(context);
            }
            if (!seenPeer.add(peerId)) {
                continue;
            }
            String name = p.getString(NAME_PREFIX + peerId, "");
            if (name == null) {
                name = "";
            }
            boolean need = false;
            long lastTime = p.getLong(LAST_TIME_PREFIX + peerId, 0L);
            if (!p.contains(LAST_TIME_PREFIX + peerId)) {
                lastTime = System.currentTimeMillis();
                need = true;
            }
            String lastMsg = p.getString(LAST_MSG_PREFIX + peerId, "");
            if (!p.contains(LAST_MSG_PREFIX + peerId)) {
                lastMsg = app.getString(R.string.conversation_preview_default);
                need = true;
            }
            if (lastMsg == null) {
                lastMsg = "";
            }
            int unread = p.getInt(UNREAD_PREFIX + peerId, 0);
            if (!p.contains(UNREAD_PREFIX + peerId)) {
                unread = 0;
                need = true;
            }
            if (need) {
                if (mig == null) {
                    mig = p.edit();
                }
                mig.putLong(LAST_TIME_PREFIX + peerId, lastTime)
                        .putString(LAST_MSG_PREFIX + peerId, lastMsg)
                        .putInt(UNREAD_PREFIX + peerId, unread);
            }
            out.add(new Row(peerId, name, lastMsg, lastTime, unread));
        }
        if (mig != null) {
            mig.commit();
        }
        Collections.sort(out, Comparator.comparingLong((Row r) -> r.lastTimeMs).reversed());
        return out;
    }

    private static void writeAvatarFile(Context context, String hex32, @Nullable byte[] avatarBytes) {
        String owner = ownerHex32(context);
        if (owner == null) {
            return;
        }
        Context app = context.getApplicationContext();
        File dir = new File(app.getFilesDir(), AVATAR_SUBDIR + "/" + owner);
        if (!dir.isDirectory() && !dir.mkdirs()) {
            return;
        }
        File f = new File(dir, hex32 + ".bin");
        if (avatarBytes == null || avatarBytes.length == 0) {
            if (f.isFile()) {
                //noinspection ResultOfMethodCallIgnored
                f.delete();
            }
            return;
        }
        try (FileOutputStream os = new FileOutputStream(f)) {
            os.write(avatarBytes);
        } catch (IOException ignored) {
            // keep previous file if any
        }
    }
}
