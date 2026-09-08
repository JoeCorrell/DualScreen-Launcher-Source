import com.wemu.engine.alsaserver.ALSAClient;
import java.nio.*;
import java.lang.reflect.*;

/** Silence only. Emulates a guest that will not exceed its 1920-frame ring. */
public final class AlsaGuestRingStartupCheck {
    public static void main(String[] args) throws Exception {
        boolean expectStall=args.length>0 && args[0].equals("expect-stall");
        ALSAClient client=new ALSAClient(false);
        client.setChannelCount(2);client.setDataType(ALSAClient.DataType.FLOATLE);
        client.setSampleRate(48000);client.setBufferSize(1920);
        try {
            client.prepare();
            Field field=ALSAClient.class.getDeclaredField("streamPtr");field.setAccessible(true);
            long stream=field.getLong(client);if(stream==0)throw new AssertionError("Audio stream unavailable");
            Method pending=ALSAClient.class.getDeclaredMethod("getPendingFrames",long.class);pending.setAccessible(true);
            ByteBuffer silence=ByteBuffer.allocateDirect(1920*8).order(ByteOrder.LITTLE_ENDIAN);
            long submitted=0,deadline=android.os.SystemClock.elapsedRealtime()+1200;
            while(submitted<15360 && android.os.SystemClock.elapsedRealtime()<deadline){
                long available=1920-(submitted-Integer.toUnsignedLong(client.pointer()));
                if(available>=1920){silence.clear();client.writeDataToStream(silence);submitted+=1920;}
                else Thread.sleep(5);
            }
            long cursor=Integer.toUnsignedLong(client.pointer());
            long queued=(Long)pending.invoke(client,stream);
            if(expectStall){
                if(submitted!=1920 || cursor!=0 || queued!=1920)
                    throw new AssertionError("Expected startup stall was not reproduced: sent="+submitted+" cursor="+cursor+" queued="+queued);
                System.out.println("REPRODUCED: guest submitted 1920, cursor=0, Android queued=1920; no further guest frames allowed.");
            } else {
                if(submitted!=15360 || cursor!=submitted)
                    throw new AssertionError("Guest startup stalled: sent="+submitted+" cursor="+cursor);
                Thread.sleep(120);
                queued=(Long)pending.invoke(client,stream);
                if(queued>=submitted)throw new AssertionError("Android playback never advanced");
                client.prepare();if(client.pointer()!=0)throw new AssertionError("Prepare failed to reset cursor");
                System.out.println("PASS: bounded guest ring submitted 15360 silent frames; Android playback advanced; cursor reset on prepare.");
            }
        } finally { client.release(); }
    }
}
