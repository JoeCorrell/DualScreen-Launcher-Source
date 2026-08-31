package com.wemu.engine.xconnector;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.atomic.AtomicBoolean;

public class Client {
    public final ClientSocket clientSocket;
    private final XConnectorEpoll connector;
    private XInputStream inputStream;
    private XOutputStream outputStream;
    private Object tag;
    protected Thread pollThread;
    protected int shutdownFd = -1;
    /** Read by the connector thread and the per-client poll thread. */
    protected volatile boolean connected;
    /** Makes handler shutdown, fd close, and map removal exactly-once. */
    protected final AtomicBoolean cleanupStarted = new AtomicBoolean(false);
    protected volatile boolean connectionInitialized;

    public Client(XConnectorEpoll connector, ClientSocket clientSocket) {
        this.connector = connector;
        this.clientSocket = clientSocket;
    }

    public void createIOStreams() {
        if (inputStream != null || outputStream != null) return;
        inputStream = new XInputStream(clientSocket, connector.getInitialInputBufferCapacity());
        outputStream = new XOutputStream(clientSocket, connector.getInitialOutputBufferCapacity());
        inputStream.setByteOrder(ByteOrder.LITTLE_ENDIAN);
        outputStream.setByteOrder(ByteOrder.LITTLE_ENDIAN);
    }

    public XInputStream getInputStream() {
        return inputStream;
    }

    public XOutputStream getOutputStream() {
        return outputStream;
    }

    public Object getTag() {
        return tag;
    }

    public void setTag(Object tag) {
        this.tag = tag;
    }

    protected void requestShutdown() {
        if (shutdownFd < 0) return;
        try {
            ByteBuffer data = ByteBuffer.allocateDirect(8);
            data.asLongBuffer().put(1);
            (new ClientSocket(shutdownFd)).write(data);
        }
        catch (IOException e) {}
    }
}
