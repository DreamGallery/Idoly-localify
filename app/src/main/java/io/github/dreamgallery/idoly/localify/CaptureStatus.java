package io.github.dreamgallery.idoly.localify;

import org.json.JSONObject;
import java.io.IOException;

/** Binds a frozen native status to the exact files being exported. */
final class CaptureStatus {
    private CaptureStatus() {}

    static JSONObject forExport(String encoded, String textFile, long textBytes, long layoutBytes)
            throws Exception {
        if (encoded == null || encoded.isEmpty() || encoded.length() > 16384)
            throw new IOException("采集完整性状态缺失；原始文件已保留");
        JSONObject status = new JSONObject(encoded);
        if (status.getInt("schema_version") != 1 ||
                !textFile.equals(status.getString("text_file")) ||
                !status.getBoolean("stopped"))
            throw new IOException("采集状态不属于已停止的当前会话");
        boolean truncated = status.getBoolean("truncated");
        boolean failed = status.getBoolean("write_failed");
        long dropped = status.getLong("dropped_records");
        long written = status.getLong("bytes_written");
        if (dropped < 0 || written < 0 || textBytes < 0 || layoutBytes < 0 ||
                status.getLong("text_records") < 0 || status.getLong("layout_records") < 0)
            throw new IOException("采集完整性计数无效");
        boolean sizeMismatch = written != textBytes + layoutBytes;
        boolean complete = !truncated && !failed && dropped == 0 && !sizeMismatch;
        // Never upgrade native incomplete state, even if its reason is unknown.
        status.put("complete", status.getBoolean("complete") && complete);
        status.put("file_size_mismatch", sizeMismatch);
        status.put("jsonl_may_be_incomplete", failed || sizeMismatch);
        status.put("exported_text_bytes", textBytes);
        status.put("exported_layout_bytes", layoutBytes);
        return status;
    }

    static String warning(JSONObject status) throws Exception {
        if (status.getBoolean("complete")) return "";
        String reason = status.getBoolean("truncated") ? "达到 16 MiB 采集上限" : "";
        if (status.getBoolean("write_failed") || status.getBoolean("file_size_mismatch"))
            reason += (reason.isEmpty() ? "" : "；") + "采集文件写入不完整，JSONL 末行可能残缺";
        return reason.isEmpty() ? "采集完整性未通过校验" : reason;
    }
}
