package com.wemu.engine.alsaserver;

import com.wemu.engine.xconnector.Client;
import com.wemu.engine.xconnector.ConnectionHandler;

import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public class ALSAClientConnectionHandler implements ConnectionHandler {
    private final boolean burstAligned;
    private final Set<ALSAClient> clients = ConcurrentHashMap.newKeySet();
    private final Object gainLock = new Object();
    private volatile float gainDb;

    public ALSAClientConnectionHandler() {
        this(false);
    }

    public ALSAClientConnectionHandler(boolean burstAligned) {
        this.burstAligned = burstAligned;
    }

    public void setGainDb(float gainDb) {
        // Keep publication and client insertion in one critical section. Without
        // this, a connection could be constructed with the old gain immediately
        // before an update, then join the set immediately after the update's
        // iteration and remain quiet for the whole session.
        synchronized (gainLock) {
            this.gainDb = gainDb;
            for (ALSAClient client : clients) client.setGainDb(gainDb);
        }
    }

    @Override
    public void handleNewConnection(Client client) {
        client.createIOStreams();
        ALSAClient alsa = new ALSAClient(burstAligned);
        synchronized (gainLock) {
            alsa.setGainDb(gainDb);
            clients.add(alsa);
            client.setTag(alsa);
        }
    }

    @Override
    public void handleConnectionShutdown(Client client) {
        ALSAClient alsa = (ALSAClient)client.getTag();
        if (alsa == null) return;
        synchronized (gainLock) {
            clients.remove(alsa);
        }
        alsa.release();
    }
}
