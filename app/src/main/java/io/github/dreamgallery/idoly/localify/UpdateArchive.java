package io.github.dreamgallery.idoly.localify;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.security.MessageDigest;
import java.util.HashSet;
import java.util.Enumeration;
import java.util.Map;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipFile;

/** Archive entries are data: destinations come only from validated manifest items. */
final class UpdateArchive {
    static final class Expected {
        final String sha256;
        final long limit;
        final File destination;
        Expected(String hash, long limit, File destination) {
            this.sha256 = hash; this.limit = limit; this.destination = destination;
        }
    }

    static void extract(File archive, Map<String, Expected> expected, long totalLimit) throws Exception {
        if (expected.isEmpty() || totalLimit < 0) {
            throw new IllegalArgumentException("更新压缩包清单或大小限制无效");
        }
        // ZipInputStream alone accepts archives truncated before their central
        // directory. Verify the container first, then stream to enforce CRC and limits.
        Set<String> listed = new HashSet<>();
        try (ZipFile zip = new ZipFile(archive)) {
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                Expected item = expected.get(entry.getName());
                if (entry.isDirectory() || item == null || !listed.add(entry.getName()) ||
                        entry.getSize() > item.limit) {
                    throw new IllegalArgumentException("更新压缩包目录与清单不一致");
                }
            }
        }
        if (listed.size() != expected.size()) {
            throw new IllegalArgumentException("更新压缩包缺少清单中的文件");
        }
        Set<String> seen = new HashSet<>();
        long total = 0;
        try (ZipInputStream input = new ZipInputStream(new FileInputStream(archive))) {
            ZipEntry entry;
            byte[] buffer = new byte[32 * 1024];
            while ((entry = input.getNextEntry()) != null) {
                Expected item = expected.get(entry.getName());
                if (entry.isDirectory() || item == null || !seen.add(entry.getName())) {
                    throw new IllegalArgumentException("更新压缩包包含重复或未列入清单的文件");
                }
                if (entry.getSize() > item.limit) {
                    throw new IllegalArgumentException("更新压缩包文件超过大小限制");
                }
                MessageDigest digest = MessageDigest.getInstance("SHA-256");
                long size = 0;
                try (FileOutputStream output = item.destination == null ? null : new FileOutputStream(item.destination)) {
                    int count;
                    while ((count = input.read(buffer)) != -1) {
                        size += count;
                        total += count;
                        if (size > item.limit || total > totalLimit) {
                            throw new IllegalArgumentException("更新压缩包解压大小超过限制");
                        }
                        digest.update(buffer, 0, count);
                        if (output != null) output.write(buffer, 0, count);
                    }
                    if (output != null) output.getFD().sync();
                }
                if (!hex(digest.digest()).equals(item.sha256)) {
                    throw new IllegalArgumentException("更新压缩包文件 SHA256 不匹配");
                }
                input.closeEntry();
            }
        }
        if (seen.size() != expected.size()) {
            throw new IllegalArgumentException("更新压缩包缺少清单中的文件");
        }
    }

    private static String hex(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) result.append(String.format(java.util.Locale.ROOT, "%02x", value & 255));
        return result.toString();
    }
}
