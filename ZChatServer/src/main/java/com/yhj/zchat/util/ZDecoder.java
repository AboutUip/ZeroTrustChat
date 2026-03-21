package com.yhj.zchat.util;

import com.yhj.zchat.bean.Agreement.ZHeader;
import com.yhj.zchat.bean.Agreement.ZMeta;
import com.yhj.zchat.bean.Agreement.ZPacket;
import io.netty.buffer.ByteBuf;
import io.netty.channel.ChannelHandlerContext;
import io.netty.handler.codec.ByteToMessageDecoder;

import java.util.List;

/**
 * @FileName ZDecoder
 * @Author Yihaojun
 * @date 2026-03-21
 * 协议解码器：二进制 → ZPacket对象
 * 把拆好的二进制ByteBuf，解析成定义的ZPacket/ZHeader/ZMeta对象
 **/
public class ZDecoder extends ByteToMessageDecoder {
    @Override
    protected void decode(ChannelHandlerContext ctx, ByteBuf in, List<Object> out) throws Exception {
        // 1. 解析 16字节 Header
        ZHeader header = new ZHeader();
        header.setMagic(in.readShort());           // 0-1: 魔数 0x5A53
        header.setVersion(in.readByte());          // 2: 版本
        header.setMessageType(in.readByte());      // 3: 消息类型
        header.setFlags(in.readByte());            // 4: 标志位
        header.setReserved(in.readByte());         // 5: 保留
        header.setSessionId(in.readInt());         // 6-9: 会话ID
        header.setSequence(in.readInt());          // 10-13: 序列号
        header.setContentLength(in.readShort());   // 14-15: 内容总长度(Meta+Payload+AuthTag)

        // 2. 解析 Meta（先读 metaLength，再按需读取）
        ZMeta meta = new ZMeta();
        short metaLength = in.readShort();         // Meta的前2字节：Meta总长度
        meta.setMetaLength(metaLength);
        meta.setTimestamp(in.readLong());          // 时间戳 8字节
        byte[] nonce = new byte[12];
        in.readBytes(nonce);                       // Nonce 12字节
        meta.setNonce(nonce);
        meta.setKeyId(in.readInt());               // KeyID 4字节

        // 3. 计算 Payload 长度
        // ContentLength = Meta + Payload + AuthTag
        // Payload长度 = ContentLength - metaLength - 16(AuthTag)
        int payloadLength = header.getContentLength() - metaLength - 16;
        byte[] payload = new byte[payloadLength];
        in.readBytes(payload);

        // 4. 解析 AuthTag（16字节）
        byte[] authTag = new byte[16];
        in.readBytes(authTag);

        // 5. 组装成完整 ZPacket
        ZPacket packet = new ZPacket();
        packet.setHeader(header);
        packet.setMeta(meta);
        packet.setPayload(payload);
        packet.setAuthTag(authTag);

        // 交给后续业务处理器
        out.add(packet);
    }
}
