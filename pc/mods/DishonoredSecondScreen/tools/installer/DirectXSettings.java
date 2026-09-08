package com.dualscreen.dishonoredinstaller;

import android.content.Context;
import android.os.Bundle;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.*;
import java.util.regex.*;
import java.util.zip.*;
import org.json.JSONObject;

/** Local maintenance of the Dishonored shortcut; never modifies the shared preset. */
final class DirectXSettings {
    static String hash(byte[] data) throws Exception {
        StringBuilder out=new StringBuilder();
        for(byte b:MessageDigest.getInstance("SHA-256").digest(data))out.append(String.format(Locale.ROOT,"%02x",b&255));
        return out.toString();
    }
    static String setting(String text,String key,String value) throws Exception {
        Pattern p=Pattern.compile("(?m)^"+Pattern.quote(key)+"=[^\\r\\n]*");
        Matcher m=p.matcher(text);
        if(m.find()) {DisplayBaseline.check(!m.find(),"Duplicate setting: "+key);return p.matcher(text).replaceFirst(Matcher.quoteReplacement(key+"="+value));}
        return text+(text.endsWith("\n")?"":"\n")+key+"="+value+"\n";
    }
    static void run(Context target,Context helper,Bundle result) throws Exception {
        DisplayBaseline.check(target.getPackageName().equals("com.dualscreen.launcher"),"Wrong target");
        File root=target.getFilesDir(), prefix=new File(root,"imagefs/home/xuser-3");
        File config=new File(prefix,".container");byte[] containerBefore=Files.readAllBytes(config.toPath());
        JSONObject container=new JSONObject(new String(containerBefore,StandardCharsets.UTF_8));
        DisplayBaseline.check(container.getInt("id")==3,"Wrong container");
        File shortcut=new File(prefix,".wine/drive_c/users/xuser/Desktop/Dishonored.desktop");
        byte[] before=Files.readAllBytes(shortcut.toPath());
        String text=new String(before,StandardCharsets.UTF_8);
        DisplayBaseline.check(text.contains("Name=Dishonored\n") && text.contains("Dishonored.exe"),"Wrong shortcut");
        Matcher section=Pattern.compile("(?ms)^\\[Extra Data\\]\\r?\\n.*?(?=^\\[|\\z)").matcher(text);
        DisplayBaseline.check(section.find(),"Shortcut extras missing");
        String extra=section.group();
        Matcher existing=Pattern.compile("(?m)^dxwrapperConfig=([^\\r\\n]*)").matcher(extra);
        String dx=existing.find()?existing.group(1):container.getString("dxwrapperConfig");
        LinkedHashMap<String,String> options=new LinkedHashMap<>();
        for(String token:dx.split(",")) {String[] pair=token.split("=",2);DisplayBaseline.check(pair.length==2,"Bad DirectX option");options.put(pair[0],pair[1]);}
        options.put("version","DXVK-2.6-2-gplasync-0");options.put("vkd3dVersion","VKD3D-2.9-0");
        StringBuilder updated=new StringBuilder();for(Map.Entry<String,String> item:options.entrySet()) {if(updated.length()>0)updated.append(',');updated.append(item.getKey()).append('=').append(item.getValue());}
        extra=setting(setting(extra,"dxwrapper","dxvk+vkd3d"),"dxwrapperConfig",updated.toString());
        String after=text.substring(0,section.start())+extra+text.substring(section.end());
        JSONObject manifest;
        try(InputStream in=helper.getAssets().open("directx-files.json")) {manifest=new JSONObject(new String(read(in,1024*1024),StandardCharsets.UTF_8));}
        Map<File,byte[]> files=new LinkedHashMap<>();
        try(ZipInputStream zip=new ZipInputStream(helper.getAssets().open("directx.zip"))) {
            ZipEntry entry;
            while((entry=zip.getNextEntry())!=null) {
                String name=entry.getName();
                DisplayBaseline.check(!entry.isDirectory() && manifest.has(name),"Unexpected runtime entry");
                DisplayBaseline.check(name.startsWith("DXVK/2.6-2-gplasync-0/") || name.startsWith("VKD3D/2.9-0/"),"Wrong runtime");
                File file=new File(root,"contents/"+name);
                DisplayBaseline.check(file.getCanonicalPath().startsWith(new File(root,"contents").getCanonicalPath()+File.separator),"Unsafe runtime path");
                byte[] bytes=read(zip,32*1024*1024);
                DisplayBaseline.check(hash(bytes).equals(manifest.getString(name)),"Runtime hash mismatch");
                DisplayBaseline.check(!files.containsKey(file),"Duplicate runtime entry");files.put(file,bytes);
                if(file.exists())DisplayBaseline.check(Arrays.equals(Files.readAllBytes(file.toPath()),bytes),"Installed runtime differs: "+name);
            }
        }
        DisplayBaseline.check(files.size()==manifest.length(),"Incomplete runtime bundle");
        File backup=new File(root,"dishonored-directx-backup-"+System.currentTimeMillis());
        DisplayBaseline.check(backup.mkdir(),"Backup failed");Files.write(new File(backup,"Dishonored.desktop").toPath(),before);
        for(Map.Entry<File,byte[]> entry:files.entrySet()) {
            File file=entry.getKey();DisplayBaseline.check(file.getParentFile().isDirectory() || file.getParentFile().mkdirs(),"Runtime directory failed");
            if(!file.exists())DisplayBaseline.write(file,entry.getValue());
            DisplayBaseline.check(Arrays.equals(Files.readAllBytes(file.toPath()),entry.getValue()),"Runtime readback failed");
        }
        DisplayBaseline.write(shortcut,after.getBytes(StandardCharsets.UTF_8));
        DisplayBaseline.check(new String(Files.readAllBytes(shortcut.toPath()),StandardCharsets.UTF_8).equals(after),"Shortcut readback failed");
        DisplayBaseline.check(Arrays.equals(containerBefore,Files.readAllBytes(config.toPath())),"Shared container changed");
        result.putString("status","dishonored-directx-configured");result.putString("dxwrapperConfig",updated.toString());
        result.putString("shortcut",shortcut.getAbsolutePath());result.putString("backup",backup.getAbsolutePath());
        result.putInt("verifiedRuntimeFiles",files.size());result.putString("sharedContainerSha256",hash(containerBefore));
    }
    static byte[] read(InputStream input,int max) throws IOException {
        ByteArrayOutputStream output=new ByteArrayOutputStream();byte[] buffer=new byte[32768];int n;
        while((n=input.read(buffer))!=-1) {DisplayBaseline.check(output.size()+n<=max,"Runtime too large");output.write(buffer,0,n);}return output.toByteArray();
    }
}
