package io.github.dreamgallery.idoly.localify;

import java.util.List;

/** Numeric stable-release comparison, independent of the host game's package. */
final class UpdateVersion {
    static int compare(String left, String right) {
        String[] a = parts(left), b = parts(right);
        for (int i = 0; i < 3; i++) {
            int order = Integer.compare(Integer.parseInt(a[i]), Integer.parseInt(b[i]));
            if (order != 0) return order;
        }
        return 0;
    }

    static boolean stableTag(String value) {
        return value != null && value.matches("v(?:0|[1-9][0-9]{0,5})\\.(?:0|[1-9][0-9]{0,5})\\.(?:0|[1-9][0-9]{0,5})");
    }

    static boolean eligibleRelease(String tag, boolean draft, boolean prerelease, boolean hasApk) {
        return !draft && !prerelease && hasApk && stableTag(tag);
    }

    interface ReleasePage { List<String> eligibleTags(int page) throws Exception; }

    /** Null marks an exhausted page; empty lists mean another page is needed. */
    static String findPluginRelease(ReleasePage source) throws Exception {
        String selected = null;
        for (int page = 1; ; page++) {
            List<String> tags = source.eligibleTags(page);
            if (tags == null) return selected;
            for (String tag : tags) {
                if (!stableTag(tag)) throw new IllegalArgumentException("Invalid release candidate");
                if (selected == null || compare(tag, selected) > 0) selected = tag;
            }
            // Continue across text-only pages and backport releases. Published
            // timestamps do not establish semantic version order.
        }
    }

    /** Prevent an older in-flight metadata check from undoing a successful install. */
    static final class TextCheckGate {
        private long revision;
        synchronized long begin() { return revision; }
        synchronized boolean publish(long started, Runnable action) {
            if (started != revision) return false;
            action.run();
            return true;
        }
        synchronized void installed(Runnable action) { revision++; action.run(); }
    }

    private static String[] parts(String value) {
        String tag = value != null && value.startsWith("v") ? value : "v" + value;
        if (!stableTag(tag)) throw new IllegalArgumentException("Invalid plugin version");
        return tag.substring(1).split("\\.");
    }

    static String textVersion(String value) {
        if (value == null || value.isEmpty() || value.length() > 128 || value.matches("(?s).*\\p{Cntrl}.*"))
            throw new IllegalArgumentException("更新清单版本无效");
        return value;
    }

    static boolean due(long now, long previous, long interval) {
        return previous <= 0 || now < previous || now - previous >= interval;
    }
}
