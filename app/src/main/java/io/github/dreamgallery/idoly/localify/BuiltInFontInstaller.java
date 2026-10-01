package io.github.dreamgallery.idoly.localify;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.Arrays;

/** Installs the module-owned binary independently of user-editable dictionaries. */
final class BuiltInFontInstaller {
    interface Source {
        InputStream open() throws Exception;
    }

    private BuiltInFontInstaller() {}

    static boolean install(Source source, File target) throws Exception {
        byte[] expected;
        try (InputStream input = source.open()) {
            expected = digest(input, null, true);
        }
        if (target.isFile()) {
            try (InputStream input = new FileInputStream(target)) {
                if (Arrays.equals(expected, digest(input, null, false))) return false;
            }
        }

        File temporary = new File(target.getParentFile(), target.getName() + ".installing");
        try {
            // Same-directory staging allows an atomic rename; interrupted staging is
            // overwritten on the next launch, never exposed to the native loader.
            try (InputStream input = source.open();
                 FileOutputStream output = new FileOutputStream(temporary)) {
                if (!Arrays.equals(expected, digest(input, output, true))) {
                    throw new IOException("Module font bundle changed while copying");
                }
                output.getFD().sync();
            }
            try (InputStream input = new FileInputStream(temporary)) {
                if (!Arrays.equals(expected, digest(input, null, true))) {
                    throw new IOException("Copied font bundle checksum mismatch");
                }
            }
            // Do not fall back to a non-atomic replacement: retain the last complete
            // font if this filesystem cannot atomically install the new one.
            Files.move(temporary.toPath(), target.toPath(),
                StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
            return true;
        } finally {
            Files.deleteIfExists(temporary.toPath());
        }
    }

    private static byte[] digest(InputStream input, FileOutputStream output,
                                 boolean requireNonEmpty) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        byte[] buffer = new byte[65536];
        long size = 0;
        int read;
        while ((read = input.read(buffer)) != -1) {
            digest.update(buffer, 0, read);
            if (output != null) output.write(buffer, 0, read);
            size += read;
        }
        if (requireNonEmpty && size == 0) throw new IOException("Empty module font bundle");
        return digest.digest();
    }
}
