package com.wemu.engine.xenvironment.components;

import com.wemu.engine.xenvironment.EnvironmentComponent;
import com.wemu.engine.xconnector.ClientPresenceListener;
import com.wemu.engine.xconnector.XConnectorEpoll;
import com.wemu.engine.xconnector.UnixSocketConfig;
import com.wemu.engine.xserver.XClientConnectionHandler;
import com.wemu.engine.xserver.XClientRequestHandler;
import com.wemu.engine.xserver.XServer;

public class XServerComponent extends EnvironmentComponent {
    private XConnectorEpoll connector;
    private XClientConnectionHandler connectionHandler;
    private ClientPresenceListener presenceListener;
    private final XServer xServer;
    private final UnixSocketConfig socketConfig;

    public XServerComponent(XServer xServer, UnixSocketConfig socketConfig) {
        this.xServer = xServer;
        this.socketConfig = socketConfig;
    }

    /**
     * Watches how many clients the server has.
     *
     * Settable before or after {@link #start()}: components are added to the
     * environment in one order and started in another, and a caller should not
     * have to know which side of the line it is on.
     */
    public void setClientPresenceListener(ClientPresenceListener listener) {
        this.presenceListener = listener;
        if (connectionHandler != null) connectionHandler.setPresenceListener(listener);
    }

    @Override
    public void start() {
        if (connector != null) return;
        connectionHandler = new XClientConnectionHandler(xServer);
        connectionHandler.setPresenceListener(presenceListener);
        connector = new XConnectorEpoll(socketConfig, connectionHandler, new XClientRequestHandler());
        connector.setInitialInputBufferCapacity(262144);
        connector.setCanReceiveAncillaryMessages(true);
        connector.start();
    }

    @Override
    public void stop() {
        if (connector != null) {
            /*
             * Wake extension-owned waits before joining the connector thread.
             * In particular SYNC AwaitFence runs on that thread; stopping the
             * socket first used to wait forever for a fence whose producer had
             * already been killed.
             */
            xServer.shutdown();
            connector.stop();
            connector = null;
            connectionHandler = null;
        }
    }

    public XServer getXServer() {
        return xServer;
    }
}
