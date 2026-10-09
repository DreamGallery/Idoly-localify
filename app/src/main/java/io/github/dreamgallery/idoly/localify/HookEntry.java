package io.github.dreamgallery.idoly.localify;

import android.app.AndroidAppHelper;
import android.app.Application;
import android.content.Context;
import android.content.res.XModuleResources;
import android.util.Log;

import com.bytedance.shadowhook.ShadowHook;

import java.io.File;
import java.io.ByteArrayOutputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicBoolean;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.IXposedHookZygoteInit;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public final class HookEntry implements IXposedHookLoadPackage, IXposedHookZygoteInit {
    private static final String TAG = "IdolyLocalify";
    private static final String GAME = "game.qualiarts.idolypride";
    private static final AtomicBoolean STARTED = new AtomicBoolean(false);
    private static final AtomicBoolean NATIVE_READY = new AtomicBoolean(false);
    private static String modulePath;

    private static native boolean nativeInstall(String il2cppPath, String dictionaryPath, String dumpPath,
                                                boolean runtimeImagesReady);
    private static native boolean nativeReloadDictionary(String dictionaryPath);
    private static native int nativeUpdateDictionary(String dictionaryPath, boolean deferApply);
    private static native boolean nativeValidateMasterBlobs(String path);
    private static native boolean nativeSetTextCapture(boolean enabled);
    private static native String nativeGetTextCapturePath();
    private static native String nativeGetTextCaptureStatus();
    private static native String nativeGetFontProfiles();
    private static native boolean nativeApplyFontProfiles(String json);
    private static native boolean nativeResetFontProfiles();
    private static native void nativePauseCaptureWrites(boolean paused);

    static void pauseCaptureWrites(boolean paused) {
        if (NATIVE_READY.get() && TranslationPreferences.active()) nativePauseCaptureWrites(paused);
    }

    static boolean reloadDictionary(File dictionary) {
        return NATIVE_READY.get() && nativeReloadDictionary(dictionary.getAbsolutePath());
    }

    static int updateDictionary(File dictionary, boolean deferApply) {
        return NATIVE_READY.get() ? nativeUpdateDictionary(dictionary.getAbsolutePath(),
                deferApply || !TranslationPreferences.active())
            : DictionaryUpdateStatus.NOT_READY;
    }

    static boolean validateMasterBlobs(File file) {
        return NATIVE_READY.get() && nativeValidateMasterBlobs(file.getAbsolutePath());
    }

    static boolean setTextCaptureEnabled(boolean enabled) {
        return NATIVE_READY.get() && TranslationPreferences.active() && nativeSetTextCapture(enabled);
    }

    static String getTextCapturePath() {
        return NATIVE_READY.get() ? nativeGetTextCapturePath() : "";
    }

    static String getTextCaptureStatus() {
        return NATIVE_READY.get() ? nativeGetTextCaptureStatus() : "";
    }

    static String getFontProfiles() {
        return NATIVE_READY.get() ? nativeGetFontProfiles() : null;
    }

    static boolean applyFontProfiles(String json) {
        return NATIVE_READY.get() && nativeApplyFontProfiles(json);
    }

    static boolean resetFontProfiles() {
        return NATIVE_READY.get() && nativeResetFontProfiles();
    }

    // Called by the native ADV playback lifecycle bridge.
    private static void onStoryVisibilityChanged(boolean visible) {
        LocalifyOverlayUi.onStoryVisibilityChanged(visible);
    }

    @Override
    public void initZygote(StartupParam startupParam) {
        modulePath = startupParam.modulePath;
    }

    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam loadPackage) {
        if (!GAME.equals(loadPackage.packageName)) return;
        NoticeWebViewHook.install();
        try {
            XposedHelpers.findAndHookMethod("com.unity3d.player.UnityPlayer",
                loadPackage.classLoader, "loadNative", String.class, new XC_MethodHook() {
                    @Override
                    protected void afterHookedMethod(MethodHookParam param) {
                        Application application = AndroidAppHelper.currentApplication();
                        if (application != null) initialize(application);
                    }
                });
            Log.i(TAG, "Watching UnityPlayer.loadNative in " + GAME);
        } catch (Throwable error) {
            Log.e(TAG, "Cannot hook UnityPlayer.loadNative", error);
        }
    }

    private static void initialize(Context context) {
        if (!STARTED.compareAndSet(false, true)) return;
        try {
            File external = context.getExternalFilesDir(null);
            File dataDir = new File(external != null ? external : context.getFilesDir(),
                "idoly-localify");
            if (!dataDir.exists() && !dataDir.mkdirs()) {
                throw new IllegalStateException("Cannot create " + dataDir);
            }
            // Recover a transaction interrupted by process death before the
            // built-in asset installer or native loader reads any file.
            TextHotUpdater.recover(dataDir);
            TranslationPreferences.initialize(context);
            LocalifyOverlayUi.install((Application) context.getApplicationContext());
            LocalifyOverlayUi.configure(dataDir);
            UpdateChecker.startup(context, dataDir);
            System.loadLibrary("idoly_localify");
            if (!TranslationPreferences.active()) {
                // Keep the parser available for verified downloads. No Unity
                // localization/font/resource hooks are installed in this process.
                NATIVE_READY.set(true);
                Log.i(TAG, "Translation disabled; settings and download validation available");
                return;
            }
            File dictionary = new File(dataDir, "dict.json");
            ensureBuiltInDictionary(context, dictionary);
            boolean imagesReady = false;
            try { BuiltInImagesInstaller.install(context, dataDir); imagesReady = true; }
            catch (Exception error) {
                Log.w(TAG, "Runtime images unavailable; original images retained", error);
            }
            try {
                File font = new File(dataDir, "resource-han-rounded.bundle");
                boolean installed = BuiltInFontInstaller.install(
                    () -> openBuiltInAsset(context, font.getName()), font);
                Log.i(TAG, "Module font bundle " + (installed ? "installed: " : "verified: ") + font);
            } catch (Exception error) {
                // A failed update leaves the previous complete bundle available.
                // Native loading retries the module bundle on later text callbacks.
                Log.w(TAG, "Module font bundle unavailable or update failed; retaining any "
                    + "existing complete module bundle for retry", error);
            }
            try {
                File notices = new File(dataDir, "web-notice.json");
                ensureBuiltInFile(context, notices, "web-notice.installed.sha256");
                NoticeWebViewHook.configure(notices);
            } catch (Throwable error) {
                // A missing or damaged optional WebView dictionary must not disable
                // the native story and interface localization hooks.
                Log.e(TAG, "Notice WebView localization unavailable", error);
            }

            ShadowHook.init(new ShadowHook.ConfigBuilder()
                .setMode(ShadowHook.Mode.UNIQUE)
                .build());
            File il2cpp = new File(context.getApplicationInfo().nativeLibraryDir, "libil2cpp.so");
            boolean ok = nativeInstall(il2cpp.getAbsolutePath(), dictionary.getAbsolutePath(),
                new File(dataDir, "dump.jsonl").getAbsolutePath(), imagesReady);
            if (!ok) throw new IllegalStateException("Native installation failed");
            NATIVE_READY.set(true);
            FontProfileEditor.restoreSaved(dataDir);
            Log.i(TAG, "Native initialization succeeded; dictionary=" + dictionary);
        } catch (Throwable error) {
            STARTED.set(false);
            NATIVE_READY.set(false);
            Log.e(TAG, "Initialization failed", error);
        }
    }

    static InputStream openBuiltInAsset(Context context, String name) throws Exception {
        if (modulePath != null) {
            return XModuleResources.createInstance(modulePath, null).getAssets()
                .open("idoly-localify/" + name);
        } else {
            Context moduleContext = context.createPackageContext(
                "io.github.dreamgallery.idoly.localify", Context.CONTEXT_IGNORE_SECURITY);
            return moduleContext.getAssets().open("idoly-localify/" + name);
        }
    }

    static byte[] readBuiltInAsset(Context context, String name) throws Exception {
        try (InputStream source = openBuiltInAsset(context, name);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int read;
            while ((read = source.read(buffer)) != -1) output.write(buffer, 0, read);
            return output.toByteArray();
        }
    }

    private static byte[] digest(byte[] bytes) throws Exception {
        return MessageDigest.getInstance("SHA-256").digest(bytes);
    }

    private static void ensureBuiltInDictionary(Context context, File target) throws Exception {
        ensureBuiltInFile(context, target, "dict.installed.sha256");
    }

    private static void ensureBuiltInFile(Context context, File target, String markerName)
            throws Exception {
        File marker = new File(target.getParentFile(), markerName);
        byte[] builtIn = readBuiltInAsset(context, target.getName());
        byte[] newDigest = digest(builtIn);
        if (target.exists()) {
            // An existing file with no marker predates this mechanism or was supplied by
            // the user. A changed file is also user-owned: never overwrite either.
            if (!marker.exists() || !Arrays.equals(digest(Files.readAllBytes(target.toPath())),
                                                 Files.readAllBytes(marker.toPath()))) {
                Log.i(TAG, "Keeping user dictionary: " + target);
                return;
            }
            if (Arrays.equals(newDigest, Files.readAllBytes(marker.toPath()))) return;
        }
        File temporary = new File(target.getParentFile(), target.getName() + ".tmp");
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            output.write(builtIn);
            output.getFD().sync();
        }
        try {
            Files.move(temporary.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING,
                       StandardCopyOption.ATOMIC_MOVE);
        } catch (AtomicMoveNotSupportedException ignored) {
            Files.move(temporary.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
        Files.write(marker.toPath(), newDigest);
        Log.i(TAG, "Installed built-in dictionary: " + target);
    }
}
