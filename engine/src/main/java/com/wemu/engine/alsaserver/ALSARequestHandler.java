package com.wemu.engine.alsaserver;

import android.util.Log;

import com.wemu.engine.xconnector.Client;
import com.wemu.engine.xconnector.RequestHandler;
import com.wemu.engine.xconnector.XInputStream;
import com.wemu.engine.xconnector.XOutputStream;
import com.wemu.engine.xconnector.XStreamLock;

import java.io.IOException;

public class ALSARequestHandler implements RequestHandler {
    private static final int PREPARE_BYTES = 10;
    private static final int MAX_WRITE_BYTES = 8 * 1024 * 1024;
    private static final int MAX_CHANNELS = 8;
    private static final int MIN_SAMPLE_RATE = 8_000;
    private static final int MAX_SAMPLE_RATE = 384_000;
    private static final int MAX_BUFFER_FRAMES = 262_144;

    @Override
    public boolean handleRequest(Client client) throws IOException {
        ALSAClient alsaClient = (ALSAClient)client.getTag();
        XInputStream inputStream = client.getInputStream();
        XOutputStream outputStream = client.getOutputStream();

        if (inputStream.available() < 5) return false;
        byte requestCode = inputStream.readByte();
        int requestLength = inputStream.readInt();
        if (requestLength < 0 || requestLength > MAX_WRITE_BYTES) {
            throw new IOException("Invalid ALSA request length " + requestLength);
        }

        switch (requestCode) {
            case RequestCodes.CLOSE:
                requireLength(requestLength, 0, "CLOSE");
                alsaClient.release();
                break;
            case RequestCodes.START:
                requireLength(requestLength, 0, "START");
                alsaClient.start();
                break;
            case RequestCodes.STOP:
                requireLength(requestLength, 0, "STOP");
                alsaClient.stop();
                break;
            case RequestCodes.PAUSE:
                requireLength(requestLength, 0, "PAUSE");
                alsaClient.pause();
                break;
            case RequestCodes.PREPARE:
                requireLength(requestLength, PREPARE_BYTES, "PREPARE");
                if (inputStream.available() < requestLength) return false;

                int channelCount = inputStream.readUnsignedByte();
                int typeIndex = inputStream.readUnsignedByte();
                int sampleRate = inputStream.readInt();
                int bufferFrames = inputStream.readInt();
                if (channelCount < 1 || channelCount > MAX_CHANNELS) {
                    throw new IOException("Invalid ALSA channel count " + channelCount);
                }
                if (typeIndex < 0 || typeIndex >= ALSAClient.DataType.values().length) {
                    throw new IOException("Invalid ALSA sample type " + typeIndex);
                }
                if (sampleRate < MIN_SAMPLE_RATE || sampleRate > MAX_SAMPLE_RATE) {
                    throw new IOException("Invalid ALSA sample rate " + sampleRate);
                }
                if (bufferFrames < 1 || bufferFrames > MAX_BUFFER_FRAMES) {
                    throw new IOException("Invalid ALSA buffer frames " + bufferFrames);
                }
                ALSAClient.DataType dataType = ALSAClient.DataType.values()[typeIndex];
                long byteSize = (long)channelCount * dataType.byteCount * bufferFrames;
                if (byteSize <= 0 || byteSize > MAX_WRITE_BYTES) {
                    throw new IOException("Invalid ALSA buffer byte size " + byteSize);
                }

                alsaClient.setChannelCount(channelCount);
                alsaClient.setDataType(dataType);
                alsaClient.setSampleRate(sampleRate);
                alsaClient.setBufferSize(bufferFrames);
                Log.i("WemuAudio", "ALSA stream format=" + dataType +
                        " channels=" + channelCount + " rate=" + sampleRate +
                        " bufferFrames=" + bufferFrames);
                alsaClient.prepare();

                // WemuSession selects socket PCM in the guest plugin. PREPARE
                // has no reply in that protocol: sending a shared-memory FD or
                // its dummy byte would corrupt the next POINTER response.
                Log.i("WemuAudio", "PCM transport=socket (ordered, no shared slot)");
                break;
            case RequestCodes.WRITE:
                if (inputStream.available() < requestLength) return false;
                alsaClient.writeDataToStream(inputStream.readByteBuffer(requestLength));
                break;
            case RequestCodes.DRAIN:
                requireLength(requestLength, 0, "DRAIN");
                alsaClient.drain();
                break;
            case RequestCodes.POINTER:
                requireLength(requestLength, 0, "POINTER");
                try (XStreamLock lock = outputStream.lock()) {
                    outputStream.writeInt(alsaClient.pointer());
                }
                break;
            default:
                throw new IOException("Unknown ALSA request opcode " + (requestCode & 0xff));
        }
        return true;
    }

    private static void requireLength(int actual, int expected, String request)
            throws IOException {
        if (actual != expected) {
            throw new IOException(request + " has invalid length " + actual
                    + " (expected " + expected + ")");
        }
    }

}
