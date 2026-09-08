import com.wemu.engine.alsaserver.ALSAClient;
import java.nio.*;
import java.lang.reflect.*;

/** Digital silence only; tests the real JNI/AAudio queue and hardware cursor. */
public final class PcmPlaybackDeviceCheck {
    public static void main(String[] args) throws Exception {
        ALSAClient client = new ALSAClient(false);
        client.setChannelCount(2); client.setDataType(ALSAClient.DataType.FLOATLE);
        client.setSampleRate(48000); client.setBufferSize(1920);
        try {
            client.prepare();
            Field ptr = ALSAClient.class.getDeclaredField("streamPtr"); ptr.setAccessible(true);
            long stream = ptr.getLong(client);
            if (stream == 0) throw new AssertionError("Could not open AAudio");
            Method pause = ALSAClient.class.getDeclaredMethod("pause", long.class);
            Method start = ALSAClient.class.getDeclaredMethod("start", long.class);
            Method pending = ALSAClient.class.getDeclaredMethod("getPendingFrames", long.class);
            Method xruns = ALSAClient.class.getDeclaredMethod("getXRunCount", long.class);
            for (Method m : new Method[]{pause, start, pending, xruns}) m.setAccessible(true);
            pause.invoke(client, stream);
            ByteBuffer silence = ByteBuffer.allocateDirect(1920 * 8 + 16).order(ByteOrder.LITTLE_ENDIAN);
            silence.position(16);
            client.writeDataToStream(silence);
            long queued = (Long)pending.invoke(client, stream);
            if (queued != 1920 || client.pointer() != 0)
                throw new AssertionError("Queued audio counted as played: pending=" + queued + " pointer=" + client.pointer());
            System.out.println("Paused queue=1920, consumed cursor=0 (old bridge incorrectly returned 1920).");
            start.invoke(client, stream);
            long last = 0;
            for (int i = 1; i < 300; ++i) {
                silence.position(16); client.writeDataToStream(silence);
                long position = Integer.toUnsignedLong(client.pointer());
                if (position < last || position > (i + 1L) * 1920)
                    throw new AssertionError("Invalid hardware cursor: " + position);
                last = position;
            }
            int underruns = (Integer)xruns.invoke(client, stream);
            long end = android.os.SystemClock.elapsedRealtime() + 1000;
            while (client.pointer() != 576000 && android.os.SystemClock.elapsedRealtime() < end)
                Thread.sleep(5);
            if (client.pointer() != 576000) throw new AssertionError("Did not consume all 576000 frames");
            if (underruns != 0) throw new AssertionError("AAudio xruns=" + underruns);
            client.prepare();
            if (client.pointer() != 0) throw new AssertionError("Prepare did not reset the cursor");
            System.out.println("PASS: 576000 silent frames, monotonic consumed cursor, drained tail, zero xruns, prepare reset.");
        } finally { client.release(); }
    }
}
