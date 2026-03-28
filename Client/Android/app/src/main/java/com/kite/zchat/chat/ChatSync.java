package com.kite.zchat.chat;

import android.content.Context;
import android.content.Intent;

import com.kite.zchat.auth.AuthCredentialStore;
import com.kite.zchat.call.ChatCallLogHelper;
import com.kite.zchat.call.VoiceCallCoordinator;
import com.kite.zchat.call.VoiceCallEngine;
import com.kite.zchat.call.WebRtcSignaling;
import com.kite.zchat.conversation.ConversationPlaceholderStore;
import com.kite.zchat.friends.FriendZspHelper;
import com.kite.zchat.zsp.ZspChatWire;
import com.kite.zchat.zsp.ZspSessionManager;

import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/** 通过 ZSP SYNC 拉取单聊消息并写入本地库与会话预览。 */
public final class ChatSync {

    private static final int SYNC_PAGE = 64;
    private static final int MAX_TAIL_ROUNDS = 8;
    private static final int MAX_GAP_ROUNDS = 8;

    private ChatSync() {}

    public static void scheduleSyncFromIncomingPush(Context context, String host, int port, byte[] textPayload) {
        if (textPayload == null || textPayload.length < 32) {
            return;
        }
        AuthCredentialStore creds = AuthCredentialStore.create(context);
        byte[] self = creds.getUserIdBytes();
        if (self.length != 16) {
            return;
        }
        byte[] im = Arrays.copyOfRange(textPayload, 0, 16);
        byte[] peer = PeerImSession.peerFromSessionId(im, self);
        String peerHex = AuthCredentialStore.bytesToHex(peer);
        if (peerHex.length() != 32) {
            return;
        }
        syncPeer(context.getApplicationContext(), host, port, peerHex, true);
    }

    /**
     * 依次同步通信列表中所有会话（用于下拉刷新）。
     *
     * @param mergeServerHeadWindow 为 true 时每个会话先合并服务端「首窗」消息，修复仅持有尾部 id 时中间永远缺消息的问题。
     * @return 无会话时 true；有会话时仅当每次 {@link #syncPeer} 均成功才为 true（含服务端返回 0 条新消息）。
     */
    public static boolean syncAllOpenSessions(
            Context context, String host, int port, boolean mergeServerHeadWindow) {
        if (host == null || host.isBlank() || port <= 0) {
            return false;
        }
        Context app = context.getApplicationContext();
        List<ConversationPlaceholderStore.Row> rows = ConversationPlaceholderStore.listSessions(app);
        if (rows.isEmpty()) {
            return true;
        }
        boolean allOk = true;
        for (ConversationPlaceholderStore.Row r : rows) {
            if (r.peerUserIdHex32 != null && r.peerUserIdHex32.length() == 32) {
                if (!syncPeer(app, host, port, r.peerUserIdHex32, mergeServerHeadWindow)) {
                    allOk = false;
                }
            }
        }
        return allOk;
    }

    /** 后台轮询：不拉首窗，降低流量；依赖打开会话与下拉刷新做深度修复。 */
    public static boolean syncAllOpenSessions(Context context, String host, int port) {
        return syncAllOpenSessions(context, host, port, false);
    }

    /** @return 会话建立且 SYNC 请求得到应答（含 0 条新消息）时为 true */
    public static boolean syncPeer(Context app, String host, int port, String peerHex32) {
        return syncPeer(app, host, port, peerHex32, false);
    }

