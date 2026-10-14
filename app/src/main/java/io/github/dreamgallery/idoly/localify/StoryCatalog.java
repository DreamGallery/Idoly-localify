package io.github.dreamgallery.idoly.localify;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashSet;
import java.util.Set;

/** Bounded, data-only picker metadata supplied by the text update transaction. */
final class StoryCatalog {
    static final int LIMIT = 1024 * 1024;
    static final String[] NAMES = {"card-stories.json", "event-stories.json", "love-stories.json"};
    interface BuiltIn { byte[] read(String name) throws Exception; }

    static boolean accepts(String name) {
        for (String candidate : NAMES) if (candidate.equals(name)) return true;
        return false;
    }

    static JSONArray load(File directory, String name, BuiltIn bundled) throws Exception {
        File file = new File(directory, name);
        if (Files.isSymbolicLink(file.toPath())) throw new IllegalArgumentException("剧情目录不能为符号链接");
        if (!file.exists()) return parse(name, bundled.read(name));
        if (!file.isFile() || file.length() < 1 || file.length() > LIMIT)
            throw new IllegalArgumentException("本地剧情目录无效，请重新更新文本");
        return parse(name, Files.readAllBytes(file.toPath()));
    }

    static JSONArray parse(String name, byte[] bytes) throws Exception {
        if (!accepts(name) || bytes.length < 1 || bytes.length > LIMIT)
            throw new IllegalArgumentException("剧情目录名称或大小无效");
        String text = StandardCharsets.UTF_8.newDecoder().decode(ByteBuffer.wrap(bytes)).toString();
        JSONObject catalog = new JSONObject(text);
        if (!integer(catalog.opt("schema_version"), 1, 1))
            throw new IllegalArgumentException("剧情目录版本无效");
        JSONArray rows = catalog.getJSONArray("stories");
        if (rows.length() > 8192) throw new IllegalArgumentException("剧情目录条目过多");
        String kind = name.substring(0, name.indexOf('-'));
        String group = kind.equals("card") ? "card" : "group";
        Set<String> seen = new HashSet<>();
        for (int i = 0; i < rows.length(); i++) {
            JSONObject row = rows.getJSONObject(i);
            String file = string(row, "file");
            if (!file.matches("adv_" + kind + "_[a-z0-9_]+\\.txt") ||
                    file.endsWith("_short.txt") || !seen.add(file))
                throw new IllegalArgumentException("剧情目录文件名重复或无效");
            if (!integer(row.opt("episode"), 1, 20))
                throw new IllegalArgumentException("剧情目录话数无效");
            string(row, "title");
            if (string(row, group + "_id").isEmpty())
                throw new IllegalArgumentException("剧情目录归属无效");
            string(row, group + "_name");
            for (String key : new String[]{"display_label", "character_id", "character_name"})
                if (row.has(key)) string(row, key);
        }
        return rows;
    }

    private static boolean integer(Object value, int min, int max) {
        if (!(value instanceof Integer || value instanceof Long)) return false;
        long number = ((Number) value).longValue();
        return number >= min && number <= max;
    }

    private static String string(JSONObject row, String key) throws Exception {
        Object value = row.get(key);
        if (!(value instanceof String) || ((String) value).indexOf('\0') >= 0 || ((String) value).length() > 8192)
            throw new IllegalArgumentException("剧情目录文字字段无效");
        return (String) value;
    }
}
