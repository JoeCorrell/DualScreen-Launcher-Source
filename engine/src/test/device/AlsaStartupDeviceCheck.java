import com.wemu.engine.alsaserver.ALSAClient;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Silent, isolated reproduction of Wine filling its ring then waiting for playback. */
public final class AlsaStartupDeviceCheck {
    public static void main(String[] args) throws Exception {
        ALSAClient client = new ALSAClient(false);
        client.setChannelCount(2);
        client.setDataType(ALSAClient.DataType.FLOATLE);
        client.setSampleRate(48000);
        client.setBufferSize(1920);
        try {
            client.prepare();
            ByteBuffer silence = ByteBuffer.allocateDirect(1920 * 8).order(ByteOrder.LITTLE_ENDIAN);
            client.writeDataToStream(silence);
            // Send exactly one guest ring. Repeated writes would conceal the
            // deadlock by filling Android's larger startup threshold anyway.
            Thread.sleep(350);
            long consumed = Integer.toUnsignedLong(client.pointer());
            System.out.println("guestQueued=1920 guestConsumed=" + consumed);
            if ("stalled".equals(args[0])) {
                if (consumed != 0) throw new AssertionError("Baseline did not reproduce starvation");
                System.out.println("REPRODUCED: guest ring full, hardware cursor remains zero");
            } else {
                if (consumed != 1920) throw new AssertionError("First guest ring did not fully play");
                for (int i = 0; i < 4; ++i) {
                    silence.clear();
                    client.writeDataToStream(silence);
                    Thread.sleep(75);
                }
                consumed = Integer.toUnsignedLong(client.pointer());
                if (consumed != 9600) throw new AssertionError("Cursor dropped or added guest frames: " + consumed);
                System.out.println("PASS: startup advances and all 9600 guest frames play; padding excluded");
            }
        } finally {
            client.release();
        }
    }
}
