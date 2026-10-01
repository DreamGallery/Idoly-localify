package io.github.dreamgallery.idoly.localify;

import android.util.Log;

import org.json.JSONObject;
import org.json.JSONArray;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

/** Fetches translation data only, checks every digest, then publishes with rollback. */
final class TextHotUpdater {
    private static final String TAG = "IdolyLocalify";
    private static final String DEFAULT_MANIFEST_URL =
        "https://github.com/DreamGallery/Idoly-localify-translations/releases/latest/download/manifest.json";
    private static final int MANIFEST_LIMIT = 4 * 1024 * 1024;
    private static final long TOTAL_LIMIT = 1024L * 1024 * 1024;
    private static final String TRANSACTION_DIR = ".text-update-transaction";
    private static final AtomicBoolean RUNNING = new AtomicBoolean();

    interface Callback { void done(Result result); }
    static final class Result {
        final boolean success;
        final String message;
        Result(boolean ok, String text) { success = ok; message = text; }
    }
    private static final class Item {
        final String name;
        final URL url;
        final String sha256;
        final long maxBytes;
        File staged;
        boolean existed;
        Item(String name, URL url, String sha256, long maxBytes) {
            this.name = name; this.url = url; this.sha256 = sha256;
            this.maxBytes = maxBytes;
        }
    }

    private TextHotUpdater() {}

    static void run(File dataDir, boolean remote, Callback callback) {
        if (!RUNNING.compareAndSet(false, true)) {
            callback.done(new Result(false, "已有文本更新正在进行"));
            return;
        }
        new Thread(() -> {
            Result result;
            try {
                result = remote ? updateRemote(dataDir) : reloadLocal(dataDir);
            } catch (Exception error) {
                // URL query strings in network exception messages may contain
                // user-supplied secrets. Keep the device log free of them.
                Log.e(TAG, "Text update failed: " + error.getClass().getSimpleName());
                String detail = error instanceof IllegalArgumentException ||
                    error instanceof IllegalStateException ? error.getMessage() : null;
                result = new Result(false, detail == null ? "下载或文件操作失败" : detail);
            } finally {
                RUNNING.set(false);
            }
            callback.done(result);
        }, "IdolyTextUpdate").start();
    }

    private static Result reloadLocal(File dataDir) throws Exception {
        if (dataDir == null) throw new IllegalStateException("本地化尚未初始化");
        recover(dataDir);
        File dictionary = new File(dataDir, "dict.json");
        if (!dictionary.isFile()) return new Result(false, "本地字典不存在");
        File notices = new File(dataDir, "web-notice.json");
        Map<String, String> noticeScripts = null;
        if (notices.isFile()) {
            try { noticeScripts = NoticeWebViewHook.prepare(notices); }
            catch (Exception error) { return new Result(false, "公告词典无效：" + error.getMessage()); }
        }
        if (!HookEntry.reloadDictionary(dictionary)) {
            return new Result(false, "字典校验或载入失败，当前翻译保持不变");
        }
        if (noticeScripts != null) NoticeWebViewHook.apply(noticeScripts);
        return new Result(true, "界面字典已重新载入；已打开的游戏页面可能需要重新进入。剧情资源在下一次加载时生效。" );
    }

    static String readManifestUrl(File dataDir) {
        if (dataDir == null) return "";
        File config = new File(dataDir, "hot-update.json");
        if (!config.isFile()) return DEFAULT_MANIFEST_URL;
        if (config.length() > MANIFEST_LIMIT) return "";
        try {
            byte[] bytes = Files.readAllBytes(config.toPath());
            if (bytes.length > MANIFEST_LIMIT) return "";
            return new JSONObject(new String(bytes, StandardCharsets.UTF_8))
                .optString("manifest_url", "");
        } catch (Exception error) {
            return "";
        }
    }

    static void writeManifestUrl(File dataDir, String value) throws Exception {
        if (dataDir == null) throw new IllegalStateException("本地化尚未初始化");
        if (!value.isEmpty()) httpsUrl(value);
        File target = new File(dataDir, "hot-update.json");
        File temp = new File(dataDir, "hot-update.json.tmp");
        byte[] bytes = new JSONObject().put("manifest_url", value)
            .toString(2).getBytes(StandardCharsets.UTF_8);
        try (FileOutputStream out = new FileOutputStream(temp)) {
            out.write(bytes);
            out.getFD().sync();
        }
        atomicMove(temp.toPath(), target.toPath());
    }

