package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

public final class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        int padding = (int) (24 * getResources().getDisplayMetrics().density);
        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(padding, padding, padding, padding);
        body.setGravity(Gravity.TOP);

        TextView title = new TextView(this);
        title.setText("偶像荣耀本地化");
        title.setTextSize(24);
        body.addView(title);

        TextView instructions = new TextView(this);
        instructions.setTextSize(16);
        instructions.setPadding(0, padding, 0, 0);
        instructions.setText("目标游戏：game.qualiarts.idolypride\n\n" +
            "在 LSPosed 中启用模块并选择偶像荣耀，或用 LSPatch 将模块嵌入游戏。" +
            "重启游戏后，翻译字典位于游戏外部文件目录的 idoly-localify/dict.json；" +
            "开启 dump 时，未收录原文会写入同目录的 dump.jsonl。\n\n" +
            "字典格式、翻译校验和 ADB 命令详见项目 README。" );
        body.addView(instructions);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(body);
        setContentView(scroll);
    }
}
