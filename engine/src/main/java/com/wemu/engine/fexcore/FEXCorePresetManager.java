package com.wemu.engine.fexcore;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;

import androidx.preference.PreferenceManager;

import com.wemu.engine.R;
import com.wemu.engine.core.WemuPaths;
import com.wemu.engine.core.AppUtils;
import com.wemu.engine.core.EnvVars;
import com.wemu.engine.core.FileUtils;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.Locale;

public class FEXCorePresetManager {
    public static EnvVars getEnvVars(Context context, String id) {
        EnvVars envVars = new EnvVars();

        if (id.equals(FEXCorePreset.STABILITY)) {
            envVars.put("FEX_TSOENABLED", "1");
            envVars.put("FEX_VECTORTSOENABLED", "1");
            envVars.put("FEX_MEMCPYSETTSOENABLED", "1");
            envVars.put("FEX_HALFBARRIERTSOENABLED", "1");
            envVars.put("FEX_X87REDUCEDPRECISION", "0");
            envVars.put("FEX_MULTIBLOCK", "0");
        }
        else if (id.equals(FEXCorePreset.COMPATIBILITY)) {
            envVars.put("FEX_TSOENABLED", "1");
            envVars.put("FEX_VECTORTSOENABLED", "1");
            envVars.put("FEX_MEMCPYSETTSOENABLED", "1");
            envVars.put("FEX_HALFBARRIERTSOENABLED", "1");
            envVars.put("FEX_X87REDUCEDPRECISION", "0");
            envVars.put("FEX_MULTIBLOCK", "1");
        }
        else if (id.equals(FEXCorePreset.INTERMEDIATE)) {
            envVars.put("FEX_TSOENABLED", "1");
            envVars.put("FEX_VECTORTSOENABLED", "0");
            envVars.put("FEX_MEMCPYSETTSOENABLED", "0");
            envVars.put("FEX_HALFBARRIERTSOENABLED", "1");
            envVars.put("FEX_X87REDUCEDPRECISION", "1");
            envVars.put("FEX_MULTIBLOCK", "1");
        }
        else if (id.equals(FEXCorePreset.PERFORMANCE)) {
            envVars.put("FEX_TSOENABLED", "0");
            envVars.put("FEX_VECTORTSOENABLED", "0");
            envVars.put("FEX_MEMCPYSETTSOENABLED", "0");
            envVars.put("FEX_HALFBARRIERTSOENABLED", "0");
            envVars.put("FEX_X87REDUCEDPRECISION", "1");
            envVars.put("FEX_MULTIBLOCK", "1");
            envVars.put("FEX_DYNAMICL1CACHE", "1");
            envVars.put("FEX_DISABLEL2CACHE", "0");
        }
        else if (id.equals(FEXCorePreset.EXTREME)) {
            envVars.put("FEX_TSOENABLED", "0");
            envVars.put("FEX_VECTORTSOENABLED", "0");
            envVars.put("FEX_MEMCPYSETTSOENABLED", "0");
            envVars.put("FEX_HALFBARRIERTSOENABLED", "0");
            envVars.put("FEX_X87REDUCEDPRECISION", "1");
            envVars.put("FEX_MULTIBLOCK", "1");
            envVars.put("FEX_DYNAMICL1CACHE", "1");
            envVars.put("FEX_DISABLEL2CACHE", "0");
            /*
             * The two that Performance leaves alone.
             *
             * `SMALLTSCSCALE` lets FEX scale the timestamp counter with cheap
             * arithmetic instead of a full 128-bit multiply, and a game that
             * reads the clock every frame — which is all of them — pays that on
             * the hot path. `VOLATILEMETADATA` lets it trust the metadata a
             * binary declares about which memory is volatile rather than
             * assuming the worst everywhere.
             *
             * Above Performance rather than inside it because both trade a
             * little correctness for speed, and the game that breaks under them
             * should have somewhere to step back to.
             */
            envVars.put("FEX_SMALLTSCSCALE", "1");
            envVars.put("FEX_VOLATILEMETADATA", "1");
        }
        else if (id.startsWith(FEXCorePreset.CUSTOM)) {
            for (String[] preset : customPresetsIterator(context)) {
                if (preset[0].equals(id)) {
                    envVars.putAll(preset[2]);
                    break;
                }
            }
        }

        return envVars;
    }

