package io.github.dreamgallery.idoly.localify;

import android.net.Uri;
import android.util.Log;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.json.JSONObject;

import java.io.File;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;

import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;

/** Applies reviewed, exact DOM text mappings to official game notice pages only. */
final class NoticeWebViewHook {
    private static final String TAG = "IdolyLocalify";
    private static final String HOST = "stat.game-idolypride.jp";
    private static final Pattern PATH = Pattern.compile(
        "^/notice/[0-9a-f]{64}/index\\.html$");
    private static final Set<Method> HOOKED = new HashSet<>();
    private static final int MAX_BYTES = 32 * 1024 * 1024;
    private static final int MAX_PAGES = 8192;
    private static final int MAX_ENTRIES_PER_PAGE = 2048;
    private static final int MAX_PAGE_BYTES = 1024 * 1024;
    private static final int MAX_TEXT_BYTES = 64 * 1024;
    private static volatile Snapshot active = new Snapshot(Collections.emptyMap());

    private static final class Snapshot {
        final Map<String, String> pages;
        final LinkedHashMap<String, String> scripts = new LinkedHashMap<String, String>(8, 0.75f, true) {
            @Override protected boolean removeEldestEntry(Map.Entry<String, String> eldest) {
                return size() > 8;
            }
        };

        Snapshot(Map<String, String> pages) {
            this.pages = Collections.unmodifiableMap(new HashMap<>(pages));
        }

        synchronized String scriptFor(String path) {
            String script = scripts.get(path);
            if (script == null) {
                String data = pages.get(path);
                if (data == null) return null;
                script = scriptForPage(path, data);
                scripts.put(path, script);
            }
            return script;
        }
    }

    private NoticeWebViewHook() {}

    static void install() {
        try {
            XposedHelpers.findAndHookMethod(WebView.class, "setWebViewClient",
                WebViewClient.class, new XC_MethodHook() {
                    @Override
                    protected void afterHookedMethod(MethodHookParam param) {
                        if (param.args[0] instanceof WebViewClient) {
                            hookPageFinished((WebViewClient) param.args[0]);
                        }
                    }
                });
            Log.i(TAG, "Watching notice WebView clients");
        } catch (Throwable error) {
            Log.e(TAG, "Cannot watch notice WebView clients", error);
        }
    }

    private static void hookPageFinished(WebViewClient client) {
        Class<?> type = client.getClass();
        while (type != null && WebViewClient.class.isAssignableFrom(type)) {
            try {
                Method method = type.getDeclaredMethod("onPageFinished", WebView.class,
                    String.class);
                synchronized (HOOKED) {
                    if (HOOKED.contains(method)) return;
                    XposedBridge.hookMethod(method, new XC_MethodHook() {
                        @Override
                        protected void afterHookedMethod(MethodHookParam param) {
                            if (param.args[0] instanceof WebView &&
                                    param.args[1] instanceof String) {
                                inject((WebView) param.args[0], (String) param.args[1]);
                            }
                        }
                    });
                    HOOKED.add(method);
                }
                Log.i(TAG, "Watching WebViewClient.onPageFinished: " + type.getName());
                return;
            } catch (NoSuchMethodException ignored) {
                type = type.getSuperclass();
            } catch (Throwable error) {
                Log.e(TAG, "Cannot hook notice page completion", error);
                return;
            }
        }
    }

    static void configure(File dictionary) throws Exception {
        apply(prepare(dictionary));
    }

    static void apply(Map<String, String> compiled) {
        active = new Snapshot(compiled);
        Log.i(TAG, "Loaded " + compiled.size() + " notice WebView pages");
    }

