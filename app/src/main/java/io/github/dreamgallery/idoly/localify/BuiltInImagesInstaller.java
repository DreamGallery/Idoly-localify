package io.github.dreamgallery.idoly.localify;

import android.content.Context;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import org.json.JSONArray;
import org.json.JSONObject;

/** Installs only verified module-owned atlases; the native loader preserves original UVs. */
final class BuiltInImagesInstaller {
    static void install(Context context, File dataDir) throws Exception {
        File root = new File(dataDir, "images");
        if (Files.isSymbolicLink(root.toPath())) throw new IllegalStateException("Image directory is a link");
        if (!root.isDirectory() && !root.mkdirs()) throw new IllegalStateException("Cannot create image directory");
        byte[] raw;
        try (InputStream input = HookEntry.openBuiltInAsset(context, "images/manifest.json")) {
            raw = read(input, 128 * 1024);
        }
        JSONObject manifest = new JSONObject(new String(raw, java.nio.charset.StandardCharsets.UTF_8));
        if (manifest.getInt("schema_version") != 1) throw new IllegalStateException("Invalid image manifest");
        JSONArray textures = manifest.getJSONArray("textures");
        if (textures.length() > 64) throw new IllegalStateException("Too many image atlases");
        for (int i = 0; i < textures.length(); i++) {
            JSONObject entry = textures.getJSONObject(i);
            String name = entry.getString("file"), expected = entry.getString("sha256");
            if (!name.matches("atlas-[0-9]+\\.png") || !expected.matches("[0-9a-f]{64}"))
                throw new IllegalStateException("Invalid image entry");
            File target = new File(root, name);
            if (Files.isSymbolicLink(target.toPath())) throw new IllegalStateException("Image is a link");
            if (target.isFile() && target.length() <= 32 * 1024 * 1024) {
                try (InputStream input = new java.io.FileInputStream(target)) {
                    if (expected.equals(hash(read(input, 32 * 1024 * 1024)))) continue;
                }
            }
            byte[] png;
            try (InputStream input = HookEntry.openBuiltInAsset(context, "images/" + name)) {
                png = read(input, 32 * 1024 * 1024);
            }
            if (png.length > 32 * 1024 * 1024 || !expected.equals(hash(png)))
                throw new IllegalStateException("Image checksum differs");
            atomicWrite(target, png);
        }
        atomicWrite(new File(root, "manifest.json"), raw);
    }

    private static String hash(byte[] raw) throws Exception {
        StringBuilder value = new StringBuilder(64);
        for (byte b : MessageDigest.getInstance("SHA-256").digest(raw))
            value.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        return value.toString();
    }

    private static byte[] read(InputStream input, int limit) throws Exception {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        byte[] buffer = new byte[65536];
        int count;
        while ((count = input.read(buffer)) != -1) {
            if (out.size() > limit - count) throw new IllegalStateException("Oversized image asset");
            out.write(buffer, 0, count);
        }
        return out.toByteArray();
    }

    private static void atomicWrite(File target, byte[] raw) throws Exception {
        if (Files.isSymbolicLink(target.toPath())) throw new IllegalStateException("Image path is a link");
        File temp = File.createTempFile(".image-", ".tmp", target.getParentFile());
        try {
            try (java.io.FileOutputStream stream = new java.io.FileOutputStream(temp)) {
                stream.write(raw); stream.getFD().sync();
            }
            Files.move(temp.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally { Files.deleteIfExists(temp.toPath()); }
    }
}