    public static ArrayList<FEXCorePreset> getPresets(Context context) {
        ArrayList<FEXCorePreset> presets = new ArrayList<>();
        presets.add(new FEXCorePreset(FEXCorePreset.STABILITY, context.getString(R.string.stability)));
        presets.add(new FEXCorePreset(FEXCorePreset.COMPATIBILITY, context.getString(R.string.compatibility)));
        presets.add(new FEXCorePreset(FEXCorePreset.INTERMEDIATE, context.getString(R.string.intermediate)));
        presets.add(new FEXCorePreset(FEXCorePreset.PERFORMANCE, context.getString(R.string.performance)));
        presets.add(new FEXCorePreset(FEXCorePreset.EXTREME, context.getString(R.string.preset_extreme)));
        for (String[] preset : customPresetsIterator(context)) presets.add(new FEXCorePreset(preset[0], preset[1]));
        return presets;
    }

    public static FEXCorePreset getPreset(Context context, String id) {
        for (FEXCorePreset preset : getPresets(context)) if (preset.id.equals(id)) return preset;
        return null;
    }

    private static Iterable<String[]> customPresetsIterator(Context context) {
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
        final String customPresetsStr = preferences.getString("fexcore_custom_presets", "");
        final String[] customPresets = customPresetsStr.split(",");
        final int[] index = {0};
        return () -> new Iterator<String[]>() {
            @Override
            public boolean hasNext() {
                return index[0] < customPresets.length && !customPresetsStr.isEmpty();
            }

            @Override
            public String[] next() {
                return customPresets[index[0]++].split("\\|");
            }
        };
    }

    public static int getNextPresetId(Context context) {
        int maxId = 0;
        for (String[] preset : customPresetsIterator(context)) {
            maxId = Math.max(maxId, Integer.parseInt(preset[0].replace(FEXCorePreset.CUSTOM+"-", "")));
        }
        return maxId+1;
    }

    public static void editPreset(Context context, String id, String name, EnvVars envVars) {
        String key = "fexcore_custom_presets";
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
        String customPresetsStr = preferences.getString(key, "");

        if (id != null) {
            String[] customPresets = customPresetsStr.split(",");
            for (int i = 0; i < customPresets.length; i++) {
                String[] preset = customPresets[i].split("\\|");
                if (preset[0].equals(id)) {
                    customPresets[i] = id+"|"+name+"|"+envVars.toString();
                    break;
                }
            }
            customPresetsStr = String.join(",", customPresets);
        }
        else {
            String preset = FEXCorePreset.CUSTOM+"-"+getNextPresetId(context)+"|"+name+"|"+envVars.toString();
            customPresetsStr += (!customPresetsStr.isEmpty() ? "," : "")+preset;
        }
        preferences.edit().putString(key, customPresetsStr).apply();
    }

