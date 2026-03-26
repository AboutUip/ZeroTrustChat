package com.ztrust.zchat.im.zsp;

/**
 * ZSP Header.Flags（见 docs/01-Architecture/02-ZSP-Protocol.md 3.1）。
 */
public final class ZspFlags {

    public static final int COMPRESSED = 1 << 0;
    public static final int ENCRYPTED = 1 << 1;
    public static final int PRIORITY = 1 << 2;
    public static final int MULTI_DEVICE = 1 << 3;

    private ZspFlags() {}

    public static boolean isCompressed(int flags) {
        return (flags & COMPRESSED) != 0;
    }

    public static boolean isEncrypted(int flags) {
        return (flags & ENCRYPTED) != 0;
    }

    public static boolean isPriority(int flags) {
        return (flags & PRIORITY) != 0;
    }

    public static boolean isMultiDevice(int flags) {
        return (flags & MULTI_DEVICE) != 0;
    }
}
