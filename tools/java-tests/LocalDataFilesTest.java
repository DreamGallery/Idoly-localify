package io.github.dreamgallery.idoly.localify;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

public final class LocalDataFilesTest {
    interface Check { void run() throws Exception; }
    static void rejected(Check check) throws Exception {
        try { check.run(); } catch (IOException expected) { return; }
        throw new AssertionError("Unsafe cleanup accepted");
    }
    static Path write(Path root, String name) throws Exception {
        Path file = root.resolve(name);
        Files.createDirectories(file.getParent());
        Files.writeString(file, "preserved content");
        return file;
    }
    static void present(Path root, String... names) {
        for (String name : names) if (!Files.isRegularFile(root.resolve(name)))
            throw new AssertionError("Unexpected deletion: " + name);
    }
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory(Path.of(args[0]), "cleanup-");
        for (String name : new String[]{"dict.json", "dict.installed.sha256", "web-notice.json",
                "web-notice.installed.sha256", "master-blobs.bin", "installed-text-version.txt",
                "card-stories.json", "event-stories.json", "love-stories.json",
                "resource/adv_card_test.txt", "asset/nested/replacement.png", "font-profiles.json",
                "resource-han-rounded.bundle", "images/atlas-0.png", "octo-settings.json", "update-url.txt",
                "dump.jsonl", "capture-export-123.jsonl", "capture-export-123.layout.jsonl",
                "capture/text-123.jsonl", "capture/text-123.layout.jsonl", "capture/.export-status-12.json",
                "capture/notes.json", "account-state.json"}) write(root, name);
        File directory = root.toFile();
        int translated = LocalDataFiles.delete(directory, LocalDataFiles.translations(directory));
        if (translated != 11 || !LocalDataFiles.translations(directory).isEmpty()) throw new AssertionError();
        present(root, "font-profiles.json", "resource-han-rounded.bundle", "images/atlas-0.png",
            "octo-settings.json", "update-url.txt", "account-state.json", "capture/text-123.jsonl");
        int captured = LocalDataFiles.delete(directory, LocalDataFiles.captures(directory));
        if (captured != 6 || !LocalDataFiles.captures(directory).isEmpty()) throw new AssertionError();
        present(root, "capture/notes.json", "font-profiles.json", "account-state.json");
        Path protectedFile = write(root.getParent(), "outside.txt");
        Path dictionary = write(root, "dict.json");
        Files.createSymbolicLink(root.resolve("resource/external.txt"), protectedFile);
        rejected(() -> LocalDataFiles.translations(directory));
        if (!Files.exists(dictionary) || !Files.exists(protectedFile)) throw new AssertionError();
        rejected(() -> LocalDataFiles.delete(directory, List.of(dictionary.toFile(), protectedFile.toFile())));
        if (!Files.exists(dictionary)) throw new AssertionError("Preflight must precede deletion");
        rejected(() -> LocalDataFiles.delete(directory, List.of(root.resolve("../outside.txt").toFile())));
        Files.delete(root.resolve("resource/external.txt"));
        List<File> selected = LocalDataFiles.translations(directory);
        Files.delete(dictionary);
        Files.createSymbolicLink(dictionary, protectedFile);
        rejected(() -> LocalDataFiles.delete(directory, selected));
        if (!Files.exists(protectedFile)) throw new AssertionError("Symlink target deleted");
        System.out.println("Cleanup boundaries, preflight and preservation verified");
    }
}
