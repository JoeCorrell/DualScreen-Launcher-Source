package com.wemu.engine.config;

import android.content.Context;

import com.wemu.engine.container.Container;
import com.wemu.engine.core.EnvVars;
import com.wemu.engine.core.FileUtils;
import com.wemu.engine.core.KeyValueSet;
import com.wemu.engine.xenvironment.ImageFs;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Turns a container's DX wrapper settings into environment variables.
 *
 * DXVK and WineD3D each had a dialog, and each dialog carried the code that
 * translated its own settings into the variables Wine reads. So starting a
 * container meant constructing two dialogs it never showed — which is how the
 * session ended up importing a piece of the interface in order to set
 * `DXVK_ASYNC`.
 *
 * The translation is the same, byte for byte. Only its address has changed.
 */
public final class DXWrapperConfig {

    private DXWrapperConfig() {}

    public static final String DEFAULT_CONFIG = Container.DEFAULT_DXWRAPPERCONFIG;

    /** Parses a stored config string, falling back to the container default. */
    public static KeyValueSet parse(Object config) {
        String data = config != null && !config.toString().isEmpty()
                ? config.toString()
                : DEFAULT_CONFIG;
        return new KeyValueSet(data);
    }

    /** DXVK and VKD3D. */
    public static void setDxvkEnvVars(Context context, KeyValueSet config, EnvVars envVars) {
        setDxvkEnvVars(context, config, envVars, 0);
    }

    /**
     * @param baseFpsCap when above zero, the rate the *game* is held to,
     *     overriding this config's own `framerate`. Frame generation multiplies
     *     what reaches the screen, so the cap the user chose has to bind here —
     *     before the layer — or capping to 30 caps the whole output to 30.
     */
    public static void setDxvkEnvVars(Context context, KeyValueSet config, EnvVars envVars,
                                      int baseFpsCap) {
        String content = "";

        /*
         * No swapchain sizing here for frame generation, deliberately.
         *
         * `dxgi.numBackBuffers` was set to 4 and then 6 to give the layer an
         * image to present its generated frames into, on the theory that its
         * non-blocking acquire was losing a race for a swapchain that was too
         * small. DXVK read the setting both times and reported `Image count: 4`
         * both times — the wrapper ICD grants what it grants — and the discards
         * did not change by one frame. The stall was in this app's compositor,
         * not in the guest; `RendererOptions.hostPresentMode` has it.
         */

        String framerate = baseFpsCap > 0 ? String.valueOf(baseFpsCap) : config.get("framerate");
        if (!framerate.isEmpty() && !framerate.equals("0")) {
            content += "dxgi.maxFrameRate = " + framerate + "; ";
            content += "d3d9.maxFrameRate = " + framerate;
            envVars.put("DXVK_FRAME_RATE", framerate);
        }

        String maxFrameLatency = config.get("maxFrameLatency");
        if (!maxFrameLatency.isEmpty() && !maxFrameLatency.equals("0")) {
            if (!content.isEmpty()) content += "; ";
            content += "dxgi.maxFrameLatency = 1";
        }

        String async = config.get("async");
        if (!async.isEmpty() && !async.equals("0")) envVars.put("DXVK_ASYNC", "1");

        String asyncCache = config.get("asyncCache");
        if (!asyncCache.isEmpty() && !asyncCache.equals("0")) envVars.put("DXVK_GPLASYNCCACHE", "1");

        String tuning = mobileTuning(config);
        if (!tuning.isEmpty()) {
            if (!content.isEmpty()) content += "; ";
            content += tuning;
        }

        if (!content.isEmpty()) envVars.put("DXVK_CONFIG", content);

        envVars.put("VKD3D_FEATURE_LEVEL", config.get("vkd3dLevel"));
    }

