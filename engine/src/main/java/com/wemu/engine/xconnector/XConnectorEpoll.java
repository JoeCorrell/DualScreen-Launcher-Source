package com.wemu.engine.xconnector;

import android.util.SparseArray;

import androidx.annotation.Keep;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

public class XConnectorEpoll implements Runnable {
    private final ConnectionHandler connectionHandler;
    private final RequestHandler requestHandler;
    private final int epollFd;
    private final int serverFd;
    private final int shutdownFd;
    private Thread epollThread;
    private volatile boolean running = false;
    private boolean multithreadedClients = false;
    private boolean canReceiveAncillaryMessages = false;
    private int initialInputBufferCapacity = 4096;
    private int initialOutputBufferCapacity = 4096;
    private final Object clientsLock = new Object();
    private final SparseArray<Client> connectedClients = new SparseArray<>();

    static {
        System.loadLibrary("wemu");
    }

    public XConnectorEpoll(UnixSocketConfig socketConfig, ConnectionHandler connectionHandler, RequestHandler requestHandler) {
        this.connectionHandler = connectionHandler;
        this.requestHandler = requestHandler;

        serverFd = createAFUnixSocket(socketConfig.path);
        if (serverFd < 0) {
            throw new RuntimeException("Failed to create an AF_UNIX socket.");
        }

        epollFd = createEpollFd();
        if (epollFd < 0) {
            closeFd(serverFd);
            throw new RuntimeException("Failed to create epoll fd.");
        }

        if (!addFdToEpoll(epollFd, serverFd)) {
            closeFd(serverFd);
            closeFd(epollFd);
            throw new RuntimeException("Failed to add server fd to epoll.");
        }

        shutdownFd = createEventFd();
        if (!addFdToEpoll(epollFd, shutdownFd)) {
            closeFd(serverFd);
            closeFd(shutdownFd);
            closeFd(epollFd);
            throw new RuntimeException("Failed to add shutdown fd to epoll.");
        }

        epollThread = new Thread(this);
    }

    public synchronized void start() {
        if (running || epollThread == null) return;
        running = true;
        epollThread.start();
    }

