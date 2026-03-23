package com.ztrust.zchat.im.zsp;

import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;

import java.util.Optional;

/**
 * 作者：Iverson-易浩军
 * 日期：2026/3/23-下午9:52
 * 描述：
 *   Auth Tag
 *   认证头
 *   ###  AUTH (0x81)
 * ┌───────────────┬─────────────┬─────────────┬─────────────┐
 * │ UserIDLen(2)  │  UserID     │  TokenLen   │   Token     │
 * │               │   (变长)     │    (2)      │   (变长)     │
 *
 * └───────────────┴─────────────┴─────────────┴─────────────┘
 * 收到的是边长字节，这里用字节数组接收
 */
public record ZspAuthTag(byte[] userId,byte[] token) {

    /**
     * 编码为 ByteBuf
     *
     把对象转成字节流，用于网络传输。
     ZspAuthTag 对象          encode()           字节流
     ┌─────────────────┐                    ┌──────────────────────────┐
     │ userId: "abc"   │       ──────►       │ 00 03 61 62 63 00 04 ... │
     │ token: "test"   │                    └──────────────────────────┘
     └─────────────────┘                         通过网络发送
     ZspAuthTag auth = new ZspAuthTag("abc".getBytes(), "test".getBytes());
     ByteBuf buf = auth.encode();
     // buf 内容: 00 03 61 62 63 00 04 74 65 73 74
     //          └──┘ └──────┘ └──┘ └──────┘
     //          len=3  "abc"   len=4  "test"
  encode() 是把 Java 对象打包成字节，准备发出去。
     */
    public ByteBuf encode() {
        ByteBuf buf = Unpooled.buffer(2 + userId.length + 2 + token.length);
        buf.writeShort(userId.length);
        buf.writeBytes(userId);
        buf.writeShort(token.length);
        buf.writeBytes(token);
        return buf;
    }

    /**
     *
     作用：返回纯 byte[]，方便不使用 Netty 的场景。
     ZspAuthTag 对象  ──►  byte[] 数组
     使用场景：
     - 存储到文件
     - 传给不使用 Netty 的代码
     - 日志打印、调试
     * @return
     */
    public byte[] encodeBytes() {
        return encode().array();
    }


    /**
     *
     作用：从网络接收的 ByteBuf 中解析出对象。
     ByteBuf (网络数据)  ──►  ZspAuthTag 对象
     使用场景：
     - Netty Handler 中直接处理网络数据
     - 零拷贝，性能最好
     * @param buf
     * @return
     */
    public static Optional<ZspAuthTag> parse(ByteBuf buf) {
        if (buf == null || buf.readableBytes() < 4) {
            return Optional.empty();
        }
        int userIdLen = buf.readUnsignedShort();
        if (buf.readableBytes() < userIdLen + 2) {
            return Optional.empty();
        }
        byte[] userId = new byte[userIdLen];
        buf.readBytes(userId);
        int tokenLen = buf.readUnsignedShort();
        if (tokenLen > ZspConstants.MAX_TOKEN_LENGTH || buf.readableBytes() < tokenLen) {
            return Optional.empty();
        }
        byte[] token = new byte[tokenLen];
        buf.readBytes(token);
        return Optional.of(new ZspAuthTag(userId, token));
    }

    /**
     *  作用：把普通 byte[] 包装成 ByteBuf 再解析。
     *   byte[] 数组  ──►  ByteBuf  ──►  ZspAuthTag 对象
     *   使用场景：
     *   - 测试代码
     *   - 从文件读取后解析
     *   - 其他非 Netty 场景
     */
    public static Optional<ZspAuthTag> parse(byte[] data) {
        if (data == null) {
            return Optional.empty();
        }
        return parse(Unpooled.wrappedBuffer(data));
    }


}
