package io.github.dreamgallery.idoly.localify;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.widget.LinearLayout;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.Locale;

/** Optional Unity graphics settings. This page only exchanges configuration snapshots. */
final class GraphicsSettingsUi {
    private static final String PREFS="idoly-localify-graphics";
    private GraphicsSettingsUi() {}

    static void restoreSaved(Context context) {
        String value=context.getSharedPreferences(PREFS,Context.MODE_PRIVATE).getString("settings",null);
        if(value!=null)HookEntry.setGraphicsSettings(value);
    }

    static void show(Activity activity) {
        Dialog dialog=new SettingsDialog(activity);
        SettingsUiStyle.Page page=new SettingsUiStyle.Page(dialog,"实验性 · 高级画质",
            "提高画质会增加耗电与显存占用。实际帧率受屏幕刷新率和设备性能限制。", "返回设置",dialog::dismiss);
        LinearLayout options=SettingsUiStyle.card(page.body,"画质覆盖",null);
        Switch enabled=new Switch(dialog.getContext());enabled.setText("启用高级画质");
        SettingsUiStyle.styleSwitch(enabled);options.addView(enabled);
        JSONObject current;
        try{current=new JSONObject(HookEntry.graphicsSettingsStatus());}catch(Exception ex){current=new JSONObject();}
        enabled.setChecked(current.optBoolean("enabled"));
        SettingsChoiceField fps=choice(options,"目标帧率",new String[]{"0","30","60","90","120"},
            new String[]{"跟随游戏","30 FPS","60 FPS","90 FPS","120 FPS"},Integer.toString(current.optInt("fps")));
        SettingsChoiceField scale=choice(options,"3D 渲染倍率",new String[]{"0","0.75","1","1.25","1.5","2"},
            new String[]{"跟随游戏","0.75×","1× · 屏幕分辨率","1.25×","1.5×","2×"},formatScale(current.optDouble("scale",0)));
        SettingsChoiceField antialiasing=choice(options,"抗锯齿",new String[]{"0","1","2","3"},
            new String[]{"跟随游戏","关闭","FXAA","SMAA · 高质量"},Integer.toString(current.optInt("antialiasing")));
        options.addView(SettingsUiStyle.text(dialog.getContext(),
            "指定渲染倍率时关闭游戏自动降分辨率；保持画面比例，渲染目标最长边不超过 4096 像素。拍照保存的指定尺寸保持原值。FXAA / SMAA 会开启所需的后处理通道。",12,SettingsUiStyle.MUTED,false));
        LinearLayout live=SettingsUiStyle.card(page.body,"运行状态",null);
        TextView status=SettingsUiStyle.text(dialog.getContext(),"读取中",13,SettingsUiStyle.INK,false);live.addView(status);
        android.widget.Button apply=SettingsUiStyle.button(dialog.getContext(),"应用并保存",true,()->{
            try {
                JSONObject value=new JSONObject().put("schema_version",1).put("enabled",enabled.isChecked())
                    .put("fps",Integer.parseInt(fps.selectedId())).put("scale",Double.parseDouble(scale.selectedId()))
                    .put("antialiasing",Integer.parseInt(antialiasing.selectedId()));
                save(activity,value);
                Toast.makeText(activity,"已提交设置，运行状态将在游戏应用后更新",Toast.LENGTH_SHORT).show();
            }catch(Exception ex){Toast.makeText(activity,"无法应用画质设置，请检查运行状态",Toast.LENGTH_LONG).show();}
        });
        LinearLayout.LayoutParams applyLayout=new LinearLayout.LayoutParams(-1,-2);
        applyLayout.topMargin=SettingsUiStyle.dp(dialog.getContext(),12);
        apply.setEnabled(current.optBoolean("available"));options.addView(apply,applyLayout);
        LinearLayout.LayoutParams restoreLayout=new LinearLayout.LayoutParams(-1,-2);
        restoreLayout.topMargin=SettingsUiStyle.dp(dialog.getContext(),12);
        restoreLayout.bottomMargin=SettingsUiStyle.dp(dialog.getContext(),8);
        options.addView(SettingsUiStyle.button(dialog.getContext(),"恢复游戏默认",false,()->{
            try {
                save(activity,new JSONObject().put("schema_version",1).put("enabled",false).put("fps",0).put("scale",0).put("antialiasing",0));
                enabled.setChecked(false);dialog.dismiss();show(activity);
            }catch(Exception ex){Toast.makeText(activity,"恢复失败，请检查运行状态",Toast.LENGTH_LONG).show();}
        }),restoreLayout);
        Runnable poll=new Runnable(){public void run(){
            if(!dialog.isShowing())return;
            try {
                JSONObject value=new JSONObject(HookEntry.graphicsSettingsStatus());
                if(!value.optBoolean("pending"))GraphicsDisplayMode.apply(activity,value.optJSONObject("active"));
                apply.setEnabled(value.optBoolean("available"));
                if(!value.optBoolean("available"))status.setText("当前游戏版本的画质接口不可用");
                else if(!value.optString("error").isEmpty())status.setText("应用失败："+value.optString("error"));
                else status.setText((value.optBoolean("pending")?"等待游戏应用\n":"")+
                    "屏幕刷新率："+String.format(Locale.ROOT,"%.0f",activity.getWindowManager().getDefaultDisplay().getRefreshRate())+" Hz\n"+
                    "目标 / 实测："+value.optInt("target_fps")+" / "+String.format(Locale.ROOT,"%.1f",value.optDouble("measured_fps"))+" FPS\n"+
                    "屏幕："+value.optInt("screen_width")+" × "+value.optInt("screen_height")+"\n"+
                    "3D 目标："+value.optInt("render_width")+" × "+value.optInt("render_height")+"\n"+
                    "抗锯齿："+aaName(value.optInt("actual_antialiasing"))+(value.optBoolean("post_processing")?"":" · 后处理未开启"));
            }catch(Exception ex){status.setText("暂时无法读取运行状态");}
            status.postDelayed(this,1000);
        }};
        dialog.setOnDismissListener(d->status.removeCallbacks(poll));
        dialog.setContentView(page.root);SettingsUiStyle.configureWindow(dialog);dialog.show();status.post(poll);
    }
    private static void save(Context context,JSONObject value) {
        if(!HookEntry.setGraphicsSettings(value.toString()))throw new IllegalStateException();
        if(!context.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit().putString("settings",value.toString()).commit())
            throw new IllegalStateException();
        if(context instanceof Activity)GraphicsDisplayMode.schedule((Activity)context,0);
    }
    private static String aaName(int value){return value==1?"FXAA":value==2?"SMAA":"关闭";}
    private static String formatScale(double n){return n==(int)n?Integer.toString((int)n):Double.toString(n);}
    private static SettingsChoiceField choice(LinearLayout parent,String title,String[] ids,String[] labels,String selected){
        SettingsChoiceField field=new SettingsChoiceField(parent,title,null);
        ArrayList<SettingsChoiceField.Choice> rows=new ArrayList<>();
        for(int i=0;i<ids.length;i++)rows.add(new SettingsChoiceField.Choice(ids[i],labels[i]));
        field.setChoices(rows,selected,"");return field;
    }
}
