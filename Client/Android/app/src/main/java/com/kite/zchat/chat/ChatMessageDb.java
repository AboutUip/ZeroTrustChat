package com.kite.zchat.chat;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;

import androidx.annotation.Nullable;

import com.kite.zchat.auth.AuthCredentialStore;

import java.util.Locale;

/**
 * 本地单聊消息（按 peer 32 hex 分表键）；与网关 MM2 的 messageId 去重。
 */
public final class ChatMessageDb extends SQLiteOpenHelper {

    private static final String DB = "zchat_chat_messages.db";
    private static final int VER = 2;

    public static final String T = "t_msg";
    public static final String COL_PEER = "peer_hex";
    public static final String COL_MSG_ID = "msg_id_hex";
    public static final String COL_TEXT = "text_utf8";
    public static final String COL_OUT = "outgoing";
    public static final String COL_TS = "ts_ms";

    public ChatMessageDb(Context context) {
        super(context.getApplicationContext(), DB, null, VER);
    }

    /** 删除本地库（切换账号、登出时调用，避免沿用上一位用户的 messageId 做增量 SYNC）。 */
    public static void wipe(Context context) {
        Context app = context.getApplicationContext();
        app.deleteDatabase(DB);
    }

    @Override
    public void onCreate(SQLiteDatabase db) {
        db.execSQL(
                "CREATE TABLE IF NOT EXISTS "
                        + T
                        + " (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                        + COL_PEER
                        + " TEXT NOT NULL, "
                        + COL_MSG_ID
                        + " TEXT NOT NULL, "
                        + COL_TEXT
                        + " TEXT NOT NULL, "
                        + COL_OUT
                        + " INTEGER NOT NULL, "
                        + COL_TS
                        + " INTEGER NOT NULL, UNIQUE("
                        + COL_PEER
                        + ","
                        + COL_MSG_ID
                        + "));");
        db.execSQL(
                "CREATE INDEX IF NOT EXISTS idx_peer_ts ON " + T + "(" + COL_PEER + "," + COL_TS + ");");
    }

    @Override
    public void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion) {
        if (oldVersion < 2) {
            db.execSQL("UPDATE " + T + " SET " + COL_PEER + " = lower(" + COL_PEER + ")");
        }
    }

    private static String normPeer(String peerHex32) {
        if (peerHex32 == null || peerHex32.length() != 32) {
            return peerHex32;
        }
        return peerHex32.trim().toLowerCase(Locale.ROOT);
    }

    /** @return true 新插入 */
    public boolean insertIfAbsent(
            String peerHex32,
            String msgIdHex32,
            String text,
            boolean outgoing,
            long tsMs) {
        peerHex32 = normPeer(peerHex32);
        if (peerHex32 == null
                || peerHex32.length() != 32
                || msgIdHex32 == null
                || msgIdHex32.length() != 32) {
            return false;
        }
        SQLiteDatabase db = getWritableDatabase();
        ContentValues v = new ContentValues();
        v.put(COL_PEER, peerHex32);
        v.put(COL_MSG_ID, msgIdHex32);
        v.put(COL_TEXT, text != null ? text : "");
        v.put(COL_OUT, outgoing ? 1 : 0);
        v.put(COL_TS, tsMs);
        long r = db.insertWithOnConflict(T, null, v, SQLiteDatabase.CONFLICT_IGNORE);
        return r != -1L;
    }

    public void deleteMessagesForPeer(String peerHex32) {
        peerHex32 = normPeer(peerHex32);
        if (peerHex32 == null || peerHex32.length() != 32) {
            return;
        }
        SQLiteDatabase db = getWritableDatabase();
        db.delete(T, COL_PEER + "=?", new String[] {peerHex32});
    }

    public static final class Row {
        public final long id;
        /** 服务端 messageId 32 hex；引用回复时需要。 */
        public final String msgIdHex;
        public final String text;
        public final boolean outgoing;
        public final long tsMs;

        public Row(long id, String msgIdHex, String text, boolean outgoing, long tsMs) {
            this.id = id;
            this.msgIdHex = msgIdHex != null ? msgIdHex : "";
            this.text = text;
            this.outgoing = outgoing;
            this.tsMs = tsMs;
        }
    }

    public java.util.List<Row> listForPeer(String peerHex32) {
        java.util.ArrayList<Row> out = new java.util.ArrayList<>();
        peerHex32 = normPeer(peerHex32);
        if (peerHex32 == null || peerHex32.length() != 32) {
            return out;
        }
        SQLiteDatabase db = getReadableDatabase();
        try (Cursor c =
                db.query(
                        T,
                        new String[] {"id", COL_MSG_ID, COL_TEXT, COL_OUT, COL_TS},
                        COL_PEER + "=?",
                        new String[] {peerHex32},
                        null,
                        null,
                        COL_TS + " ASC")) {
            while (c.moveToNext()) {
                out.add(
                        new Row(
                                c.getLong(0),
                                c.getString(1),
                                c.getString(2),
                                c.getInt(3) != 0,
                                c.getLong(4)));
            }
        }
        return out;
    }

    /**
     * 增量 SYNC 游标：取本地「最后插入」一条的 messageId（与 sqlite 自增 id 一致），避免用 ts_ms
     *（插入时刻）误选游标导致服务端 after-id 链断裂、中间消息永远拉不到。
     */
    @Nullable
    public byte[] getLastMsgId16ForPeer(String peerHex32) {
        peerHex32 = normPeer(peerHex32);
        if (peerHex32 == null || peerHex32.length() != 32) {
            return null;
        }
        SQLiteDatabase db = getReadableDatabase();
        try (Cursor c =
                db.query(
                        T,
                        new String[] {COL_MSG_ID},
                        COL_PEER + "=?",
                        new String[] {peerHex32},
                        null,
                        null,
                        "id DESC",
                        "1")) {
            if (c.moveToFirst()) {
                String hex = c.getString(0);
                byte[] id = AuthCredentialStore.hexToBytes(hex);
                return id.length == 16 ? id : null;
            }
        }
        return null;
    }

    /** 用于向前补齐缺口：服务端返回会话内该 id 之后的下一段消息。 */
    @Nullable
    public byte[] getOldestMsgId16ForPeer(String peerHex32) {
        peerHex32 = normPeer(peerHex32);
        if (peerHex32 == null || peerHex32.length() != 32) {
            return null;
        }
        SQLiteDatabase db = getReadableDatabase();
        try (Cursor c =
                db.query(
                        T,
                        new String[] {COL_MSG_ID},
                        COL_PEER + "=?",
                        new String[] {peerHex32},
                        null,
                        null,
                        "id ASC",
                        "1")) {
            if (c.moveToFirst()) {
                String hex = c.getString(0);
                byte[] id = AuthCredentialStore.hexToBytes(hex);
                return id.length == 16 ? id : null;
            }
        }
        return null;
    }
}
