package com.wemu.engine.xenvironment.components;

import com.wemu.engine.alsaserver.ALSAClientConnectionHandler;
import com.wemu.engine.alsaserver.ALSARequestHandler;
import com.wemu.engine.xconnector.UnixSocketConfig;
import com.wemu.engine.xconnector.XConnectorEpoll;
import com.wemu.engine.xenvironment.EnvironmentComponent;

public class ALSAServerComponent extends EnvironmentComponent {
    private XConnectorEpoll connector;
    private final UnixSocketConfig socketConfig;
    private final boolean burstAligned;
    private final ALSAClientConnectionHandler clientHandler;

    public ALSAServerComponent(UnixSocketConfig socketConfig) {
        this(socketConfig, false);
    }

    public ALSAServerComponent(UnixSocketConfig socketConfig, boolean burstAligned) {
        this.socketConfig = socketConfig;
        this.burstAligned = burstAligned;
        this.clientHandler = new ALSAClientConnectionHandler(burstAligned);
    }

    /** Changes gain for current clients and for streams opened later in this session. */
    public void setGainDb(float gainDb) {
        clientHandler.setGainDb(gainDb);
    }

    @Override
    public void start() {
        if (connector != null) return;
        connector = new XConnectorEpoll(socketConfig,
                clientHandler, new ALSARequestHandler());
        connector.setMultithreadedClients(true);
        connector.start();
    }

    @Override
    public void stop() {
        if (connector != null) {
            connector.stop();
            connector = null;
        }
    }
}
