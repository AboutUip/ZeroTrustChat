package com.kite.zchat.profile;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.widget.ImageView;

import androidx.annotation.DrawableRes;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * 本机头像缓存：按 userId（32 hex）分文件；可与服务端 {@code USER_AVATAR_GET} / {@code USER_PROFILE_GET} 同步后写入。
 */
public final class LocalProfileStore {

    private static final int MAX_SIDE_PX = 1024;
    private static final int JPEG_QUALITY = 88;

    private LocalProfileStore() {}

    public static byte[] readAvatarFileBytes(Context context, String userIdHex32) {
        File f = avatarFileForUserId(context, userIdHex32);
        if (f == null || !f.isFile() || f.length() <= 0) {
            return null;
        }
        int len = (int) f.length();
        byte[] buf = new byte[len];
        try (FileInputStream in = new FileInputStream(f)) {
            int n = in.read(buf);
            if (n != len) {
                return null;
            }
            return buf;
        } catch (IOException e) {
            return null;
        }
    }

    public static void writeAvatarBytes(Context context, String userIdHex32, byte[] data) throws IOException {
        if (data == null || data.length == 0) {
            clearAvatar(context, userIdHex32);
            return;
        }
        File out = avatarFileForUserId(context, userIdHex32);
        if (out == null) {
            throw new IOException("userId");
        }
        try (FileOutputStream os = new FileOutputStream(out)) {
            os.write(data);
        }
    }

    public static File avatarFileForUserId(Context context, String userIdHex32) {
        if (userIdHex32 == null || userIdHex32.length() != 32) {
            return null;
        }
        return new File(context.getFilesDir(), "avatar_" + userIdHex32 + ".jpg");
    }

    public static boolean hasCustomAvatar(Context context, String userIdHex32) {
        File f = avatarFileForUserId(context, userIdHex32);
        return f != null && f.isFile() && f.length() > 0L;
    }

    public static void loadAvatarInto(ImageView imageView, String userIdHex32, @DrawableRes int defaultDrawable) {
        Context ctx = imageView.getContext();
        if (hasCustomAvatar(ctx, userIdHex32)) {
            File f = avatarFileForUserId(ctx, userIdHex32);
            BitmapFactory.Options opts = new BitmapFactory.Options();
            opts.inSampleSize = computeSampleSize(f, 512);
            Bitmap bmp = BitmapFactory.decodeFile(f.getAbsolutePath(), opts);
            if (bmp != null) {
                imageView.setImageTintList(null);
                imageView.setImageBitmap(bmp);
                return;
            }
        }
        imageView.setImageResource(defaultDrawable);
    }

    private static int computeSampleSize(File f, int maxSide) {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(f.getAbsolutePath(), bounds);
        int w = bounds.outWidth;
        int h = bounds.outHeight;
        if (w <= 0 || h <= 0) {
            return 1;
        }
        int inSampleSize = 1;
        while (Math.max(w, h) / inSampleSize > maxSide) {
            inSampleSize *= 2;
        }
        return Math.max(1, inSampleSize);
    }

    public static void loadPreviewFromUri(ImageView imageView, Uri uri, @DrawableRes int defaultDrawable) {
        if (uri == null) {
            imageView.setImageResource(defaultDrawable);
            return;
        }
        try {
            Bitmap bmp = decodeAndScale(imageView.getContext(), uri, 512);
            if (bmp != null) {
                imageView.setImageTintList(null);
                imageView.setImageBitmap(bmp);
            } else {
                imageView.setImageResource(defaultDrawable);
            }
        } catch (IOException e) {
            imageView.setImageResource(defaultDrawable);
        }
    }

    public static void saveAvatarFromUri(Context context, Uri uri, String userIdHex32) throws IOException {
        Bitmap bmp = decodeAndScale(context, uri, MAX_SIDE_PX);
        if (bmp == null) {
            throw new IOException("decode");
        }
        File out = avatarFileForUserId(context, userIdHex32);
        if (out == null) {
            bmp.recycle();
            throw new IOException("userId");
        }
        try (FileOutputStream os = new FileOutputStream(out)) {
            if (!bmp.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, os)) {
                throw new IOException("compress");
            }
        } finally {
            bmp.recycle();
        }
    }

    public static void clearAvatar(Context context, String userIdHex32) {
        File f = avatarFileForUserId(context, userIdHex32);
        if (f != null && f.exists()) {
            // noinspection ResultOfMethodCallIgnored
            f.delete();
        }
    }

    private static Bitmap decodeAndScale(Context context, Uri uri, int maxSide) throws IOException {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        try (InputStream in = context.getContentResolver().openInputStream(uri)) {
            if (in == null) {
                return null;
            }
            BitmapFactory.decodeStream(in, null, bounds);
        }
        int w = bounds.outWidth;
        int h = bounds.outHeight;
        if (w <= 0 || h <= 0) {
            return null;
        }
        int inSampleSize = 1;
        while (Math.max(w, h) / inSampleSize > maxSide) {
            inSampleSize *= 2;
        }
        BitmapFactory.Options opts = new BitmapFactory.Options();
        opts.inSampleSize = Math.max(1, inSampleSize);
        try (InputStream in2 = context.getContentResolver().openInputStream(uri)) {
            if (in2 == null) {
                return null;
            }
            return BitmapFactory.decodeStream(in2, null, opts);
        }
    }
}
