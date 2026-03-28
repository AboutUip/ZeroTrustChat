package com.ztrust.zchat.im.jni;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Locale;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * 按平台加载 ZChatIM JNI：Windows 为 {@code ZChatIMJNI.dll}，Linux 为 {@code libZChatIMJNI.so}，macOS 为
 * {@code libZChatIMJNI.dylib}。
 *
 * <p>优先级：
 *
 * <ol>
 *   <li>系统属性 {@value #PROP_NATIVE_DIR}：指向已解压目录，按 {@link #loadFromDirectory} 顺序 {@link
 *       System#load(String)}（可显式控制依赖库顺序）。
 *   <li>类路径资源 {@code /native/&lt;subdir&gt;/}：Maven 模块下为 {@code src/main/resources/native/&lt;subdir&gt;/}
 *       （例如 Linux：{@code native/linux-x64/}，须含 {@link #LINUX_SO_ORDER} 所列文件名，见该目录内 {@code README.md}；
 *       可用 {@code ZChatServer/scripts/populate-linux-native-resources.sh} 填充）。
 *   <li>{@link System#loadLibrary(String)} 名称 {@code ZChatIMJNI}（依赖 {@code java.library.path} /
 *       {@code LD_LIBRARY_PATH}，Linux 上实际文件名为 {@code libZChatIMJNI.so}）。
 * </ol>
 */
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

    /**
     * 与常见 Ubuntu/Debian OpenSSL 3 动态链一致；若你的 .so 静态链 OpenSSL，可只保留最后一项并把前两项从目录中省略。
     */
    private static final String[] LINUX_SO_ORDER = {
            "libcrypto.so.3",
            "libssl.so.3",
            "libZChatIMJNI.so",
    };

    private static final String[] MACOS_DYLIB_ORDER = {
            "libcrypto.3.dylib",
            "libssl.3.dylib",
            "libZChatIMJNI.dylib",
    };

    public static final String PROP_NATIVE_DIR = "zchat.native.dir";

    private NativeLibraryLoader() {}

    public static void load() {
        Kind kind = detect();
        String override = System.getProperty(PROP_NATIVE_DIR);
        if (override != null && !override.isBlank()) {
            Path dir = Path.of(override.trim());
            switch (kind) {
                case WINDOWS_AMD64:
                    loadFromDirectory(dir, WINDOWS_DLL_ORDER);
                    return;
                case LINUX_AMD64:
                    loadFromDirectory(dir, LINUX_SO_ORDER);
                    return;
                case MACOS:
                    loadFromDirectory(dir, MACOS_DYLIB_ORDER);
                    return;
                default:
                    loadFromDirectory(dir, guessOrderForUnknown(kind));
                    return;
            }
        }

        if (kind == Kind.WINDOWS_AMD64) {
            loadBundledFromClasspath("windows-x64", WINDOWS_DLL_ORDER);
            return;
        }
        if (kind == Kind.LINUX_AMD64) {
            if (tryLoadBundled("linux-x64", LINUX_SO_ORDER)) {
                return;
            }
            LOG.log(Level.INFO, "Native: no bundled linux-x64; loadLibrary(ZChatIMJNI), os={0}", System.getProperty("os.name"));
            System.loadLibrary("ZChatIMJNI");
            return;
        }
        if (kind == Kind.MACOS) {
            if (tryLoadBundled("macos-x64", MACOS_DYLIB_ORDER)) {
                return;
            }
            LOG.log(Level.INFO, "Native: no bundled macos-x64; loadLibrary(ZChatIMJNI), os={0}", System.getProperty("os.name"));
            System.loadLibrary("ZChatIMJNI");
            return;
        }

        LOG.log(Level.INFO, "Native: loadLibrary(ZChatIMJNI), os={0}, arch={1}", new Object[] {
            System.getProperty("os.name"), System.getProperty("os.arch")
        });
        System.loadLibrary("ZChatIMJNI");
    }

    private static String[] guessOrderForUnknown(Kind kind) {
        if (kind == Kind.WINDOWS_AMD64) {
            return WINDOWS_DLL_ORDER;
        }
        if (kind == Kind.LINUX_AMD64) {
            return LINUX_SO_ORDER;
        }
        if (kind == Kind.MACOS) {
            return MACOS_DYLIB_ORDER;
        }
        return new String[] {"libZChatIMJNI.so"};
    }

    private enum Kind {
        WINDOWS_AMD64,
        LINUX_AMD64,
        /** Apple Silicon / Intel 均走同一加载顺序；Homebrew OpenSSL 名称因版本可能需调整常量。 */
        MACOS,
        OTHER
    }

    private static Kind detect() {
        String os = System.getProperty("os.name", "").toLowerCase(Locale.ROOT);
        String arch = System.getProperty("os.arch", "").toLowerCase(Locale.ROOT);
        boolean amd64 = arch.contains("amd64") || arch.contains("x86_64");
        boolean arm64 = arch.contains("aarch64") || arch.contains("arm64");
        if (os.contains("win") && amd64) {
            return Kind.WINDOWS_AMD64;
        }
        if (os.contains("linux") && amd64) {
            return Kind.LINUX_AMD64;
        }
        if (os.contains("mac") && (amd64 || arm64)) {
            return Kind.MACOS;
        }
        return Kind.OTHER;
    }

    /** @return true 若类路径上资源齐全并已加载 */
    private static boolean tryLoadBundled(String resourceSubdir, String[] fileNames) {
        for (String name : fileNames) {
            String cp = "/native/" + resourceSubdir + "/" + name;
            if (NativeLibraryLoader.class.getResource(cp) == null) {
                return false;
            }
        }
        loadBundledFromClasspath(resourceSubdir, fileNames);
        return true;
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
