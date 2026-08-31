package com.wemu.engine.xserver;

import com.wemu.engine.xconnector.Client;
import com.wemu.engine.xconnector.ClientPresenceListener;
import com.wemu.engine.xconnector.ConnectionHandler;

import java.util.HashSet;

public class XClientConnectionHandler implements ConnectionHandler {
    private final XServer xServer;

    /*
     * The live clients, rather than a counter.
     *
     * `XConnectorEpoll.killConnection` is reachable from more than one place —
     * a failed read, an explicit close, the connector shutting down — and
     * nothing guarantees a given client passes through here exactly once. A
     * counter would drift on a double shutdown and never reach zero again,
     * which for anyone watching this for the end of a session means a guest
     * that has gone and a session that never notices.
     *
     * A set makes the second shutdown a no-op, which is what it is.
     */
    private final HashSet<Client> liveClients = new HashSet<>();
    private ClientPresenceListener presenceListener;

    public XClientConnectionHandler(XServer xServer) {
        this.xServer = xServer;
    }

    public void setPresenceListener(ClientPresenceListener presenceListener) {
        int count;
        synchronized (liveClients) {
            this.presenceListener = presenceListener;
            count = liveClients.size();
        }
        // Told the current count on arrival, so a listener attached after the
        // first client connected is not waiting for a change that already
        // happened.
        if (presenceListener != null) presenceListener.onClientCountChanged(count);
    }

    @Override
    public void handleNewConnection(Client client) {
        client.createIOStreams();
        client.setTag(new XClient(xServer, client.getInputStream(), client.getOutputStream()));
        notifyPresence(client, true);
    }

    @Override
    public void handleConnectionShutdown(Client client) {
        ((XClient)client.getTag()).freeResources();
        notifyPresence(client, false);
    }

    private void notifyPresence(Client client, boolean connected) {
        ClientPresenceListener listener;
        int count;
        synchronized (liveClients) {
            boolean changed = connected ? liveClients.add(client) : liveClients.remove(client);
            if (!changed) return;
            listener = presenceListener;
            count = liveClients.size();
        }
        if (listener != null) listener.onClientCountChanged(count);
    }
}
