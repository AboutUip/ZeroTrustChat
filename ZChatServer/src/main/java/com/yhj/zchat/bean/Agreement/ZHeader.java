package com.yhj.zchat.bean.Agreement;

import lombok.Data;

/**
 * 作者：Iverson-易浩军
 * 日期：2026/3/20-下午9:29
 * 描述：协议头
 * 自定义协议的本质：把「二进制字节流」 ↔ 「Java 对象」互相转换
 * 没有实体类 = 没有协议标准：后续写编解码器、拆包器、业务处理器，全部依赖这 3 个类
 * 长度：16 字节
 */
@Data
public class ZHeader {
    // 0-1: 魔数 0x5A53
    private short magic;
    // 2: 协议版本
    private byte version;
    // 3: 消息类型 TEXT/FILE/AUTH 等
    private byte messageType;
    // 4: 标志位 加密/压缩/优先级
    private byte flags;
    // 5: 保留字段
    private byte reserved;
    // 6-9: 会话ID
    private int sessionId;
    // 10-13: 序列号
    private int sequence;
    // 14-15: 内容总长度 = Meta + Payload + AuthTag
    private short contentLength;
}
