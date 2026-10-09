package io.github.dreamgallery.idoly.localify;

import android.content.Context;
import android.content.SharedPreferences;

/** Startup policy is separate from downloadable dictionaries. */
final class TranslationPreferences {
    private static volatile boolean sessionEnabled;

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences("idoly-localify-translation", 0);
    }

    static void initialize(Context context) { sessionEnabled = enabled(context); }
    static boolean active() { return sessionEnabled; }
    static boolean enabled(Context context) { return prefs(context).getBoolean("enabled", true); }
    static boolean deleted(Context context) { return prefs(context).getBoolean("files_deleted", false); }
    static void setEnabled(Context context, boolean enabled) {
        if (!prefs(context).edit().putBoolean("enabled", enabled).commit())
            throw new IllegalStateException("无法保存翻译开关");
    }
    static void markDeleted(Context context) {
        if (!prefs(context).edit().putBoolean("enabled", false).putBoolean("files_deleted", true).commit())
            throw new IllegalStateException("无法保存文件清理状态");
    }
    static void downloaded(Context context) { prefs(context).edit().putBoolean("files_deleted", false).apply(); }
}
