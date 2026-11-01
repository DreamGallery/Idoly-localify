package io.github.dreamgallery.idoly.localify;

import android.content.Context;
import org.json.JSONArray;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

/** Shared settings-list ordering; never changes game character or ownership data. */
final class CharacterGroups {
    private static CharacterGroups cached;
    private final Map<String, Integer> positions = new HashMap<>();
    private final Map<String, String> names = new HashMap<>();

    static synchronized CharacterGroups load(Context context) {
        if (cached != null) return cached;
        CharacterGroups result = new CharacterGroups();
        try {
            JSONArray groups = new JSONArray(new String(
                HookEntry.readBuiltInAsset(context, "character-groups.json"), StandardCharsets.UTF_8));
            int position = 0;
            for (int i = 0; i < groups.length(); i++) {
                JSONObject group = groups.getJSONObject(i);
                JSONArray members = group.getJSONArray("characters");
                for (int j = 0; j < members.length(); j++) {
                    String id = members.getString(j);
                    result.positions.put(id, position++);
                    result.names.put(id, group.getString("name"));
                }
            }
        } catch (Exception error) {
            result = new CharacterGroups();
            android.util.Log.w("IdolyLocalify", "Character group index unavailable", error);
        }
        return cached = result;
    }

    int order(String id) { return positions.getOrDefault(id, Integer.MAX_VALUE); }
    String name(String id, String fallback) { return names.getOrDefault(id, fallback); }
    int compare(String left, String right) {
        int result = Integer.compare(order(left), order(right));
        return result == 0 ? left.compareTo(right) : result;
    }
}
