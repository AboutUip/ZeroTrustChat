package com.ztrust.zchat.im.zsp.server;

import java.util.Arrays;

/** 16B userId 等作为 Map 键。 */
public final class ZspByteArrayKey {

    private final byte[] value;

    public ZspByteArrayKey(byte[] value) {
        if (value == null) {
            throw new IllegalArgumentException("null key");
        }
        this.value = Arrays.copyOf(value, value.length);
    }

    public byte[] bytes() {
        return Arrays.copyOf(value, value.length);
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof ZspByteArrayKey other)) {
            return false;
        }
        return Arrays.equals(value, other.value);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(value);
    }
}