    public static void duplicatePreset(Context context, String id) {
        ArrayList<FEXCorePreset> presets = getPresets(context);
        FEXCorePreset originPreset = null;
        for (FEXCorePreset preset : presets) {
            if (preset.id.equals(id)) {
                originPreset = preset;
                break;
            }
        }
        if (originPreset == null) return;

        String newName;
        for (int i = 1;;i++) {
            newName = originPreset.name+" ("+i+")";
            boolean found = false;
            for (FEXCorePreset preset : presets) {
                if (preset.name.equals(newName)) {
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }

        editPreset(context, null, newName, getEnvVars(context, originPreset.id));
    }

    public static void removePreset(Context context, String id) {
        String key = "fexcore_custom_presets";
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
        String oldCustomPresetsStr = preferences.getString(key, "");
        String newCustomPresetsStr = "";

        String[] customPresets = oldCustomPresetsStr.split(",");
        for (int i = 0; i < customPresets.length; i++) {
            String[] preset = customPresets[i].split("\\|");
            if (!preset[0].equals(id)) newCustomPresetsStr += (!newCustomPresetsStr.isEmpty() ? "," : "")+customPresets[i];
        }

        preferences.edit().putString(key, newCustomPresetsStr).apply();
    }

    public static void exportPreset(Context context, String id) {
        File presetFile = null;
        String key = "fexcore_custom_presets";
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
        String[] customPresets = preferences.getString(key, "").split(",");

        for (int i = 0; i < customPresets.length; i++) {
            String[] preset = customPresets[i].split("\\|");
            if (preset[0].equals(id)) {;
                String uriPath = preferences.getString("shared_path_uri", null);
                if (uriPath != null) {
                    Uri uri = Uri.parse(uriPath);
                    String path = FileUtils.getFilePathFromUri(context, uri);
                    presetFile = new File(path, "Presets/fexcore_" + preset[1] + ".wbp");
                }
                else {
                    presetFile = new File(WemuPaths.DEFAULT_SHARED_PATH, "Presets/fexcore_" + preset[1] + ".wbp");
                }
                if (!presetFile.getParentFile().exists())
                    presetFile.getParentFile().mkdirs();

                try {
                    FileOutputStream fos = new FileOutputStream(presetFile);
                    PrintWriter pw = new PrintWriter(fos);
                    pw.write("ID:" + preset[0] + "\n");
                    pw.write("Name:" + preset[1] + "\n");
                    pw.write("EnvVars:" + preset[2] + "\n");
                    pw.close();
                    fos.close();
                } catch (IOException e) {
                }
                break;
            }
        }
        if (presetFile != null && presetFile.exists())
            AppUtils.showToast(context, "Preset " + presetFile.getName() + " exported successfully at " + presetFile.getParentFile().getPath());
        else
            AppUtils.showToast(context, "Failed to export preset");
    }

    public static void importPreset(Context context, InputStream stream) {
        String key = "fexcore_custom_presets";
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(context);
        String customPresetStr = preferences.getString(key, "");
        ArrayList<String> lines = new ArrayList<>();

        try {
            String[] preset = new String[3];
            BufferedReader reader = new BufferedReader(new InputStreamReader(stream));
            String line;
            while ((line = reader.readLine()) != null) {
                lines.add(line);
            }
            for (int i = 0; i < lines.size(); i++) {
                String[] contents = lines.get(i).split(":");
                switch (contents[0]) {
                    case "ID":
                        preset[0] = contents[1];
                        break;
                    case "Name":
                        preset[1] = contents[1];
                        break;
                    case "EnvVars":
                        preset[2] = contents[1];
                        break;
                }
            }
            customPresetStr = customPresetStr + (!customPresetStr.equals("") ? "," : "") + FEXCorePreset.CUSTOM+"-"+getNextPresetId(context) + "|" + preset[1] + "|" + preset[2];
        } catch (IOException e) {
        }

        preferences.edit().putString(key, customPresetStr).apply();
    }

    /**
     * Where a preset sits in the list, or 0 when it is not in it.
     *
     * Replaces `loadSpinner` and `getSpinnerSelectedId`, which between them were
     * the last `Spinner` the engine touched. Filling a widget and reading a
     * selection back out of it are not this class's job — it knows about
     * presets, and this is the only thing a list of them needs that the list
     * itself cannot answer.
     *
     * Zero rather than -1 for an unknown id, because the caller's next move is
     * always to select that index and a preset that no longer exists should
     * land on the first one rather than on nothing.
     */
    public static int indexOfPreset(ArrayList<FEXCorePreset> presets, String selectedId) {
        for (int i = 0; i < presets.size(); i++) {
            if (presets.get(i).id.equals(selectedId)) return i;
        }
        return 0;
    }
}
