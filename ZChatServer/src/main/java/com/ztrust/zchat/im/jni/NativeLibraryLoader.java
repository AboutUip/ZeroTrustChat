package com.ztrust.zchat.im.jni;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Locale;
import java.util.logging.Level;
import java.util.logging.Logger;

public final class NativeLibraryLoader {

    private static final Logger LOG = Logger.getLogger(NativeLibraryLoader.class.getName());

    private static final String[] WINDOWS_DLL_ORDER = {
            "libcrypto-3-x64.dll",
            "libssl-3-x64.dll",
            "libsodium.dll",
            "capi.dll",
            "legacy.dll",
            "loader_attic.dll",
            "padlock.dll",
            "ZChatIMJNI.dll",
    };

    private static final String PROP_NATIVE_DIR = "zchat.native.dir";

    private NativeLibraryLoader() {}

    public static void load() {
        Kind kind = detect();
        if (kind == Kind.WINDOWS_AMD64) {
            String override = System.getProperty(PROP_NATIVE_DIR);
            if (override != null && !override.isBlank()) {
                loadFromDirectory(Path.of(override.trim()), WINDOWS_DLL_ORDER);
            } else {
                loadBundledFromClasspath("windows-x64", WINDOWS_DLL_ORDER);
            }
            return;
        }
        LOG.log(Level.INFO, "Native: fallback loadLibrary(ZChatIMJNI), os={0}", System.getProperty("os.name"));
        System.loadLibrary("ZChatIMJNI");
    }

    private enum Kind {
        WINDOWS_AMD64,
        OTHER
    }

    private static Kind detect() {
        String os = System.getProperty("os.name", "").toLowerCase(Locale.ROOT);
        String arch = System.getProperty("os.arch", "").toLowerCase(Locale.ROOT);
        boolean amd64 = arch.contains("amd64") || arch.contains("x86_64");
        if (os.contains("win") && amd64) {
            return Kind.WINDOWS_AMD64;
        }
        return Kind.OTHER;
    }

    private static void loadBundledFromClasspath(String resourceSubdir, String[] fileNames) {
        try {
            Path dir = Files.createTempDirectory("zchat-native-");
            dir.toFile().deleteOnExit();
            for (String name : fileNames) {
                String cp = "/native/" + resourceSubdir + "/" + name;
                Path out = dir.resolve(name);
                try (InputStream in = NativeLibraryLoader.class.getResourceAsStream(cp)) {
                    if (in == null) {
                        throw new IOException("Missing classpath resource: " + cp);
                    }
                    Files.copy(in, out, StandardCopyOption.REPLACE_EXISTING);
                }
                out.toFile().deleteOnExit();
                System.load(out.toAbsolutePath().toString());
            }
        } catch (IOException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    private static void loadFromDirectory(Path dir, String[] fileNames) {
        if (!Files.isDirectory(dir)) {
            throw new ExceptionInInitializerError("Not a directory: " + dir.toAbsolutePath());
        }
        try {
            for (String name : fileNames) {
                Path p = dir.resolve(name);
                if (!Files.isRegularFile(p)) {
                    throw new IOException("Missing file: " + p.toAbsolutePath());
                }
                System.load(p.toAbsolutePath().toString());
            }
        } catch (IOException e) {
            throw new ExceptionInInitializerError(e);
        }
    }
}