    public static boolean syncPeer(
            Context app, String host, int port, String peerHex32, boolean mergeServerHeadWindow) {
        if (host == null || host.isBlank() || port <= 0 || peerHex32 == null) {
            return false;
        }
        peerHex32 = peerHex32.trim().toLowerCase(Locale.ROOT);
        if (peerHex32.length() != 32) {
            return false;
        }
        if (!FriendZspHelper.ensureSession(app, host, port)) {
            return false;
        }
        AuthCredentialStore creds = AuthCredentialStore.create(app);
        byte[] self = creds.getUserIdBytes();
        byte[] peer = AuthCredentialStore.hexToBytes(peerHex32);
        if (self.length != 16 || peer.length != 16) {
            return false;
        }
        byte[] im = PeerImSession.deriveSessionId(self, peer);
        ChatMessageDb db = new ChatMessageDb(app);
        TsSeq tsSeq = new TsSeq();

        int inserted = 0;
        if (mergeServerHeadWindow) {
            byte[] headRaw =
                    ZspSessionManager.get()
                            .syncSessionMessages(ZspChatWire.buildSyncPayloadInitial(im));
            if (headRaw == null) {
                return false;
            }
            inserted += ingestParsedSyncResponse(app, db, peerHex32, peer, headRaw, tsSeq);
        }

        int gapIns;
        boolean okGap;
        {
            int[] acc = {0};
            okGap = runGapFillForward(app, db, im, peerHex32, peer, acc, tsSeq);
            gapIns = acc[0];
        }
        inserted += gapIns;

        int tailIns;
        boolean okTail;
        {
            int[] acc = {0};
            okTail = runTailCatchUp(app, db, im, peerHex32, peer, acc, tsSeq);
            tailIns = acc[0];
        }
        inserted += tailIns;

        if (inserted > 0) {
            refreshConversationPreviewFromDb(app, db, peerHex32);
            broadcast(app, ChatEvents.ACTION_CONVERSATION_LIST_CHANGED, null);
            broadcast(app, ChatEvents.ACTION_CHAT_MESSAGES_CHANGED, peerHex32);
        }
        return okTail && okGap;
    }

    private static void refreshConversationPreviewFromDb(
            Context app, ChatMessageDb db, String peerHex32) {
        List<ChatMessageDb.Row> rows = db.listForPeer(peerHex32);
        if (rows.isEmpty()) {
            return;
        }
        ChatMessageDb.Row last = rows.get(rows.size() - 1);
        ConversationPlaceholderStore.updatePreviewAndTime(
                app,
                peerHex32,
                previewLine(ChatReplyCodec.stripForPreview(last.text)),
                last.tsMs);
    }

    /** 同一轮 SYNC 内多批插入时递增时间戳，避免 ORDER BY ts 时顺序错乱。 */
    private static final class TsSeq {
        private final long base = System.currentTimeMillis();
        private int seq;

        long next() {
            return base + (++seq);
        }
    }

    /** @return 新插入行数 */
    private static int ingestParsedSyncResponse(
            Context app,
            ChatMessageDb db,
            String peerHex32,
            byte[] peer,
            byte[] raw,
            TsSeq tsSeq) {
        List<ZspChatWire.SyncRow> rows = ZspChatWire.parseSyncResponse(raw);
        if (rows.isEmpty()) {
            return 0;
        }
        return ingestSyncRowsNoBroadcast(app, db, peerHex32, peer, rows, tsSeq);
    }

    /** 以「最新一条」为游标向后拉，直到没有满页（处理一次轮询内多条新消息）。 */
    private static boolean runTailCatchUp(
            Context app,
            ChatMessageDb db,
            byte[] im,
            String peerHex32,
            byte[] peer,
            int[] insertedTotal,
            TsSeq tsSeq) {
        for (int round = 0; round < MAX_TAIL_ROUNDS; round++) {
            byte[] last = db.getLastMsgId16ForPeer(peerHex32);
            byte[] syncPayload;
            if (last == null || isAllZero(last)) {
                syncPayload = ZspChatWire.buildSyncPayloadInitial(im);
            } else {
                syncPayload = ZspChatWire.buildSyncPayloadSince(im, im, last, SYNC_PAGE);
            }
            if (syncPayload.length == 0) {
                return false;
            }
            byte[] raw = ZspSessionManager.get().syncSessionMessages(syncPayload);
            if (raw == null) {
                return false;
            }
            List<ZspChatWire.SyncRow> rows = ZspChatWire.parseSyncResponse(raw);
            if (rows.isEmpty()) {
                break;
            }
            int n = ingestSyncRowsNoBroadcast(app, db, peerHex32, peer, rows, tsSeq);
            insertedTotal[0] += n;
            if (n == 0) {
                break;
            }
            if (rows.size() < SYNC_PAGE) {
                break;
            }
        }
        return true;
    }

