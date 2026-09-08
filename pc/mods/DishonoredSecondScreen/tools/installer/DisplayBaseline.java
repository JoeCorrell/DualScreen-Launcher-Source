package com.dualscreen.dishonoredinstaller;

import android.content.Context;
import android.os.Bundle;
import android.util.AtomicFile;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.regex.*;
import org.json.JSONObject;

final class DisplayBaseline {
    static void restoreDual(Context context, Bundle result) throws Exception {
        check(context.getPackageName().equals("com.dualscreen.launcher"), "Wrong target");
        File config=new File(context.getFilesDir(),"imagefs/home/xuser-3/.container");
        byte[] before=Files.readAllBytes(config.toPath());
        JSONObject value=new JSONObject(new String(before,StandardCharsets.UTF_8));
        check(value.getInt("id")==3 && value.getString("screenSize").equals("1280x720"),"Unexpected display profile");
        File flag=new File(context.getFilesDir(),"dishonored-single-display");
        byte[] flagBytes=flag.isFile()?Files.readAllBytes(flag.toPath()):null;
        File backup=new File(context.getFilesDir(),"dishonored-dual-restore-"+System.currentTimeMillis());
        check(backup.mkdir(),"Cannot create restore backup");
        Files.write(new File(backup,"container.json").toPath(),before);
        if(flagBytes!=null)Files.write(new File(backup,"single-display-override").toPath(),flagBytes);
        value.getJSONObject("extraData").put("displaySecondScreen","1");
        try {
            write(config,value.toString().getBytes(StandardCharsets.UTF_8));
            check(!flag.exists() || flag.delete(),"Cannot remove single-display override");
        } catch(Exception failure) {write(config,before);if(flagBytes!=null)write(flag,flagBytes);throw failure;}
        check(!flag.exists() && new String(Files.readAllBytes(config.toPath()),StandardCharsets.UTF_8).equals(value.toString()),"Dual display verification failed");
        result.putString("status","dual-display-configured");result.putString("backup",backup.getAbsolutePath());
        result.putString("screenSize","1280x720");result.putString("secondScreen","1");
    }
    static void check(boolean ok, String message) throws IOException { if (!ok) throw new IOException(message); }
    static void write(File file, byte[] bytes) throws IOException {
        AtomicFile target = new AtomicFile(file); FileOutputStream out = null;
        try { out=target.startWrite();out.write(bytes);target.finishWrite(out); }
        catch(IOException e) { if(out!=null)target.failWrite(out);throw e; }
    }
    static void run(Context context, boolean apply, Bundle result) throws Exception {
        check(context.getPackageName().equals("com.dualscreen.launcher"), "Wrong target");
        File prefix = new File(context.getFilesDir(), "imagefs/home/xuser-3");
        File config = new File(prefix, ".container"), registry = new File(prefix, ".wine/user.reg");
        byte[] configBytes=Files.readAllBytes(config.toPath()), registryBytes=Files.readAllBytes(registry.toPath());
        JSONObject value=new JSONObject(new String(configBytes, StandardCharsets.UTF_8));
        check(value.getInt("id")==3, "Wrong container");
        result.putString("container", value.toString());
        String text=new String(registryBytes, StandardCharsets.UTF_8);
        Pattern section=Pattern.compile("(?ms)^\\[Software\\\\\\\\Arkane\\\\\\\\Dishonored\\].*?(?=^\\[|\\z)");
        Matcher matcher=section.matcher(text);check(matcher.find(), "Dishonored registry section missing");
        String before=matcher.group();result.putString("gameRegistry", before);
        File documents=new File(prefix, ".wine/drive_c/users/xuser/Documents/My Games/Dishonored");
        inspect(documents, 0, result);
        if(!apply) {result.putString("status", "display-inspected");return;}
        value.put("screenSize", "1280x720");
        value.getJSONObject("extraData").put("displaySecondScreen", "0");
        String after=before;
        for(String[] item:new String[][]{{"ResX","00000500"},{"ResY","000002d0"},{"Fullscreen","00000000"}}) {
            Pattern entry=Pattern.compile("(?m)^\""+item[0]+"\"=dword:[0-9a-fA-F]{8}\\r?$");
            Matcher found=entry.matcher(after);check(found.find(), "Missing registry value "+item[0]);
            after=found.replaceAll(Matcher.quoteReplacement("\""+item[0]+"\"=dword:"+item[1]));
        }
        String replacement=text.substring(0,matcher.start())+after+text.substring(matcher.end());
        File engine=new File(documents,"DishonoredGame/Config/DishonoredEngine.ini");
        byte[] engineBytes=Files.readAllBytes(engine.toPath());
        String engineText=new String(engineBytes,StandardCharsets.UTF_8);
        Matcher settings=Pattern.compile("(?ms)^\\[SystemSettings\\]\\r?\\n.*?(?=^\\[|\\z)").matcher(engineText);
        check(settings.find(), "Engine SystemSettings missing");
        String display=settings.group();
        for(String[] item:new String[][]{{"ResX","1280"},{"ResY","720"},{"Fullscreen","False"}}) {
            Matcher setting=Pattern.compile("(?m)^"+item[0]+"=[^\\r\\n]*").matcher(display);
            check(setting.find(),"Engine setting missing: "+item[0]);
            display=setting.replaceAll(Matcher.quoteReplacement(item[0]+"="+item[1]));
        }
        String engineAfter=engineText.substring(0,settings.start())+display+engineText.substring(settings.end());
        File backup=new File(context.getFilesDir(), "dishonored-single-display-backup-"+System.currentTimeMillis());
        check(backup.mkdir(), "Cannot create backup");
        Files.write(new File(backup,"container.json").toPath(),configBytes);
        Files.write(new File(backup,"user.reg").toPath(),registryBytes);
        Files.write(new File(backup,"DishonoredEngine.ini").toPath(),engineBytes);
        try {
            write(config,value.toString().getBytes(StandardCharsets.UTF_8));
            write(registry,replacement.getBytes(StandardCharsets.UTF_8));
            write(engine,engineAfter.getBytes(StandardCharsets.UTF_8));
            write(new File(context.getFilesDir(),"dishonored-single-display"),"1280x720\n".getBytes(StandardCharsets.US_ASCII));
        } catch(Exception failure) {write(config,configBytes);write(registry,registryBytes);write(engine,engineBytes);throw failure;}
        check(new String(Files.readAllBytes(config.toPath()), StandardCharsets.UTF_8).equals(value.toString()), "Config verification failed");
        check(new String(Files.readAllBytes(registry.toPath()), StandardCharsets.UTF_8).equals(replacement), "Registry verification failed");
        check(new String(Files.readAllBytes(engine.toPath()), StandardCharsets.UTF_8).equals(engineAfter), "Engine verification failed");
        result.putString("backup",backup.getAbsolutePath());result.putString("status","single-display-configured");
        result.putString("gameRegistryAfter",after);
    }
    static void inspect(File folder,int depth,Bundle result) throws IOException {
        if(depth>3 || !folder.isDirectory())return;
        File[] files=folder.listFiles();if(files==null)return;
        for(File file:files) {
            if(file.isDirectory()) {if(!file.getName().equalsIgnoreCase("SaveData"))inspect(file,depth+1,result);continue;}
            if(!file.getName().endsWith(".ini") || file.length()>1024*1024)continue;
            String content=new String(Files.readAllBytes(file.toPath()),StandardCharsets.UTF_8);
            Matcher setting=Pattern.compile("(?im)^(?:ResX|ResY|Fullscreen|bFullscreen|bUseDesktopResolutionForFullscreen|ResolutionSizeX|ResolutionSizeY)=.*$").matcher(content);
            StringBuilder values=new StringBuilder();while(setting.find())values.append(setting.group()).append('\n');
            if(values.length()>0)result.putString(file.getAbsolutePath(),values.toString());
        }
    }
}
