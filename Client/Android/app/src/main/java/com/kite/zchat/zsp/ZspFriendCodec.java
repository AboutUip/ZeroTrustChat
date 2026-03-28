package com.kite.zchat.zsp;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

/** 好友申请列表：count(u32 BE) ‖ count×(requestId16‖from16‖createdSec u64 BE)。 */
public final class ZspFriendCodec {

    public static final class PendingRow {
        public final byte[] requestId16;
        public final byte[] fromUserId16;
        public final long createdSec;

        public PendingRow(byte[] requestId16, byte[] fromUserId16, long createdSec) {
            this.requestId16 = requestId16;
            this.fromUserId16 = fromUserId16;
            this.createdSec = createdSec;
        }
    }

    private static final int ROW_BYTES = ZspProtocolConstants.USER_ID_SIZE
            + ZspProtocolConstants.USER_ID_SIZE
            + 8;

    private ZspFriendCodec() {}

    public static List<PendingRow> parsePendingRows(byte[] raw) {
        List<PendingRow> out = new ArrayList<>();
        if (raw == null || raw.length < 4) {
            return out;
        }
        ByteBuffer buf = ByteBuffer.wrap(raw).order(ByteOrder.BIG_ENDIAN);
        int n = buf.getInt();
        for (int i = 0; i < n; i++) {
            if (buf.remaining() < ROW_BYTES) {
                break;
            }
            byte[] rid = new byte[ZspProtocolConstants.USER_ID_SIZE];
            byte[] from = new byte[ZspProtocolConstants.USER_ID_SIZE];
            buf.get(rid);
            buf.get(from);
            long cs = buf.getLong();
            out.add(new PendingRow(rid, from, cs));
        }
        return out;
    }
}
