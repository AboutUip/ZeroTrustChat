package com.yhj.zchat.bean.Agreement;

import lombok.Data;

/**
 * @FileName Meta
 * @Author Yihaojun
 * @date 2026-03-21
 * 元数据 - 变长结构
 **/
@Data
public class ZMeta {
    // Meta总长度
    private short metaLength;
    // 时间戳(毫秒)
    private long timestamp;
    // 随机数 12字节
    private byte[] nonce;
    // 密钥ID
    private int keyId;
}
