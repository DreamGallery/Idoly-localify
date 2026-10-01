package io.github.dreamgallery.idoly.localify;

import java.net.URL;

/** Pure Java validation shared by the updater and local regression tests. */
final class UpdatePolicy {
    static int schemaVersion(Object value) {
        if (value == null) return 1; // Legacy manifests omitted this field.
        if (!(value instanceof Integer || value instanceof Long)) {
            throw new IllegalArgumentException("更新清单版本格式无效");
        }
        long version = ((Number) value).longValue();
        if (version != 1 && version != 2) {
            throw new IllegalArgumentException("更新清单需要更新版本的插件");
        }
        return (int) version;
    }

    static URL httpsUrl(String value) throws Exception {
        if (value == null || value.indexOf('\\') >= 0 ||
                value.chars().anyMatch(ch -> ch <= 32 || ch == 127)) {
            throw new IllegalArgumentException("更新地址必须为无账号信息的 HTTPS URL");
        }
        URL url = new URL(value);
        if (!"https".equalsIgnoreCase(url.getProtocol()) || url.getHost().isEmpty() ||
                url.getUserInfo() != null || url.getRef() != null) {
            throw new IllegalArgumentException("更新地址必须为无账号信息的 HTTPS URL");
        }
        return url;
    }

    static URL redirect(URL source, String location) throws Exception {
        if (location == null || location.indexOf('\\') >= 0 ||
                location.chars().anyMatch(ch -> ch <= 32 || ch == 127)) {
            throw new IllegalArgumentException("下载重定向地址无效");
        }
        return httpsUrl(new URL(source, location).toExternalForm());
    }
}