    /**
     * 以「最早一条」为游标向前拉，补齐服务端链上早先缺失的条目。若本页全部为已存在的重复行则停止，避免在已连续链上死循环。
     */
    private static boolean runGapFillForward(
            Context app,
            ChatMessageDb db,
            byte[] im,
            String peerHex32,
            byte[] peer,
            int[] insertedTotal,
            TsSeq tsSeq) {
        for (int round = 0; round < MAX_GAP_ROUNDS; round++) {
            byte[] oldest = db.getOldestMsgId16ForPeer(peerHex32);
            if (oldest == null || isAllZero(oldest)) {
                break;
            }
            byte[] syncPayload = ZspChatWire.buildSyncPayloadSince(im, im, oldest, SYNC_PAGE);
            if (syncPayload.length == 0) {
                return false;
            }
            byte[] raw = ZspSessionManager.get().syncSessionMessages(syncPayload);
            if (raw == null) {
                return false;
            }
            List<ZspChatWire.SyncRow> rows = ZspChatWire.parseSyncResponse(raw);
            if (rows.isEmpty()) {
                break;
            }
            int n = ingestSyncRowsNoBroadcast(app, db, peerHex32, peer, rows, tsSeq);
            insertedTotal[0] += n;
            if (n == 0) {
                break;
            }
            if (rows.size() < SYNC_PAGE) {
                break;
            }
        }
        return true;
    }

    /** @return 新插入行数 */
    private static int ingestSyncRowsNoBroadcast(
            Context app,
            ChatMessageDb db,
            String peerHex32,
            byte[] peer,
            List<ZspChatWire.SyncRow> rows,
            TsSeq tsSeq) {
        int inserted = 0;
        String active = ChatActivePeer.getActivePeerHex();
        for (ZspChatWire.SyncRow row : rows) {
            byte[] to = ZspChatWire.extractToUser(row.plainPayload);
            String text = ZspChatWire.extractTextUtf8(row.plainPayload);
            if (WebRtcSignaling.isSignaling(text)) {
                /*
                 * SYNC 会反复带上会话内历史信令。若在 IDLE 时仍派发 offer，挂断/失败后下一次同步会再次
                 * 弹出「来电」，形成死循环。实时 offer 由 ZSP TEXT 监听 {@link
                 * com.kite.zchat.call.VoiceCallEngine#dispatchSignalingFromIncomingTextPayload} 派发。
                 */
                if (WebRtcSignaling.isOfferMessage(text)
                        && VoiceCallCoordinator.canAcceptIncomingOffer()) {
                    // skip replayed offer
                } else {
                    VoiceCallEngine.dispatchSignaling(app, peerHex32, text);
                }
                continue;
            }
            if (ChatCallLogHelper.isCallLog(text)) {
                continue;
            }
            boolean outgoing = Arrays.equals(to, peer);
            String msgHex = AuthCredentialStore.bytesToHex(row.messageId16);
            if (msgHex.length() != 32) {
                continue;
            }
            long ts = tsSeq.next();
            if (db.insertIfAbsent(peerHex32, msgHex, text, outgoing, ts)) {
                inserted++;
                if (!outgoing && (active == null || !active.equalsIgnoreCase(peerHex32))) {
                    ConversationPlaceholderStore.incrementUnread(app, peerHex32);
                }
            }
        }
        return inserted;
    }

    private static boolean isAllZero(byte[] b) {
        if (b == null) {
            return true;
        }
        for (byte x : b) {
            if (x != 0) {
                return false;
            }
        }
        return true;
    }

    private static String previewLine(String t) {
        if (t == null) {
            return "";
        }
        String s = ChatReplyCodec.stripForPreview(t).replace('\n', ' ');
        return s.length() > 80 ? s.substring(0, 80) + "…" : s;
    }

    private static void broadcast(Context app, String action, String peerHex) {
        Intent i = new Intent(action);
        i.setPackage(app.getPackageName());
        if (peerHex != null) {
            i.putExtra(ChatEvents.EXTRA_PEER_HEX, peerHex);
        }
        app.sendBroadcast(i);
    }
}
