package com.yhj.zchat.util;

import io.netty.handler.codec.LengthFieldBasedFrameDecoder;

/**
 * @FileName ZFrameDecoder
 * @Author Yihaojun
 * @date 2026-03-21
 * 拆包器
 * 解决TCP粘包半包，基于Header里的ContentLength字段自动切割完整数据包
 *
 * 【协议结构】
 * Header(16) + Meta(变长) + Payload(变长) + AuthTag(16)
 *
 * 【ContentLength含义】
 * ContentLength = Meta + Payload + AuthTag 的总长度
 * 完整包长度 = Header(16) + ContentLength
 *
 * 【LengthFieldBasedFrameDecoder计算公式】
 * Frame长度 = lengthFieldOffset + lengthFieldLength + lengthAdjustment + lengthFieldValue
 *           = 14 + 2 + 0 + ContentLength
 *           = 16 + ContentLength ✓ 正好等于完整包长度
 **/
public class ZFrameDecoder extends LengthFieldBasedFrameDecoder {
    public ZFrameDecoder() {
        super(
                100 * 1024 * 1024,  // maxFrameLength: 最大100MB
                14,                 // lengthFieldOffset: ContentLength在Header中的偏移量
                2,                  // lengthFieldLength: ContentLength占2字节(short)
                0,                  // lengthAdjustment: 不需要额外调整
                0                   // initialBytesToStrip: 保留完整数据包，不跳过任何字节
        );
    }
}
