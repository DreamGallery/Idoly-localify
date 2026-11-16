package io.github.dreamgallery.idoly.localify;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import org.json.JSONArray;
import org.json.JSONObject;

public final class StoryCatalogTest {
    interface Check { void run() throws Exception; }
    static void rejected(Check check) throws Exception {
        try { check.run(); } catch (Exception expected) { return; }
        throw new AssertionError("Invalid catalog accepted");
    }
    static JSONObject base() throws Exception {
        JSONObject result = new JSONObject();
        for (String name : StoryCatalog.LEGACY_NAMES) result.put(name, new JSONObject());
        return result;
    }
    static byte[] data(String file) throws Exception {
        JSONObject row = new JSONObject().put("file", file).put("episode", 2)
            .put("title", "标题").put("group_id", "char-ai").put("group_name", "小美山爱")
            .put("character_id", "char-ai").put("character_name", "小美山爱");
        return new JSONObject().put("schema_version", 1).put("stories", new JSONArray().put(row))
            .toString().getBytes(StandardCharsets.UTF_8);
    }
    public static void main(String[] args) throws Exception {
        JSONObject manifest = new JSONObject().put("story_catalogs", base());
        assert StoryCatalog.updates(new JSONObject()).length() == 0;
        assert StoryCatalog.updates(manifest).length() == 3;
        JSONObject extra = new JSONObject().put("bond-stories.json", new JSONObject());
        manifest.put("additional_story_catalogs", extra);
        assert StoryCatalog.updates(manifest).length() == 4;
        // Legacy clients still receive the exact three-entry field they accept.
        assert manifest.getJSONObject("story_catalogs").length() == 3;
        rejected(() -> StoryCatalog.updates(new JSONObject().put("additional_story_catalogs", extra)));
        rejected(() -> StoryCatalog.updates(new JSONObject().put("story_catalogs",
            base().put("bond-stories.json", new JSONObject()))));
        assert StoryCatalog.updates(new JSONObject().put("story_catalogs", base())
            .put("additional_story_catalogs", new JSONObject().put("future-stories.json", new JSONObject())))
            .length() == 3;
        rejected(() -> StoryCatalog.updates(new JSONObject().put("story_catalogs", base())
            .put("additional_story_catalogs", new JSONObject().put("bond-stories.json", "invalid"))));
        byte[] bundled = data("adv_bond_ai_01_02.txt");
        assert StoryCatalog.parse("bond-stories.json", bundled).length() == 1;
        rejected(() -> StoryCatalog.parse("bond-stories.json", data("adv_card_ai_01_02.txt")));
        rejected(() -> StoryCatalog.parse("bond-stories.json", data("adv_bond_ai_01_02_short.txt")));
        rejected(() -> StoryCatalog.parse("bond-stories.json", data("../adv_bond_ai_01_02.txt")));
        Path root = Path.of(args[0]);
        assert StoryCatalog.load(root.toFile(), "bond-stories.json", name -> bundled)
            .getJSONObject(0).getString("file").equals("adv_bond_ai_01_02.txt");
        Files.write(root.resolve("bond-stories.json"), data("adv_bond_szk_01_02.txt"));
        assert StoryCatalog.load(root.toFile(), "bond-stories.json", name -> bundled)
            .getJSONObject(0).getString("file").equals("adv_bond_szk_01_02.txt");
        Files.writeString(root.resolve("bond-stories.json"), "{}");
        rejected(() -> StoryCatalog.load(root.toFile(), "bond-stories.json", name -> bundled));
    }
}
