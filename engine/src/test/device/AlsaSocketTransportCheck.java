import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import com.wemu.engine.alsaserver.ALSAClient;
import com.wemu.engine.alsaserver.ALSARequestHandler;
import com.wemu.engine.xconnector.*;
import java.io.*;
import java.nio.*;
import java.nio.file.Files;

/** Standalone app_process check: private socket, synthetic PCM, no audio device or game. */
public final class AlsaSocketTransportCheck {
    static final class Sink extends ALSAClient {
        volatile int samples;
        volatile Throwable failure;
        @Override public void prepare() { samples = 0; }
        @Override public void release() { }
        @Override public int pointer() { return samples / 2; }
        @Override public void writeDataToStream(ByteBuffer packet) {
            try {
                // Deliberately stall the consumer while the producer queues
                // multiple packets. Every sample must retain its identity.
                Thread.sleep(2);
                packet.order(ByteOrder.LITTLE_ENDIAN);
                while (packet.remaining() >= 4) {
                    int actual = packet.getInt();
                    if (actual != samples) throw new AssertionError("sample " + samples + " became " + actual);
                    ++samples;
                }
            } catch (Throwable error) { failure = error; }
        }
    }
    static byte[] message(int code, byte[] data) {
        return ByteBuffer.allocate(5 + data.length).order(ByteOrder.LITTLE_ENDIAN)
                .put((byte)code).putInt(data.length).put(data).array();
    }
    public static void main(String[] args) throws Exception {
        File parent = new File(args[0]).getCanonicalFile();
        if (!parent.getPath().endsWith("/com.dualscreen.launcher/cache")) throw new IllegalArgumentException();
        File folder = Files.createTempDirectory(parent.toPath(), "alsa-socket-check-").toFile();
        Sink sink = new Sink();
        UnixSocketConfig config = UnixSocketConfig.createSocket(folder.getPath(), "/pcm");
        XConnectorEpoll server = new XConnectorEpoll(config, new ConnectionHandler() {
            @Override public void handleNewConnection(Client client) { client.createIOStreams(); client.setTag(sink); }
            @Override public void handleConnectionShutdown(Client client) { }
        }, new ALSARequestHandler());
        server.setMultithreadedClients(true);
        server.start();
        try (LocalSocket client = new LocalSocket()) {
            client.connect(new LocalSocketAddress(config.path, LocalSocketAddress.Namespace.FILESYSTEM));
            client.setSoTimeout(5000);
            OutputStream output = client.getOutputStream();
            byte[] prepare = message(4, ByteBuffer.allocate(10).order(ByteOrder.LITTLE_ENDIAN)
                    .put((byte)2).put((byte)3).putInt(48000).putInt(1920).array());
            for (byte part : prepare) { output.write(part); Thread.sleep(1); }
            int count = 0;
            for (int packet = 0; packet < 200; ++packet) {
                ByteBuffer pcm = ByteBuffer.allocate(1920 * 8).order(ByteOrder.LITTLE_ENDIAN);
                while (pcm.hasRemaining()) pcm.putInt(count++);
                byte[] request = message(5, pcm.array());
                // Both split headers/payloads and packets larger than the
                // connector's initial 4096-byte input buffer.
                output.write(request, 0, 3);
                output.write(request, 3, request.length - 3);
            }
            output.write(message(7, new byte[0]));
            byte[] reply = new byte[4];
            new DataInputStream(client.getInputStream()).readFully(reply);
            int pointer = ByteBuffer.wrap(reply).order(ByteOrder.LITTLE_ENDIAN).getInt();
            if (sink.failure != null) throw new AssertionError(sink.failure);
            if (pointer != count / 2 || sink.samples != count) throw new AssertionError("pointer/PCM mismatch: " + pointer);
            if (client.getAncillaryFileDescriptors() != null) throw new AssertionError("Unexpected shared-memory handshake");
            System.out.println("PASS: 200 delayed-consumer packets, " + count + " exact samples; fragmented PREPARE/WRITE and POINTER framing; no audio/game access.");
        } finally {
            server.stop();
            new File(config.path).delete();
            folder.delete();
        }
    }
}
