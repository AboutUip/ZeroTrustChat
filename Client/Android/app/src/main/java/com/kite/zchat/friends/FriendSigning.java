package com.kite.zchat.friends;

import org.bouncycastle.crypto.AsymmetricCipherKeyPair;
import org.bouncycastle.crypto.generators.Ed25519KeyPairGenerator;
import org.bouncycastle.crypto.params.Ed25519KeyGenerationParameters;
import org.bouncycastle.crypto.params.Ed25519PrivateKeyParameters;
import org.bouncycastle.crypto.params.Ed25519PublicKeyParameters;
import org.bouncycastle.crypto.signers.Ed25519Signer;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;

/** Canonical v1 载荷与 C++ {@code FriendVerificationManager} 一致。 */
public final class FriendSigning {

    private static final byte[] DOMAIN_SEND =
            "ZChatIM|SendFriendRequest|v1".getBytes(StandardCharsets.UTF_8);
    private static final byte[] DOMAIN_RESPOND =
            "ZChatIM|RespondFriendRequest|v1".getBytes(StandardCharsets.UTF_8);
    private static final byte[] DOMAIN_DELETE =
            "ZChatIM|DeleteFriend|v1".getBytes(StandardCharsets.UTF_8);

    private FriendSigning() {}

    public static AsymmetricCipherKeyPair generateKeyPair() {
        Ed25519KeyPairGenerator gen = new Ed25519KeyPairGenerator();
        gen.init(new Ed25519KeyGenerationParameters(new SecureRandom()));
        return gen.generateKeyPair();
    }

    public static byte[] getPublicKeyBytes(Ed25519PrivateKeyParameters sk) {
        Ed25519PublicKeyParameters pk = sk.generatePublicKey();
        return pk.getEncoded();
    }

    public static byte[] signSendFriendRequest(
            Ed25519PrivateKeyParameters sk, byte[] from16, byte[] to16, long timestampSeconds) {
        byte[] msg = buildSendSignMessage(from16, to16, timestampSeconds);
        return sign(sk, msg);
    }

    public static byte[] signRespondFriendRequest(
            Ed25519PrivateKeyParameters sk,
            byte[] requestId16,
            boolean accept,
            byte[] responderId16,
            long timestampSeconds) {
        byte[] msg = buildRespondSignMessage(requestId16, accept, responderId16, timestampSeconds);
        return sign(sk, msg);
    }

    public static byte[] buildFriendRequestWire(
            byte[] from16, byte[] to16, long timestampSeconds, byte[] signature64) {
        ByteBuffer buf = ByteBuffer.allocate(16 + 16 + 8 + 64).order(ByteOrder.BIG_ENDIAN);
        buf.put(from16);
        buf.put(to16);
        buf.putLong(timestampSeconds);
        buf.put(signature64);
        return buf.array();
    }

    public static byte[] buildFriendResponseWire(
            byte[] requestId16, boolean accept, byte[] responderId16, long timestampSeconds, byte[] signature64) {
        ByteBuffer buf = ByteBuffer.allocate(16 + 1 + 16 + 8 + 64).order(ByteOrder.BIG_ENDIAN);
        buf.put(requestId16);
        buf.put((byte) (accept ? 1 : 0));
        buf.put(responderId16);
        buf.putLong(timestampSeconds);
        buf.put(signature64);
        return buf.array();
    }

    public static byte[] signDeleteFriend(
            Ed25519PrivateKeyParameters sk, byte[] selfUserId16, byte[] friendUserId16, long timestampSeconds) {
        byte[] msg = buildDeleteSignMessage(selfUserId16, friendUserId16, timestampSeconds);
        return sign(sk, msg);
    }

    /** ZSP DELETE_FRIEND 明文载荷：userId(16)‖friendId(16)‖timestamp(u64 BE)‖signature(64)。 */
    public static byte[] buildDeleteFriendWire(
            byte[] selfUserId16, byte[] friendUserId16, long timestampSeconds, byte[] signature64) {
        ByteBuffer buf = ByteBuffer.allocate(16 + 16 + 8 + 64).order(ByteOrder.BIG_ENDIAN);
        buf.put(selfUserId16);
        buf.put(friendUserId16);
        buf.putLong(timestampSeconds);
        buf.put(signature64);
        return buf.array();
    }

    private static byte[] buildDeleteSignMessage(byte[] self16, byte[] friend16, long timestampSeconds) {
        ByteBuffer buf =
                ByteBuffer.allocate(DOMAIN_DELETE.length + 16 + 16 + 8).order(ByteOrder.BIG_ENDIAN);
        buf.put(DOMAIN_DELETE);
        buf.put(self16);
        buf.put(friend16);
        buf.putLong(timestampSeconds);
        return buf.array();
    }

    private static byte[] buildSendSignMessage(byte[] from16, byte[] to16, long timestampSeconds) {
        ByteBuffer buf =
                ByteBuffer.allocate(DOMAIN_SEND.length + 16 + 16 + 8).order(ByteOrder.BIG_ENDIAN);
        buf.put(DOMAIN_SEND);
        buf.put(from16);
        buf.put(to16);
        buf.putLong(timestampSeconds);
        return buf.array();
    }

    private static byte[] buildRespondSignMessage(
            byte[] requestId16, boolean accept, byte[] responderId16, long timestampSeconds) {
        ByteBuffer buf =
                ByteBuffer.allocate(DOMAIN_RESPOND.length + 16 + 1 + 16 + 8).order(ByteOrder.BIG_ENDIAN);
        buf.put(DOMAIN_RESPOND);
        buf.put(requestId16);
        buf.put((byte) (accept ? 1 : 0));
        buf.put(responderId16);
        buf.putLong(timestampSeconds);
        return buf.array();
    }

    private static byte[] sign(Ed25519PrivateKeyParameters sk, byte[] message) {
        Ed25519Signer signer = new Ed25519Signer();
        signer.init(true, sk);
        signer.update(message, 0, message.length);
        return signer.generateSignature();
    }
}
