package com.wemu.engine.xconnector;

import androidx.annotation.Keep;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayDeque;

public class ClientSocket {
    /**
     * Local X/ALSA traffic should drain immediately.  A guest which stops
     * reading must never pin an Android UI or engine thread in a native
     * write; Android declares the app unresponsive after five seconds.
     */
    private static final int WRITE_TIMEOUT_MS = 250;
    public final int fd;
    private final ArrayDeque<Integer> ancillaryFds = new ArrayDeque<>();
    private volatile boolean outputPoisoned;

    static {
        System.loadLibrary("wemu");
    }

    public ClientSocket(int fd) {
        this.fd = fd;
    }

    public boolean hasAncillaryFds() {
        return !ancillaryFds.isEmpty();
    }

    public int getAncillaryFd() {
        return hasAncillaryFds() ? ancillaryFds.poll() : -1;
    }

    @Keep
    public void addAncillaryFd(int ancillaryFd) {
        ancillaryFds.add(ancillaryFd);
    }

    public int read(ByteBuffer data) throws IOException {
        int position = data.position();
        int bytesRead = read(fd, data, position, data.remaining());
        if (bytesRead > 0) {
            data.position(position + bytesRead);
            return bytesRead;
        }
        else if (bytesRead == 0) {
            return -1;
        }
        else throw new IOException("Failed to read data.");
    }

    public void write(ByteBuffer data) throws IOException {
        if (outputPoisoned) throw new IOException("Socket output is closed.");
        int position = data.position();
        int length = data.remaining();
        int bytesWritten = write(fd, data, position, length, WRITE_TIMEOUT_MS);
        if (bytesWritten == length) {
            data.position(position + bytesWritten);
            return;
        }
        outputPoisoned = true;
        throw new IOException("Failed to write complete socket message (result="
                + bytesWritten + ", expected=" + length + ").");
    }

    public int recvAncillaryMsg(ByteBuffer data) throws IOException {
        int position = data.position();
        int bytesRead = recvAncillaryMsg(fd, data, position, data.remaining());
        if (bytesRead > 0) {
            data.position(position + bytesRead);
            return bytesRead;
        }
        else if (bytesRead == 0) {
            return -1;
        }
        else throw new IOException("Failed to receive ancillary messages.");
    }

    public void sendAncillaryMsg(ByteBuffer data, int ancillaryFd) throws IOException {
        if (outputPoisoned) throw new IOException("Socket output is closed.");
        int position = data.position();
        int length = data.remaining();
        int bytesSent = sendAncillaryMsg(
                fd, data, position, length, ancillaryFd, WRITE_TIMEOUT_MS);
        if (bytesSent == length) {
            data.position(position + bytesSent);
            return;
        }
        outputPoisoned = true;
        throw new IOException("Failed to send complete ancillary message (result="
                + bytesSent + ", expected=" + length + ").");
    }

    private native int read(int fd, ByteBuffer data, int offset, int length);

    private native int write(int fd, ByteBuffer data, int offset, int length, int timeoutMs);

    private native int recvAncillaryMsg(int clientFd, ByteBuffer data, int offset, int length);

    private native int sendAncillaryMsg(int clientFd, ByteBuffer data, int offset, int length,
                                        int ancillaryFd, int timeoutMs);
}
