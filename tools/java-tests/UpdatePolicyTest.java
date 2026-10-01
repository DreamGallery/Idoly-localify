package io.github.dreamgallery.idoly.localify;

import java.net.URL;

public final class UpdatePolicyTest {
    interface Check { void run() throws Exception; }
    static void rejected(Check check) throws Exception {
        try { check.run(); } catch (IllegalArgumentException | java.net.MalformedURLException expected) { return; }
        throw new AssertionError("invalid policy input accepted");
    }
    public static void main(String[] args) throws Exception {
        if (UpdatePolicy.schemaVersion(null) != 1 || UpdatePolicy.schemaVersion(2) != 2 ||
                UpdatePolicy.schemaVersion(1L) != 1) throw new AssertionError();
        for (Object value : new Object[]{"2", "garbage", 2.9, 1.0, true, 0, 3, 4294967298L, new Object()}) {
            rejected(() -> UpdatePolicy.schemaVersion(value));
        }
        URL source = UpdatePolicy.httpsUrl("https://github.com/project/releases/latest/download/manifest.json");
        if (!UpdatePolicy.redirect(source, "../file.zip").getProtocol().equals("https")) throw new AssertionError();
        UpdatePolicy.redirect(source, "https://release-assets.githubusercontent.com/file?signature=test");
        for (String value : new String[]{"http://example.com/a", "https://user:pass@example.com/a", "https://example.com/a#x",
                " https://example.com/a", "https://example.com/a\\b", "https://example.com/a\nb"}) {
            rejected(() -> UpdatePolicy.httpsUrl(value));
            rejected(() -> UpdatePolicy.redirect(source, value));
        }
        rejected(() -> UpdatePolicy.redirect(source, null));
        System.out.println("Schema and HTTPS policy checks passed");
    }
}
