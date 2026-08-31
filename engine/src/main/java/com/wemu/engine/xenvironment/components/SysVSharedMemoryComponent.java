package com.wemu.engine.xenvironment.components;

import com.wemu.engine.sysvshm.SysVSHMConnectionHandler;
import com.wemu.engine.sysvshm.SysVSHMRequestHandler;
import com.wemu.engine.sysvshm.SysVSharedMemory;
import com.wemu.engine.xconnector.UnixSocketConfig;
import com.wemu.engine.xconnector.XConnectorEpoll;
import com.wemu.engine.xenvironment.EnvironmentComponent;
import com.wemu.engine.xserver.SHMSegmentManager;
import com.wemu.engine.xserver.XServer;

public class SysVSharedMemoryComponent extends EnvironmentComponent {
    private XConnectorEpoll connector;
    public final UnixSocketConfig socketConfig;
    private SysVSharedMemory sysVSharedMemory;
    private final XServer xServer;

    public SysVSharedMemoryComponent(XServer xServer, UnixSocketConfig socketConfig) {
        this.xServer = xServer;
        this.socketConfig = socketConfig;
    }

    @Override
    public void start() {
        if (connector != null) return;
        sysVSharedMemory = new SysVSharedMemory();
        connector = new XConnectorEpoll(socketConfig, new SysVSHMConnectionHandler(sysVSharedMemory), new SysVSHMRequestHandler());
        connector.start();

        xServer.setSHMSegmentManager(new SHMSegmentManager(sysVSharedMemory));
    }

    @Override
    public void stop() {
        if (connector != null) {
            connector.stop();
            connector = null;
        }

        sysVSharedMemory.deleteAll();
    }
}