    public synchronized void stop() {
        if (epollThread == null) return;
        running = false;
        requestShutdown();
        if (Thread.currentThread() == epollThread) return;

        try {
            epollThread.join(2000);
            if (epollThread.isAlive()) {
                epollThread.interrupt();
                requestShutdown();
                epollThread.join(250);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        if (epollThread.isAlive()) {
            /*
             * Java cannot safely kill a thread which is inside a client request.
             * Returning is preferable to pinning the process-wide session
             * cleanup chain forever; the thread retains this connector and will
             * run shutdown() itself if the request eventually returns.
             */
            android.util.Log.e("XConnectorEpoll",
                    "X connector did not stop after shutdown; abandoning its blocked thread");
            return;
        }
        epollThread = null;
    }

    @Override
    public void run() {
        while (running && doEpollIndefinitely(epollFd, serverFd, !multithreadedClients));
        shutdown();
    }

    @Keep
    private void handleNewConnection(int fd) {
        final Client client = new Client(this, new ClientSocket(fd));
        client.connected = true;
        synchronized (clientsLock) {
            connectedClients.put(fd, client);
        }
        if (multithreadedClients) {
            client.shutdownFd = createEventFd();
            client.pollThread = new Thread(() -> {
                try {
                    connectionHandler.handleNewConnection(client);
                    client.connectionInitialized = true;
                    while (client.connected
                            && waitForSocketRead(client.clientSocket.fd, client.shutdownFd));
                } finally {
                    /*
                     * A peer close is delivered as POLLHUP, not necessarily
                     * as readable EOF. The old poll loop simply returned and
                     * left this Client registered forever; worse, the native
                     * helper treated HUP as success and spun one Java/JNI round
                     * trip per iteration at a full CPU core after a session
                     * ended. The client thread owns cleanup when it is the one
                     * that observes the disconnect. Connector.stop() sets
                     * connected=false first, so its concurrent teardown still
                     * remains the sole owner in that path.
                     */
                    finishConnection(client);
                }
            });
            client.pollThread.start();
        }
        else {
            try {
                connectionHandler.handleNewConnection(client);
                client.connectionInitialized = true;
            } catch (RuntimeException failure) {
                finishConnection(client);
                throw failure;
            }
        }
    }

    @Keep
    private void handleExistingConnection(int fd) {
        Client client;
        synchronized (clientsLock) {
            client = connectedClients.get(fd);
        }
        if (client == null) return;

        XInputStream inputStream = client.getInputStream();
        try {
            if (inputStream != null) {
                if (inputStream.readMoreData(canReceiveAncillaryMessages) > 0) {
                    int activePosition = 0;
                    while (running && requestHandler.handleRequest(client)) activePosition = inputStream.getActivePosition();
                    inputStream.setActivePosition(activePosition);
                }
                else killConnection(client);
            }
            else requestHandler.handleRequest(client);
        }
        catch (IOException e) {
            killConnection(client);
        }
    }

    public Client getClient(int fd) {
        synchronized (clientsLock) {
            return connectedClients.get(fd);
        }
    }

    public void killConnection(Client client) {
        if (client == null) return;
        client.connected = false;
        if (multithreadedClients) {
            if (Thread.currentThread() != client.pollThread) {
                client.requestShutdown();

                try {
                    client.pollThread.join(1000);
                    if (client.pollThread.isAlive()) {
                        client.pollThread.interrupt();
                        client.requestShutdown();
                        client.pollThread.join(250);
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
                if (client.pollThread.isAlive()) {
                    android.util.Log.e("XConnectorEpoll",
                            "X client thread did not stop; deferring owned cleanup");
                    return;
                } else {
                    client.pollThread = null;
                }
            }
        }
        finishConnection(client);
    }

    private void finishConnection(Client client) {
        if (!client.cleanupStarted.compareAndSet(false, true)) return;
        client.connected = false;
        try {
            if (client.connectionInitialized) {
                connectionHandler.handleConnectionShutdown(client);
            }
        } finally {
            if (multithreadedClients) {
                if (client.shutdownFd >= 0) closeFd(client.shutdownFd);
                client.shutdownFd = -1;
            } else {
                removeFdFromEpoll(epollFd, client.clientSocket.fd);
            }
            closeFd(client.clientSocket.fd);
            synchronized (clientsLock) {
                if (connectedClients.get(client.clientSocket.fd) == client) {
                    connectedClients.remove(client.clientSocket.fd);
                }
            }
        }
    }

    private void shutdown() {
        final List<Client> clients = new ArrayList<>();
        synchronized (clientsLock) {
            for (int i = 0; i < connectedClients.size(); i++) {
                clients.add(connectedClients.valueAt(i));
            }
        }
        for (Client client : clients) killConnection(client);

        removeFdFromEpoll(epollFd, serverFd);
        removeFdFromEpoll(epollFd, shutdownFd);
        closeFd(serverFd);
        closeFd(shutdownFd);
        closeFd(epollFd);
    }

    public int getInitialInputBufferCapacity() {
        return initialInputBufferCapacity;
    }

    public void setInitialInputBufferCapacity(int initialInputBufferCapacity) {
        this.initialInputBufferCapacity = initialInputBufferCapacity;
    }

    public int getInitialOutputBufferCapacity() {
        return initialOutputBufferCapacity;
    }

    public void setInitialOutputBufferCapacity(int initialOutputBufferCapacity) {
        this.initialOutputBufferCapacity = initialOutputBufferCapacity;
    }

    public boolean isMultithreadedClients() {
        return multithreadedClients;
    }

    public void setMultithreadedClients(boolean multithreadedClients) {
        this.multithreadedClients = multithreadedClients;
    }

    public boolean isCanReceiveAncillaryMessages() {
        return canReceiveAncillaryMessages;
    }

    public void setCanReceiveAncillaryMessages(boolean canReceiveAncillaryMessages) {
        this.canReceiveAncillaryMessages = canReceiveAncillaryMessages;
    }

    private void requestShutdown() {
        try {
            ByteBuffer data = ByteBuffer.allocateDirect(8);
            data.asLongBuffer().put(1);
            (new ClientSocket(shutdownFd)).write(data);
        }
        catch (IOException e) {}
    }

    public static native void closeFd(int fd);

    private native int createEpollFd();

    private native int createEventFd();

    private native boolean doEpollIndefinitely(int epollFd, int serverFd, boolean addClientToEpoll);

    private native boolean addFdToEpoll(int epollFd, int fd);

    private native void removeFdFromEpoll(int epollFd, int fd);

    private native boolean waitForSocketRead(int clientFd, int shutdownFd);

    private native int createAFUnixSocket(String path);
}
