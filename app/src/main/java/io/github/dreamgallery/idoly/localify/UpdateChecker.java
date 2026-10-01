package io.github.dreamgallery.idoly.localify;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/** Metadata-only checks. No Activity is retained and no payload is downloaded. */
final class UpdateChecker {
    static final String RELEASES = FeedbackUi.REPOSITORY + "/releases";
    private static final String API = "https://api.github.com/repos/DreamGallery/Idoly-localify-translations/releases?per_page=100";
    private static final long INTERVAL = 6L * 60 * 60 * 1000;
    private static final AtomicBoolean RUNNING = new AtomicBoolean();
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static final UpdateVersion.TextCheckGate TEXT_CHECK = new UpdateVersion.TextCheckGate();

    private static SharedPreferences preferences(Context context) {
        return context.getApplicationContext().getSharedPreferences("idoly-localify-updates", 0);
    }

    static boolean enabled(Context context) { return preferences(context).getBoolean("startup_check", true); }
    static void setEnabled(Context context, boolean enabled) {
        preferences(context).edit().putBoolean("startup_check", enabled).apply();
    }

    static void startup(Context context, File directory) {
        if (enabled(context)) check(context, directory, false);
    }

    static void textInstalled(Context context, TextHotUpdater.Result result) {
        if (!result.success || result.manifestUrl == null || result.version == null) return;
        SharedPreferences prefs = preferences(context);
        TEXT_CHECK.installed(() -> prefs.edit().putString("text_address", result.manifestUrl)
            .putString("text_version", result.version).putString("text_status", "检查完成").apply());
    }

    private static List<String> releasePage(int page) throws Exception {
        JSONArray releases = new JSONArray(new String(TextHotUpdater.fetchMetadata(API + "&page=" + page,
            2 * 1024 * 1024), StandardCharsets.UTF_8));
        List<String> tags = new ArrayList<>();
        for (int i = 0; i < releases.length(); i++) {
            JSONObject release = releases.getJSONObject(i);
            String tag = release.optString("tag_name", "");
            JSONArray assets = release.optJSONArray("assets");
            boolean apk = false;
            if (assets != null) for (int j = 0; j < assets.length(); j++) {
                JSONObject asset = assets.getJSONObject(j);
                if (asset.optString("name", "").endsWith(".apk") &&
                        asset.optString("browser_download_url", "").startsWith(
                            FeedbackUi.REPOSITORY + "/releases/download/")) apk = true;
            }
            if (UpdateVersion.eligibleRelease(tag, release.optBoolean("draft"), release.optBoolean("prerelease"), apk))
                tags.add(tag);
        }
        return tags.isEmpty() && releases.length() < 100 ? null : tags;
    }

    static void check(Context context, File directory, boolean force) {
        if (directory == null) return;
        SharedPreferences prefs = preferences(context);
        long now = System.currentTimeMillis();
        if ((!force && !UpdateVersion.due(now, prefs.getLong("last_attempt", 0), INTERVAL))
                || !RUNNING.compareAndSet(false, true)) return;
        prefs.edit().putLong("last_attempt", now).apply();
        MAIN.post(LocalifySettingsUi::refreshUpdateStatus);
        new Thread(() -> {
            try {
                long textRevision = TEXT_CHECK.begin();
                try {
                    String address = TextHotUpdater.readManifestUrl(directory);
                    String version = TextHotUpdater.remoteVersion(address);
                    TEXT_CHECK.publish(textRevision, () -> prefs.edit().putString("text_version", version).putString("text_address", address)
                        .putString("text_status", version.isEmpty() ? "在线文本更新已停用" : "检查完成").apply());
                } catch (Exception ignored) {
                    TEXT_CHECK.publish(textRevision, () -> prefs.edit().putString("text_status", "暂时无法检查文本更新").apply());
                }
                try {
                    String selected = UpdateVersion.findPluginRelease(UpdateChecker::releasePage);
                    boolean found = selected != null;
                    String link = found ? RELEASES + "/tag/" + selected : "";
                    prefs.edit().putString("plugin_version", found ? selected : "").putString("plugin_url", link)
                        .putString("plugin_status", found ? "检查完成" : "暂未找到插件发行版").apply();
                } catch (Exception ignored) {
                    prefs.edit().putString("plugin_status", "暂时无法检查插件更新").apply();
                }
            } finally {
                RUNNING.set(false);
                MAIN.post(LocalifySettingsUi::refreshUpdateStatus);
            }
        }, "IdolyVersionCheck").start();
    }

    static String status(Context context, File directory) {
        SharedPreferences prefs = preferences(context);
        String text = TextHotUpdater.readTextVersion(directory);
        String display = text.isEmpty() ? "内置文本" : text;
        String remote = prefs.getString("text_version", "");
        String address = TextHotUpdater.readManifestUrl(directory);
        boolean sameAddress = address.equals(prefs.getString("text_address", ""));
        String textState = sameAddress && !remote.isEmpty() && !remote.equals(text) ? "有可用文本更新" :
            sameAddress && !remote.isEmpty() && remote.equals(text) ? "文本已是最新" : prefs.getString("text_status", "尚未检查文本更新");
        String plugin = prefs.getString("plugin_version", "");
        String pluginState = prefs.getString("plugin_status", "尚未检查插件更新");
        if (UpdateVersion.stableTag(plugin)) pluginState = UpdateVersion.compare(plugin, BuildConfig.VERSION_NAME) > 0 ?
            "插件 " + plugin + " 可下载" : "插件已是最新";
        if (prefs.getString("plugin_status", "").startsWith("暂时"))
            pluginState = "暂时无法检查插件更新";
        if (prefs.getString("text_status", "").startsWith("暂时"))
            textState = "暂时无法检查文本更新";
        if (!sameAddress) textState = "尚未检查当前文本地址";
        if (address.isEmpty()) textState = "在线文本更新已停用";
        return "插件 " + BuildConfig.VERSION_NAME + "\n文本：" + display + "\n"
            + (RUNNING.get() ? "正在检查版本…" : textState + "\n" + pluginState);
    }

    static String downloadPage(Context context) {
        String link = preferences(context).getString("plugin_url", "");
        return link.startsWith(RELEASES + "/tag/v") ? link : RELEASES;
    }
}
