package com.wemu.engine.core;

import static org.junit.Assert.*;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class PrefixSuspendStateTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();
    private File process(File root, int pid, String prefix, String state, String start) throws Exception {
        File process=new File(root,Integer.toString(pid)); process.mkdirs();
        Files.write(new File(process,"environ").toPath(),("PATH=/bin\0WINEPREFIX="+prefix+"\0").getBytes(StandardCharsets.UTF_8));
        String[] fields=new String[20]; Arrays.fill(fields,"0"); fields[0]=state; fields[19]=start;
        Files.write(new File(process,"stat").toPath(),(pid+" (wine worker (1)) "+String.join(" ",fields)).getBytes(StandardCharsets.UTF_8));
        return process;
    }
    @Test public void onlyOwnPrefixAndOwnedStopsAreResumed() throws Exception {
        File root=temporary.newFolder();
        process(root,10,"/isaac","S","100"); process(root,11,"/other","S","101");
        process(root,12,"/isaac","T","102");
        PrefixSuspendState state=new PrefixSuspendState();
        List<String> signals=new ArrayList<>();
        PrefixSuspendState.Signal signal=(pid,stop)->signals.add(pid+":"+stop);
        state.suspend(root,"/isaac",signal); state.suspend(root,"/isaac",signal);
        state.resume(root,signal); state.resume(root,signal);
        assertEquals(Arrays.asList("10:true","10:false"),signals);
    }
    @Test public void reusedPidIsNeverResumed() throws Exception {
        File root=temporary.newFolder(); process(root,10,"/isaac","S","100");
        PrefixSuspendState state=new PrefixSuspendState(); List<String> signals=new ArrayList<>();
        PrefixSuspendState.Signal signal=(pid,stop)->signals.add(pid+":"+stop);
        state.suspend(root,"/isaac",signal); process(root,10,"/isaac","S","200");
        state.resume(root,signal); assertEquals(Arrays.asList("10:true"),signals);
    }
    @Test public void environmentMatchIsExactAndStatAcceptsSpacesAndParentheses() {
        assertFalse(PrefixSuspendState.matchesPrefix("WINEPREFIX=/isaac-old\0".getBytes(StandardCharsets.UTF_8),"/isaac"));
        assertFalse(PrefixSuspendState.matchesPrefix("WINEPREFIX=/isaac\0".getBytes(StandardCharsets.UTF_8),""));
        assertNull(PrefixSuspendState.identity("bad"));
    }
}
