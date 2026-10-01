package io.github.dreamgallery.idoly.localify;

import java.io.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;

public final class UpdateArchiveTest {
    interface Check { void run() throws Exception; }
    static void rejected(Check check) throws Exception {
        try { check.run(); } catch (IllegalArgumentException | IOException expected) { return; }
        throw new AssertionError("invalid archive accepted");
    }
    static String hash(byte[] data) throws Exception {
        StringBuilder result = new StringBuilder();
        for (byte b : MessageDigest.getInstance("SHA-256").digest(data)) result.append(String.format("%02x", b & 255));
        return result.toString();
    }
    static void zip(Path output, String... names) throws Exception {
        try (ZipOutputStream out = new ZipOutputStream(Files.newOutputStream(output))) {
            for (String name : names) {
                out.putNextEntry(new ZipEntry(name)); out.write(new byte[]{65, 66, 67}); out.closeEntry();
            }
        }
    }
    public static void main(String[] args) throws Exception {
        Path root = Paths.get(args[0]);
        Path archive = root.resolve("update.zip");
        File destination = root.resolve("staged").toFile();
        String digest = hash(new byte[]{65, 66, 67});
        Map<String, UpdateArchive.Expected> expected = new LinkedHashMap<>();
        expected.put("dict.json", new UpdateArchive.Expected(digest, 3, destination));
        expected.put("resource/adv_test.txt", new UpdateArchive.Expected(digest, 3, null));
        zip(archive, "dict.json", "resource/adv_test.txt");
        UpdateArchive.extract(archive.toFile(), expected, 6);
        if (!Arrays.equals(Files.readAllBytes(destination.toPath()), new byte[]{65,66,67})) throw new AssertionError();
        rejected(() -> UpdateArchive.extract(archive.toFile(), expected, 5));
        expected.put("dict.json", new UpdateArchive.Expected(digest, 2, destination));
        rejected(() -> UpdateArchive.extract(archive.toFile(), expected, 6));
        expected.put("dict.json", new UpdateArchive.Expected("0".repeat(64), 3, destination));
        rejected(() -> UpdateArchive.extract(archive.toFile(), expected, 6));
        expected.put("dict.json", new UpdateArchive.Expected(digest, 3, destination));
        zip(archive, "dict.json");
        rejected(() -> UpdateArchive.extract(archive.toFile(), expected, 6));
        zip(archive, "dict.json", "resource/adv_test.txt", "../escape");
        rejected(() -> UpdateArchive.extract(archive.toFile(), expected, 9));
        zip(archive, "dict.json", "dict.jsox");
        byte[] raw = Files.readAllBytes(archive);
        byte[] pattern = "dict.jsox".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        for (int i=0;i <= raw.length-pattern.length;i++) {
            boolean matches=true;
            for (int j=0;j<pattern.length;j++) if(raw[i+j]!=pattern[j]) matches=false;
            if(matches) raw[i+pattern.length-1]='n';
        }
        Files.write(archive, raw);
        rejected(() -> UpdateArchive.extract(archive.toFile(), expected, 6));
        Files.write(archive, new byte[]{1,2,3});
        rejected(() -> UpdateArchive.extract(archive.toFile(), expected, 6));
        zip(archive, "dict.json", "resource/adv_test.txt");
        raw = Files.readAllBytes(archive);
        int central = -1;
        for (int i = 0; i < raw.length - 3; i++) {
            if (raw[i] == 80 && raw[i+1] == 75 && raw[i+2] == 1 && raw[i+3] == 2) { central = i; break; }
        }
        if (central < 0) throw new AssertionError("fixture central directory missing");
        Files.write(archive, Arrays.copyOf(raw, central));
        rejected(() -> UpdateArchive.extract(archive.toFile(), expected, 6));
        zip(archive, "dict.json", "resource/adv_test.txt", "folder/");
        rejected(() -> UpdateArchive.extract(archive.toFile(), expected, 9));
        zip(archive, "dict.json", "resource/adv_test.txt");
        rejected(() -> UpdateArchive.extract(archive.toFile(), Collections.emptyMap(), 6));
        // Stored payload corruption must be rejected even if an attacker changes the
        // manifest hash to match the corrupt bytes: the ZIP CRC remains inconsistent.
        byte[] body = {65,66,67};
        CRC32 crc = new CRC32(); crc.update(body);
        try (ZipOutputStream out = new ZipOutputStream(Files.newOutputStream(archive))) {
            ZipEntry entry = new ZipEntry("dict.json");
            entry.setMethod(ZipEntry.STORED); entry.setSize(3); entry.setCompressedSize(3); entry.setCrc(crc.getValue());
            out.putNextEntry(entry); out.write(body); out.closeEntry();
        }
        raw = Files.readAllBytes(archive);
        raw[30 + "dict.json".length()] = 68;
        Files.write(archive, raw);
        Map<String, UpdateArchive.Expected> corrupt = new LinkedHashMap<>();
        corrupt.put("dict.json", new UpdateArchive.Expected(hash(new byte[]{68,66,67}), 3, null));
        rejected(() -> UpdateArchive.extract(archive.toFile(), corrupt, 3));
        System.out.println("Archive checks passed: valid, unchanged, size, total, hash, missing, traversal, duplicate, invalid ZIP");
    }
}