    private static Result updateRemote(File dataDir) throws Exception {
        if (dataDir == null) throw new IllegalStateException("本地化尚未初始化");
        recover(dataDir);
        String manifestUrl = readManifestUrl(dataDir);
        if (manifestUrl.isEmpty()) return new Result(false, "请先设置 HTTPS 更新清单地址");
        byte[] manifestBytes = fetch(httpsUrl(manifestUrl), MANIFEST_LIMIT);
        JSONObject manifest = new JSONObject(new String(manifestBytes, StandardCharsets.UTF_8));
        int schemaVersion = UpdatePolicy.schemaVersion(manifest.opt("schema_version"));
        JSONObject archive = manifest.optJSONObject("archive");
        if ((archive != null && schemaVersion != 2) || (manifest.has("archive") && archive == null)) {
            throw new IllegalArgumentException("更新压缩包配置无效");
        }
        String version = manifest.getString("version");
        if (version.isEmpty() || version.length() > 128) {
            throw new IllegalArgumentException("更新清单版本无效");
        }
        JSONObject files = manifest.getJSONObject("files");
        if (!files.has("dict.json") || files.length() > 8192) {
            throw new IllegalArgumentException("更新清单必须包含字典，且文件数不得超过 8192");
        }
        List<String> names = new ArrayList<>();
        for (Iterator<String> iterator = files.keys(); iterator.hasNext();) {
            names.add(iterator.next());
        }
        Collections.sort(names);
        List<Item> items = new ArrayList<>();
        for (String name : names) {
            long max = maximumFor(name);
            JSONObject entry = files.getJSONObject(name);
            String hash = entry.getString("sha256").toLowerCase(Locale.ROOT);
            if (!hash.matches("[0-9a-f]{64}")) {
                throw new IllegalArgumentException("文件 SHA256 无效：" + name);
            }
            items.add(new Item(name, archive == null ? httpsUrl(entry.getString("url")) : null, hash, max));
        }
        List<Item> changed = new ArrayList<>();
        for (Item item : items) {
            File target = safeTarget(dataDir, item.name);
            if (!target.isFile() || !sha256(target).equals(item.sha256)) changed.add(item);
        }
        if (changed.isEmpty()) return new Result(true, "文本已是清单版本 " + version);
        Path stage = Files.createTempDirectory(dataDir.toPath(), ".text-update-");
        Map<String, String> noticeScripts = null;
        try {
            long total = 0;
            for (int index = 0; index < changed.size(); index++) {
                Item item = changed.get(index);
                item.staged = stage.resolve("new-" + index).toFile();
                if (archive == null) total += download(item.url, item.staged, item.maxBytes, item.sha256);
                if (total > TOTAL_LIMIT) throw new IllegalArgumentException("更新总量超过 1 GiB");
            }
            if (archive != null) {
                String archiveHash = archive.getString("sha256").toLowerCase(Locale.ROOT);
                if (!archiveHash.matches("[0-9a-f]{64}")) {
                    throw new IllegalArgumentException("更新压缩包 SHA256 无效");
                }
                File zip = stage.resolve("download.zip").toFile();
                download(httpsUrl(archive.getString("url")), zip, TOTAL_LIMIT, archiveHash);
                Map<String, UpdateArchive.Expected> expected = new LinkedHashMap<>();
                for (Item item : items) expected.put(item.name,
                    new UpdateArchive.Expected(item.sha256, item.maxBytes, item.staged));
                UpdateArchive.extract(zip, expected, TOTAL_LIMIT);
            }
            boolean masterChanged = false;
            for (Item item : changed) {
                if (item.name.equals("web-notice.json")) {
                    noticeScripts = NoticeWebViewHook.prepare(item.staged);
                }
                if (item.name.equals("master-blobs.bin")) {
                    if (!HookEntry.validateMasterBlobs(item.staged)) {
                        throw new IllegalArgumentException("MasterDB 更新文件未通过校验");
                    }
                    masterChanged = true;
                }
            }
            File currentDictionary = new File(dataDir, "dict.json");
            File stagedDictionary = currentDictionary;
            boolean dictionaryChanged = false;
            for (Item item : changed) if (item.name.equals("dict.json")) {
                stagedDictionary = item.staged;
                dictionaryChanged = true;
                break;
            }

            // Backups and a durable journal precede the first in-memory or disk
            // change. Startup restores them if the process dies during commit.
            prepareTransaction(dataDir, changed);
            if (dictionaryChanged && !HookEntry.reloadDictionary(stagedDictionary)) {
                recover(dataDir);
                return new Result(false, "下载字典未通过原生校验，旧版本保持不变");
            }
            try {
                for (Item item : changed) {
                    File target = safeTarget(dataDir, item.name);
                    File parent = target.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs()) {
                        throw new IllegalStateException("无法创建资源目录");
                    }
                    atomicMove(item.staged.toPath(), target.toPath());
                }
                writeJournal(dataDir, "committed", changed);
                if (noticeScripts != null) NoticeWebViewHook.apply(noticeScripts);
                try { clearTransaction(dataDir); }
                catch (Exception cleanupError) {
                    Log.w(TAG, "Committed text update; stale backup cleanup deferred", cleanupError);
                }
                return new Result(true, "已更新到 " + version + "。界面字典即时生效；剧情资源下次加载生效。"
                    + (masterChanged ? "MasterDB 已更新，请重启游戏使其生效。" : ""));
            } catch (Exception error) {
                try { recover(dataDir); }
                catch (Exception rollbackError) {
                    error.addSuppressed(rollbackError);
                }
                if (dictionaryChanged && !HookEntry.reloadDictionary(currentDictionary)) {
                    Log.e(TAG, "Cannot restore running dictionary after update failure");
                }
                throw error;
            }
        } finally {
            File[] leftovers = stage.toFile().listFiles();
            if (leftovers != null) for (File file : leftovers) {
                try { Files.deleteIfExists(file.toPath()); }
                catch (Exception error) { Log.w(TAG, "Cannot remove download staging file", error); }
            }
            try { Files.deleteIfExists(stage); }
            catch (Exception error) { Log.w(TAG, "Cannot remove download staging directory", error); }
        }
    }

    static void recover(File dataDir) throws Exception {
        File directory = new File(dataDir, TRANSACTION_DIR);
        if (!directory.exists()) return;
        File journal = new File(directory, "journal.json");
        if (!journal.isFile()) {
            clearTransaction(dataDir); // No target was changed before this journal exists.
            return;
        }
        JSONObject state = new JSONObject(new String(Files.readAllBytes(journal.toPath()),
            StandardCharsets.UTF_8));
        if ("committed".equals(state.getString("phase"))) {
            clearTransaction(dataDir);
            return;
        }
        if (!"prepared".equals(state.getString("phase"))) {
            throw new IllegalStateException("未知的文本更新恢复状态");
        }
        JSONArray files = state.getJSONArray("files");
        Exception failure = null;
        for (int index = files.length() - 1; index >= 0; index--) {
            JSONObject entry = files.getJSONObject(index);
            try {
                File target = safeTarget(dataDir, entry.getString("name"));
                if (entry.getBoolean("existed")) {
                    File backup = new File(directory, "old-" + index);
                    if (!backup.isFile()) throw new IllegalStateException("更新备份缺失");
                    File parent = target.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs()) {
                        throw new IllegalStateException("无法恢复资源目录");
                    }
                    // Preserve both a complete target and the reusable backup if recovery dies.
                    File restored = new File(directory, "restore-" + index);
                    copyDurably(backup, restored);
                    atomicMove(restored.toPath(), target.toPath());
                } else {
                    Files.deleteIfExists(target.toPath());
                }
            } catch (Exception error) {
                if (failure == null) failure = error;
                else failure.addSuppressed(error);
            }
        }
        if (failure != null) throw failure; // Keep backups for the next startup.
        clearTransaction(dataDir);
        Log.i(TAG, "Recovered interrupted text update");
    }

    private static void prepareTransaction(File dataDir, List<Item> items) throws Exception {
        File directory = new File(dataDir, TRANSACTION_DIR);
        if (!directory.mkdir()) throw new IllegalStateException("无法创建文本更新事务目录");
        try {
            for (int index = 0; index < items.size(); index++) {
                File current = safeTarget(dataDir, items.get(index).name);
                items.get(index).existed = current.isFile();
                if (items.get(index).existed) {
                    copyDurably(current, new File(directory, "old-" + index));
                }
            }
            writeJournal(dataDir, "prepared", items);
        } catch (Exception error) {
            clearTransaction(dataDir);
            throw error;
        }
    }

    private static void writeJournal(File dataDir, String phase, List<Item> items)
            throws Exception {
        File directory = new File(dataDir, TRANSACTION_DIR);
        JSONArray files = new JSONArray();
        for (Item item : items) {
            files.put(new JSONObject().put("name", item.name)
                .put("existed", item.existed));
        }
        byte[] bytes = new JSONObject().put("phase", phase).put("files", files)
            .toString().getBytes(StandardCharsets.UTF_8);
        File temp = new File(directory, "journal.tmp");
        try (FileOutputStream out = new FileOutputStream(temp)) {
            out.write(bytes);
            out.getFD().sync();
        }
        atomicMove(temp.toPath(), new File(directory, "journal.json").toPath());
    }

    private static void copyDurably(File source, File target) throws Exception {
        Files.copy(source.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
        try (FileOutputStream output = new FileOutputStream(target, true)) {
            output.getFD().sync();
        }
    }

    private static void clearTransaction(File dataDir) throws Exception {
        File directory = new File(dataDir, TRANSACTION_DIR);
        // Only called after commit, complete rollback, or before any target changes.
        // Remove the recovery marker first: interrupted cleanup must never leave a
        // prepared journal referring to backups that were already deleted.
        Files.deleteIfExists(new File(directory, "journal.json").toPath());
        File[] files = directory.listFiles();
        if (files != null) for (File file : files) Files.deleteIfExists(file.toPath());
        Files.deleteIfExists(directory.toPath());
    }

    private static long maximumFor(String name) {
        if (name.equals("dict.json")) return 64L * 1024 * 1024;
        if (name.equals("web-notice.json")) return 32L * 1024 * 1024;
        if (name.equals("master-blobs.bin")) return 256L * 1024 * 1024;
        if (name.matches("resource/adv_[A-Za-z0-9_-]+\\.txt")) return 8L * 1024 * 1024;
        throw new IllegalArgumentException("清单包含不允许的文件名：" + name);
    }

    private static File safeTarget(File dataDir, String name) throws Exception {
        maximumFor(name);
        File target = new File(dataDir, name).getCanonicalFile();
        if (!target.getPath().startsWith(dataDir.getCanonicalPath() + File.separator)) {
            throw new IllegalArgumentException("更新路径超出本地化目录");
        }
        return target;
    }

    private static URL httpsUrl(String value) throws Exception {
        return UpdatePolicy.httpsUrl(value);
    }

    private static byte[] fetch(URL url, int maxBytes) throws Exception {
        File temp = File.createTempFile("idoly-update-manifest-", ".json");
        try {
            download(url, temp, maxBytes, null);
            return Files.readAllBytes(temp.toPath());
        } finally {
            Files.deleteIfExists(temp.toPath());
        }
    }

    private static long download(URL url, File target, long maxBytes, String expectedHash)
            throws Exception {
        for (int redirects = 0; redirects <= 5; redirects++) {
        HttpURLConnection conn = (HttpURLConnection) url.openConnection();
        conn.setInstanceFollowRedirects(false);
        conn.setConnectTimeout(10_000);
        conn.setReadTimeout(30_000);
        conn.setRequestMethod("GET");
        conn.setRequestProperty("Accept-Encoding", "identity");
        try {
            int status = conn.getResponseCode();
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                String location = conn.getHeaderField("Location");
                if (location == null) throw new IllegalStateException("下载重定向缺少地址");
                url = UpdatePolicy.redirect(url, location);
                continue;
            }
            if (status != HttpURLConnection.HTTP_OK) {
                throw new IllegalStateException("下载返回 HTTP " + status);
            }
            long declared = conn.getContentLengthLong();
            if (declared > maxBytes) throw new IllegalArgumentException("下载文件超过大小限制");
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            long size = 0;
            try (InputStream input = conn.getInputStream();
                 FileOutputStream output = new FileOutputStream(target)) {
                byte[] buffer = new byte[32 * 1024];
                int read;
                while ((read = input.read(buffer)) != -1) {
                    size += read;
                    if (size > maxBytes) throw new IllegalArgumentException("下载文件超过大小限制");
                    digest.update(buffer, 0, read);
                    output.write(buffer, 0, read);
                }
                output.getFD().sync();
            }
            if (expectedHash != null && !hex(digest.digest()).equals(expectedHash)) {
                throw new IllegalArgumentException("下载文件 SHA256 不匹配");
            }
            return size;
        } finally {
            conn.disconnect();
        }
        }
        throw new IllegalStateException("下载重定向次数超过限制");
    }

    private static String sha256(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (FileInputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[32 * 1024];
            int read;
            while ((read = input.read(buffer)) != -1) digest.update(buffer, 0, read);
        }
        return hex(digest.digest());
    }

    private static String hex(byte[] bytes) {
        char[] digits = "0123456789abcdef".toCharArray();
        char[] out = new char[bytes.length * 2];
        for (int i = 0; i < bytes.length; i++) {
            out[i * 2] = digits[(bytes[i] >>> 4) & 15];
            out[i * 2 + 1] = digits[bytes[i] & 15];
        }
        return new String(out);
    }

    private static void atomicMove(Path source, Path target) throws Exception {
        try {
            Files.move(source, target, StandardCopyOption.REPLACE_EXISTING,
                StandardCopyOption.ATOMIC_MOVE);
        } catch (AtomicMoveNotSupportedException ignored) {
            Files.move(source, target, StandardCopyOption.REPLACE_EXISTING);
        }
    }
}
