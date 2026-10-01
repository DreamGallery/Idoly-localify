package io.github.dreamgallery.idoly.localify;

import java.util.Arrays;
import java.util.Collections;

public final class UpdateVersionTest {
    static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        check(UpdateVersion.compare("v0.10.0", "0.2.9") > 0);
        check(UpdateVersion.compare("v0.2.1", "0.2.1") == 0);
        check(UpdateVersion.compare("v0.2.0", "0.2.1") < 0);
        check(UpdateVersion.eligibleRelease("v0.2.2", false, false, true));
        check(!UpdateVersion.eligibleRelease("text-20261003", false, false, true));
        check(!UpdateVersion.eligibleRelease("v0.2.2", false, false, false));
        check(!UpdateVersion.eligibleRelease("v0.2.2", true, false, true));
        check(!UpdateVersion.eligibleRelease("v0.2.2", false, true, true));
        for (String value : new String[]{"v0.2.2-rc1", "v01.2.3", "v1.2", "v999999999999.0.0", "v1.2.3/evil", null})
            check(!UpdateVersion.stableTag(value));
        check(UpdateVersion.due(100, 0, 60));
        check(!UpdateVersion.due(100, 90, 60));
        check(UpdateVersion.due(100, 40, 60));
        check(UpdateVersion.due(50, 100, 60));
        check(UpdateVersion.textVersion("abc123").equals("abc123"));
        for (String value : new String[]{"", "new\nold", "a".repeat(129)}) {
            try { UpdateVersion.textVersion(value); throw new AssertionError(); }
            catch (IllegalArgumentException expected) { }
        }
        int[] pages = {0};
        String plugin = UpdateVersion.findPluginRelease(page -> {
            check(page == ++pages[0]);
            // Two full pages of daily text releases before an APK; a backport
            // can be newer by publication date than a higher plugin version.
            if (page <= 2) return Collections.emptyList();
            if (page == 3) return Arrays.asList("v0.2.2", "v0.2.1");
            if (page == 4) return Arrays.asList("v0.3.0");
            return null;
        });
        check(plugin.equals("v0.3.0") && pages[0] == 5);
        check(UpdateVersion.findPluginRelease(page -> page == 1 ? Collections.emptyList() : null) == null);
        boolean failed = false;
        try {
            UpdateVersion.findPluginRelease(page -> {
                if (page == 1) return Arrays.asList("v0.2.1");
                throw new java.io.IOException("page unavailable");
            });
        } catch (java.io.IOException expected) { failed = true; }
        check(failed); // Incomplete history must not be reported as up to date.

        UpdateVersion.TextCheckGate gate = new UpdateVersion.TextCheckGate();
        String[] cached = {"url-A", "A"};
        long earlierCheck = gate.begin();
        gate.installed(() -> { cached[0] = "url-B"; cached[1] = "B"; });
        check(cached[0].equals("url-B") && cached[1].equals("B"));
        check(!gate.publish(earlierCheck, () -> { cached[0] = "url-A"; cached[1] = "A"; }));
        check(cached[0].equals("url-B") && cached[1].equals("B"));
        check(gate.publish(gate.begin(), () -> cached[1] = "C"));
        check(cached[1].equals("C"));
        System.out.println("Version, paginated releases, successful-install cache and stale-check race tests passed");
    }
}
