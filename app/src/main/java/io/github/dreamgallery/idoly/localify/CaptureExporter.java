package io.github.dreamgallery.idoly.localify;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Arrays;

/** Exports only the current capture session to system Downloads. */
final class CaptureExporter {
    private static final long LIMIT = 16L * 1024 * 1024;
    private static final String FOLDER = Environment.DIRECTORY_DOWNLOADS + "/Idoly-localify/";

    static final class Result {
        final String textName;
        final String layoutName;
        final String statusName;
        final boolean complete;
        final String warning;
        final Uri textUri;
        final Uri layoutUri;
        final Uri statusUri;

        Result(String textName, String layoutName, String statusName, Uri textUri, Uri layoutUri,
               Uri statusUri, boolean complete, String warning) {
            this.textName = textName;
            this.layoutName = layoutName;
            this.statusName = statusName;
            this.textUri = textUri;
            this.layoutUri = layoutUri;
            this.statusUri = statusUri;
            this.complete = complete;
            this.warning = warning;
        }
    }

    private CaptureExporter() {}

    static Result exportCurrent(Context context, File dataDir, String nativePath,
                                String nativeStatus) throws Exception {
        if (context == null || dataDir == null || nativePath == null)
            throw new IOException("采集文件路径不存在");
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q)
            throw new IOException("Android 9 暂不支持自动导出到下载目录；原始采集记录已保留");

        File root = dataDir.getCanonicalFile();
        File captureDir = new File(root, "capture").getCanonicalFile();
        File rawText = new File(nativePath);
        if (Files.isSymbolicLink(rawText.toPath())) throw new IOException("采集文件不能是链接");
        File text = rawText.getCanonicalFile();
        if (!captureDir.equals(text.getParentFile()) ||
            !text.getName().matches("text-[0-9]+\\.jsonl"))
            throw new IOException("采集文件不在当前会话目录");
        String stamp = text.getName().substring(5, text.getName().length() - 6);
        File rawLayout = new File(captureDir, "text-" + stamp + ".layout.jsonl");
        if (Files.isSymbolicLink(rawLayout.toPath())) throw new IOException("布局文件不能是链接");
        File layout = rawLayout.getCanonicalFile();
        if (!captureDir.equals(layout.getParentFile()) || !text.isFile() || !layout.isFile() ||
            text.length() + layout.length() > LIMIT)
            throw new IOException("本次采集文件缺失或超出大小限制");

        ContentResolver resolver = context.getApplicationContext().getContentResolver();
        String textName = "capture-" + stamp + ".jsonl";
        String layoutName = "capture-" + stamp + ".layout.jsonl";
        String statusName = "capture-" + stamp + ".status.json";
        JSONObject status = CaptureStatus.forExport(nativeStatus, text.getName(),
            text.length(), layout.length());
        boolean complete = status.getBoolean("complete");
        String warning = CaptureStatus.warning(status);
        File metadata = File.createTempFile(".export-status-", ".json", captureDir);
        Uri textUri = null;
        Uri layoutUri = null;
        Uri statusUri = null;
        try {
            textUri = createPending(resolver, textName);
            layoutUri = createPending(resolver, layoutName);
            statusUri = createPending(resolver, statusName);
            // Providers may rename duplicate Downloads entries. Bind metadata
            // to actual provider names and verified content, not requested names.
            textName = displayName(resolver, textUri);
            layoutName = displayName(resolver, layoutUri);
            statusName = displayName(resolver, statusUri);
            String textHash = copyChecked(resolver, text, textUri);
            String layoutHash = copyChecked(resolver, layout, layoutUri);
            status.put("exported_text_file", textName).put("exported_layout_file", layoutName)
                .put("text_sha256", textHash).put("layout_sha256", layoutHash);
            Files.write(metadata.toPath(), status.toString(2).getBytes(StandardCharsets.UTF_8));
            copyChecked(resolver, metadata, statusUri);
            // Publish the completeness metadata first: a process death cannot
            // expose text/layout without also exposing this session's status.
            publish(resolver, statusUri);
            publish(resolver, textUri);
            publish(resolver, layoutUri);
            return new Result(textName, layoutName, statusName, textUri, layoutUri,
                statusUri, complete, warning);
        } catch (Exception error) {
            // Only items inserted by this invocation may be removed.
            deleteOwn(resolver, textUri, error);
            deleteOwn(resolver, layoutUri, error);
            deleteOwn(resolver, statusUri, error);
            throw error;
        } finally {
            // A cleanup failure must not change a verified export's result.
            try { Files.deleteIfExists(metadata.toPath()); } catch (Exception ignored) {}
        }
    }

    private static Uri createPending(ContentResolver resolver, String name) throws IOException {
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
        values.put(MediaStore.MediaColumns.MIME_TYPE,
            name.endsWith(".jsonl") ? "application/x-ndjson" : "application/json");
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, FOLDER);
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri collection = MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);
        Uri item = resolver.insert(collection, values);
        if (item == null) throw new IOException("无法在下载目录创建采集文件");
        return item;
    }

    private static String copyChecked(ContentResolver resolver, File source, Uri destination)
        throws Exception {
        long before = source.length();
        if (before < 0 || before > LIMIT) throw new IOException("采集文件过大");
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        long copied = 0;
        try (InputStream input = new FileInputStream(source);
             OutputStream output = resolver.openOutputStream(destination, "w")) {
            if (output == null) throw new IOException("无法写入下载目录");
            byte[] buffer = new byte[8192];
            int read;
            while ((read = input.read(buffer)) != -1) {
                copied += read;
                if (copied > LIMIT) throw new IOException("采集文件在导出时超出大小限制");
                digest.update(buffer, 0, read);
                output.write(buffer, 0, read);
            }
            output.flush();
        }
        byte[] hash = digest.digest();
        if (copied != before || !Arrays.equals(hash, sha256(resolver, destination, copied)))
            throw new IOException("采集文件导出校验失败");
        StringBuilder hex = new StringBuilder(64);
        for (byte value : hash) {
            hex.append(Character.forDigit((value >>> 4) & 15, 16));
            hex.append(Character.forDigit(value & 15, 16));
        }
        return hex.toString();
    }

    private static byte[] sha256(ContentResolver resolver, Uri item, long expected) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        long size = 0;
        try (InputStream input = resolver.openInputStream(item)) {
            if (input == null) throw new IOException("无法读取导出文件进行校验");
            byte[] buffer = new byte[8192];
            int read;
            while ((read = input.read(buffer)) != -1) {
                size += read;
                if (size > LIMIT) throw new IOException("导出文件超出大小限制");
                digest.update(buffer, 0, read);
            }
        }
        if (size != expected) throw new IOException("采集文件导出长度不一致");
        return digest.digest();
    }

    private static void publish(ContentResolver resolver, Uri item) throws IOException {
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.IS_PENDING, 0);
        if (resolver.update(item, values, null, null) != 1)
            throw new IOException("无法发布已校验的采集文件");
    }

    private static String displayName(ContentResolver resolver, Uri item) throws IOException {
        try (Cursor cursor = resolver.query(item,
            new String[] {MediaStore.MediaColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String actual = cursor.getString(0);
                if (actual != null && !actual.isEmpty()) return actual;
            }
        } catch (Exception error) {
            throw new IOException("无法读取导出文件的实际名称", error);
        }
        throw new IOException("导出文件的实际名称为空");
    }

    private static void deleteOwn(ContentResolver resolver, Uri item, Exception error) {
        if (item == null) return;
        try { resolver.delete(item, null, null); }
        catch (Exception cleanupFailure) { error.addSuppressed(cleanupFailure); }
    }
}
