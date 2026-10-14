package io.github.dreamgallery.idoly.localify;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

/** Deletion is restricted to plugin-owned payloads; settings and game data stay outside it. */
final class LocalDataFiles {
    static List<File> translations(File directory) throws IOException {
        List<File> result = new ArrayList<>();
        for (String name : new String[]{"dict.json", "dict.installed.sha256", "web-notice.json",
                "web-notice.installed.sha256", "master-blobs.bin", "installed-text-version.txt",
                "card-stories.json", "event-stories.json", "love-stories.json"}) {
            addFile(directory, new File(directory, name), result);
        }
        collect(directory, new File(directory, "resource"), result, true);
        collect(directory, new File(directory, "asset"), result, true);
        return result;
    }

    static List<File> captures(File directory) throws IOException {
        List<File> result = new ArrayList<>();
        addFile(directory, new File(directory, "dump.jsonl"), result);
        File[] legacy = directory.listFiles();
        if (legacy == null) throw new IOException("无法读取采集目录");
        for (File file : legacy)
            if (file.getName().matches("capture-export-[0-9]+(?:\\.layout)?\\.jsonl")) addFile(directory, file, result);
        File folder = new File(directory, "capture");
        check(directory, folder);
        if (!folder.exists()) return result;
        File[] files = folder.listFiles();
        if (files == null) throw new IOException("无法读取采集目录");
        for (File file : files) {
            if (file.getName().matches("text-[0-9]+(?:\\.layout)?\\.jsonl") ||
                    file.getName().matches("\\.export-status-.*\\.json")) addFile(directory, file, result);
        }
        return result;
    }

    private static void collect(File root, File folder, List<File> result, boolean recursive) throws IOException {
        check(root, folder);
        if (!folder.exists()) return;
        if (!folder.isDirectory()) throw new IOException("本地资源目录无效");
        File[] files = folder.listFiles();
        if (files == null) throw new IOException("无法读取本地资源目录");
        for (File file : files) {
            check(root, file);
            if (file.isDirectory() && recursive) collect(root, file, result, true);
            else addFile(root, file, result);
        }
    }

    private static void addFile(File root, File file, List<File> result) throws IOException {
        check(root, file);
        if (!file.exists()) return;
        if (!file.isFile()) throw new IOException("待清理文件类型无效");
        result.add(file);
    }

    private static void check(File root, File file) throws IOException {
        File base = root.getAbsoluteFile();
        if (!file.getCanonicalFile().toPath().startsWith(base.getCanonicalFile().toPath()))
            throw new IOException("清理路径超出本地化目录");
        for (File p = file.getAbsoluteFile(); p != null; p = p.getParentFile()) {
            if (Files.isSymbolicLink(p.toPath())) throw new IOException("不清理符号链接");
            if (p.equals(base)) return;
        }
        throw new IOException("清理路径超出本地化目录");
    }

    static int delete(File root, List<File> files) throws IOException {
        // Validate the full selection before deleting the first file.
        for (File file : files) check(root, file);
        int count = 0;
        for (File file : files) {
            check(root, file);
            if (Files.deleteIfExists(file.toPath())) count++;
        }
        return count;
    }
}
