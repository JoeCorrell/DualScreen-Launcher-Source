package com.wemu.engine.core;

/** Pure parsing shared by the reported Wine topology and kernel affinity. */
public final class CpuAffinityPolicy {
    public static final String BEFORE_LAUNCH = "affinityBeforeLaunch";
    private CpuAffinityPolicy() {}

    public static int mask(String list) {
        if (list == null || list.trim().isEmpty()) return 0;
        int mask = 0;
        for (String part : list.split(",", -1)) {
            try {
                String token = part.trim();
                if (!token.matches("[0-9]{1,2}")) return 0;
                int core = Integer.parseInt(token);
                if (core >= 32) return 0;
                mask |= 1 << core;
            } catch (NumberFormatException malformed) { return 0; }
        }
        return Integer.bitCount(mask) >= 2 ? mask : 0;
    }

    public static String list(int mask) {
        if (Integer.bitCount(mask) < 2) return "";
        StringBuilder result = new StringBuilder();
        for (int core = 0; core < 32; ++core) if ((mask & (1 << core)) != 0) {
            if (result.length() > 0) result.append(',');
            result.append(core);
        }
        return result.toString();
    }

    public static String topology(String list) {
        int mask = mask(list);
        return mask == 0 ? "" : Integer.bitCount(mask) + ":" + list(mask);
    }
}