    /** Validate a staged notice map without changing the active WebView map. */
    static Map<String, String> prepare(File dictionary) throws Exception {
        if (dictionary.length() > MAX_BYTES) {
            throw new IllegalArgumentException("Notice dictionary exceeds 32 MiB");
        }
        byte[] bytes = Files.readAllBytes(dictionary.toPath());
        if (bytes.length > MAX_BYTES) {
            throw new IllegalArgumentException("Notice dictionary exceeds 32 MiB");
        }
        JSONObject root = new JSONObject(new String(bytes, StandardCharsets.UTF_8));
        if (root.length() != 2 || !HOST.equals(root.getString("host"))) {
            throw new IllegalArgumentException("Unexpected notice dictionary host");
        }
        JSONObject pages = root.getJSONObject("pages");
        if (pages.length() > MAX_PAGES) {
            throw new IllegalArgumentException("Too many notice pages");
        }
        Map<String, String> compiled = new HashMap<>();
        for (Iterator<String> paths = pages.keys(); paths.hasNext();) {
            String path = paths.next();
            if (!PATH.matcher(path).matches()) {
                throw new IllegalArgumentException("Unexpected notice path: " + path);
            }
            JSONObject translations = pages.getJSONObject(path);
            if (translations.length() == 0 || translations.length() > MAX_ENTRIES_PER_PAGE) {
                throw new IllegalArgumentException("Invalid notice page entry count");
            }
            for (Iterator<String> keys = translations.keys(); keys.hasNext();) {
                String source = keys.next();
                Object translated = translations.get(source);
                if (source.isEmpty() || !(translated instanceof String) ||
                        ((String) translated).isEmpty() ||
                        source.getBytes(StandardCharsets.UTF_8).length > MAX_TEXT_BYTES ||
                        ((String) translated).getBytes(StandardCharsets.UTF_8).length > MAX_TEXT_BYTES) {
                    throw new IllegalArgumentException("Invalid notice translation text");
                }
            }
            String data = translations.toString();
            if (data.getBytes(StandardCharsets.UTF_8).length > MAX_PAGE_BYTES) {
                throw new IllegalArgumentException("Notice page exceeds 1 MiB");
            }
            compiled.put(path, data);
        }
        return compiled;
    }

    private static String scriptForPage(String path, String pageJson) {
        String data = JSONObject.quote(pageJson)
            .replace("\u2028", "\\u2028").replace("\u2029", "\\u2029");
        return "(function(){"
                + "if(location.protocol!=='https:'||location.hostname!==" + JSONObject.quote(HOST)
                + "||location.pathname!==" + JSONObject.quote(path) + ")return;"
                + "var marker='__idolyLocalifyNoticePath';"
                + "if(window[marker]===location.pathname)return;"
                + "if(window.__idolyLocalifyNoticeObserver)"
                + "window.__idolyLocalifyNoticeObserver.disconnect();"
                + "var map=JSON.parse(" + data + ");"
                + "function apply(){if(location.pathname!==" + JSONObject.quote(path)
                + "||!document.body)return;"
                + "var walk=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);"
                + "for(var node;node=walk.nextNode();){var parent=node.parentElement;"
                + "if(!parent||parent.closest('script,style,noscript,textarea,input'))continue;"
                + "var raw=node.nodeValue,key=raw.trim();"
                + "if(Object.prototype.hasOwnProperty.call(map,key)){"
                + "var start=raw.indexOf(key);"
                + "node.nodeValue=raw.slice(0,start)+map[key]+raw.slice(start+key.length);}}}"
                + "window[marker]=location.pathname;apply();"
                + "var pending=false;var observer=new MutationObserver(function(){"
                + "if(pending)return;pending=true;requestAnimationFrame(function(){"
                + "pending=false;apply();});});"
                + "observer.observe(document.documentElement,"
                + "{subtree:true,childList:true,characterData:true});"
                + "window.__idolyLocalifyNoticeObserver=observer;"
                + "})();";
    }

    private static void inject(WebView view, String url) {
        Uri uri = Uri.parse(url);
        if (!"https".equalsIgnoreCase(uri.getScheme()) ||
                !HOST.equalsIgnoreCase(uri.getHost())) return;
        String path = uri.getPath();
        if (path == null || !PATH.matcher(path).matches()) return;
        if (!active.pages.containsKey(path)) return;
        view.post(() -> {
            Uri current = Uri.parse(view.getUrl() == null ? "" : view.getUrl());
            if ("https".equalsIgnoreCase(current.getScheme()) &&
                    HOST.equalsIgnoreCase(current.getHost()) &&
                    path.equals(current.getPath())) {
                String script = active.scriptFor(path);
                if (script == null) return;
                view.evaluateJavascript(script, null);
                Log.i(TAG, "Localized notice WebView: " + path);
            }
        });
    }
}