    /**
     * DXVK settings chosen for a phone rather than for a desktop.
     *
     * DXVK's defaults assume a discrete GPU with its own memory and cores to
     * spare. Neither is true here, and three of its assumptions cost real
     * frames on a tiler with unified memory:
     *
     * <ul>
     *   <li><b>Dynamic resources are uncached.</b> On a card with separate VRAM
     *       that is right — the CPU writes once and the GPU reads over PCIe. Here
     *       there is one pool of memory and the CPU reads back through an
     *       uncached mapping, which is roughly an order of magnitude slower than
     *       a cached one. Games that update vertex or constant buffers every
     *       frame — which is most of them — pay it every frame.</li>
     *   <li><b>Every barrier is honoured exactly.</b> Correct, and stricter than
     *       a tiler needs; relaxing it lets the driver keep more work in the
     *       tile rather than flushing to memory between passes.</li>
     *   <li><b>Pipelines compile on every spare core.</b> Which is every core the
     *       game wanted. Worse now that the guest can be pinned to the big
     *       cluster: without a cap, background compilation and the game compete
     *       for exactly the same four cores. Two threads still hides most
     *       compilation and leaves the game its cores.</li>
     * </ul>
     *
     * Off by default. Relaxed barriers in particular are a correctness trade,
     * and a container that already works is not a place to make one uninvited.
     *
     * <h2>The barriers are their own switch now</h2>
     *
     * They used to be part of this one, and the comment above already said they
     * were the risky third of it. `d3d11.ignoreGraphicsBarriers` tells DXVK to
     * skip synchronisation it inserted for a reason, and a game that draws its
     * interface into a resource the GPU has not finished with can get an
     * interface made of whatever was there.
     *
     * Separated on suspicion of causing exactly that, and the suspicion was
     * wrong: the container had been running with these on for a day before the
     * corruption appeared. The split stands on its own merits — a switch called
     * "tune for mobile" should not be able to produce a wrong frame, and the
     * two assumptions that remain are where its frames actually came from:
     * cached dynamic resources, and not letting pipeline compilation take every
     * core the guest was just pinned to. But nothing here is a diagnosis, and
     * turning `relaxedBarriers` back on restores exactly the prior behaviour.
     */
    private static String mobileTuning(KeyValueSet config) {
        String enabled = config.get("mobileTuning");
        if (enabled.isEmpty() || enabled.equals("0")) return "";

        String tuning = "d3d11.cachedDynamicResources = a"
                + "; dxvk.numCompilerThreads = 2";

        String barriers = config.get("relaxedBarriers");
        if (barriers.equals("1")) {
            tuning += "; d3d11.relaxedBarriers = True"
                    + "; d3d11.ignoreGraphicsBarriers = True";
        }
        return tuning;
    }

    /** WineD3D, which reads one packed string rather than a variable per setting. */
    public static void setWineD3DEnvVars(Context context, KeyValueSet config, EnvVars vars) {
        String deviceID = getDeviceIdFromGPUName(context, config.get("gpuName"));
        String vendorID = getVendorIdFromGPUName(context, config.get("vendorID"));
        String wined3dConfig = "csmt=0x" + config.get("csmt")
                + ",strict_shader_math=0x" + config.get("strict_shader_math")
                + ",OffscreenRenderingMode=" + config.get("OffscreenRenderingMode")
                + ",VideoMemorySize=" + config.get("videoMemorySize")
                + ",VideoPciDeviceID=" + deviceID
                + ",VideoPciVendorID=" + vendorID
                + ",renderer=" + config.get("renderer");
        vars.put("WINE_D3D_CONFIG", wined3dConfig);
    }

    public static String getDeviceIdFromGPUName(Context context, String gpuName) {
        return lookupGpuField(context, gpuName, "deviceID");
    }

    public static String getVendorIdFromGPUName(Context context, String gpuName) {
        return lookupGpuField(context, gpuName, "vendorID");
    }

    /**
     * Finds one field of the GPU the container claims to be.
     *
     * The two lookups were identical but for the field read, and both silently
     * swallowed their JSON exception. Kept silent — a card the table does not
     * know is a normal outcome, and Wine copes with an empty id — but written
     * once instead of twice.
     */
    private static String lookupGpuField(Context context, String gpuName, String field) {
        if (gpuName == null || gpuName.isEmpty()) return "";

        String gpuNameList = FileUtils.readString(context, "gpu_cards.json");
        String value = "";
        try {
            JSONArray jsonArray = new JSONArray(gpuNameList);
            for (int i = 0; i < jsonArray.length(); i++) {
                JSONObject entry = jsonArray.getJSONObject(i);
                if (entry.getString("name").contains(gpuName)) {
                    value = entry.getString(field);
                }
            }
        } catch (JSONException ignored) {
            // An unknown card is not an error; see above.
        }
        return value;
    }
}
