package com.wemu.engine.core;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import org.junit.Test;
import static org.junit.Assert.*;

public class CpuAffinityScopeTest {
    static class Fake implements CpuAffinityScope.Scheduler {
        int current=0xb8, sets, gets, setError; boolean restrict;
        public int get() { ++gets; return current; }
        public int set(int mask) { ++sets; if(setError!=0) return setError; current=restrict?mask&0xb8:mask; return 0; }
    }
    @Test public void childSeesCorrectMaskAndParentIsRestored() throws Exception {
        Fake scheduler=new Fake(); ArrayList<String> log=new ArrayList<>();
        CpuAffinityScope scope=CpuAffinityScope.open(0xf8,scheduler,log::add);
        try(scope) { assertEquals(0xf8,scheduler.current); }
        assertEquals(0xb8,scheduler.current); assertEquals(2,scheduler.sets);
        scope.close(); assertEquals(2,scheduler.sets); assertEquals(1,log.size());
    }
    @Test public void failedSpawnStillRestoresParent() throws Exception {
        Fake scheduler=new Fake();
        try (CpuAffinityScope ignored=CpuAffinityScope.open(0xf8,scheduler,x->{})) {
            throw new IOException("synthetic spawn failure");
        } catch(IOException expected) { assertEquals("synthetic spawn failure",expected.getMessage()); }
        assertEquals(0xb8,scheduler.current);
    }
    @Test public void restrictionsAreNotMisreportedAsFiveCores() throws Exception {
        Fake scheduler=new Fake(); scheduler.restrict=true;
        HashMap<String,String> environment=new HashMap<>();
        environment.put("WINE_CPU_TOPOLOGY","5:3,4,5,6,7");
        environment.put("FEX_TSOENABLED","0");
        try(CpuAffinityScope scope=CpuAffinityScope.open(0xf8,scheduler,x->{})) {
            scope.configureEnvironment(environment);
            assertEquals(0xb8,scheduler.current);
            assertEquals("4:3,4,5,7",environment.get("WINE_CPU_TOPOLOGY"));
            assertEquals("0",environment.get("FEX_TSOENABLED"));
        }
        assertEquals(0xb8,scheduler.current); assertEquals(2,scheduler.sets);
    }
    @Test public void observedThorD8RestrictionLaunchesWithMatchingTopology() throws Exception {
        Fake scheduler=new Fake() {
            public int set(int mask) { ++sets; current=mask&0xd8; return 0; }
        };
        scheduler.current=0xd8;
        HashMap<String,String> environment=new HashMap<>();
        try(CpuAffinityScope scope=CpuAffinityScope.open(0xf8,scheduler,x->{})) {
            scope.configureEnvironment(environment);
            assertEquals("4:3,4,6,7",environment.get("WINE_CPU_TOPOLOGY"));
            assertEquals(0xd8,scheduler.current);
        }
        assertEquals(0xd8,scheduler.current);
    }
    @Test public void invalidReadbacksStillAbortAndRestore() throws Exception {
        for(int invalid:new int[]{0,-1,0x80,0xff}) {
            Fake scheduler=new Fake() {
                public int get() { return ++gets==2?invalid:current; }
            };
            try { CpuAffinityScope.open(0xf8,scheduler,x->{}); fail(); }
            catch(IOException expected) { assertTrue(expected.getMessage().contains("valid multi-core subset")); }
            assertEquals(0xb8,scheduler.current); assertEquals(2,scheduler.sets);
        }
    }
    @Test public void unchangedAndDefaultTopologyAreChildLocal() throws Exception {
        Fake scheduler=new Fake();
        HashMap<String,String> environment=new HashMap<>();
        environment.put("WINE_CPU_TOPOLOGY","existing");
        try(CpuAffinityScope scope=CpuAffinityScope.open(0,scheduler,x->{})) {
            scope.configureEnvironment(environment);
            assertEquals("existing",environment.get("WINE_CPU_TOPOLOGY"));
        }
        try(CpuAffinityScope scope=CpuAffinityScope.open(0xf8,scheduler,x->{})) {
            scope.configureEnvironment(environment);
            assertEquals("5:3,4,5,6,7",environment.get("WINE_CPU_TOPOLOGY"));
        }
    }
    @Test public void defaultAndSingleCoreRequestsDoNotTouchScheduler() throws Exception {
        Fake scheduler=new Fake();
        try(CpuAffinityScope ignored=CpuAffinityScope.open(0,scheduler,x->{})) {}
        try(CpuAffinityScope ignored=CpuAffinityScope.open(8,scheduler,x->{})) {}
        assertEquals(0,scheduler.sets); assertEquals(0,scheduler.gets);
    }
    @Test public void refusedSetDoesNotSpawnOrChangeTheParent() throws Exception {
        Fake scheduler=new Fake(); scheduler.setError=1;
        try { CpuAffinityScope.open(0xf8,scheduler,x->{}); fail(); }
        catch(IOException expected) { assertTrue(expected.getMessage().contains("refused")); }
        assertEquals(0xb8,scheduler.current); assertEquals(1,scheduler.sets);
    }
    @Test public void restoreFailureIsReportedWithoutLosingCreatedChild() throws Exception {
        Fake scheduler=new Fake(); ArrayList<String> log=new ArrayList<>();
        try(CpuAffinityScope ignored=CpuAffinityScope.open(0xf8,scheduler,log::add)) { scheduler.setError=1; }
        assertEquals(2,log.size()); assertTrue(log.get(1).contains("restoration failed"));
    }
}
