#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <jni.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <android/log.h>

#define WAIT_COMPLETION_TIMEOUT 100 * 1000000L
#define LOG_TAG "WemuAudio"

enum Format {U8, S16LE, S16BE, FLOATLE, FLOATBE};

static aaudio_format_t toAAudioFormat(int format) {
    switch (format) {
        case FLOATLE:
        case FLOATBE:
            return AAUDIO_FORMAT_PCM_FLOAT;
        case U8:
            // Java expands unsigned 8-bit PCM to signed 16-bit before write.
            return AAUDIO_FORMAT_PCM_I16;
        case S16LE:
        case S16BE:
        default:
            return AAUDIO_FORMAT_PCM_I16;
    }
}

static AAudioStream *aaudioCreate(int32_t format, int8_t channelCount, int32_t sampleRate,
                                 int32_t bufferSize, bool burstAligned) {
    aaudio_result_t result;
    AAudioStreamBuilder *builder;
    AAudioStream *stream;

    result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "AAudio builder failed: %s", AAudio_convertResultToText(result));
        return NULL;
    }

    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    // Usage was added in API 28 while WEMU still supports API 26. Resolve it
    // dynamically so newer devices route the stream as game audio without
    // making libwemu unloadable on 26/27.
    typedef void (*set_usage_fn)(AAudioStreamBuilder*, int32_t);
    set_usage_fn setUsage = (set_usage_fn)dlsym(RTLD_DEFAULT,
                                                "AAudioStreamBuilder_setUsage");
    if (setUsage) setUsage(builder, 14 /* AAUDIO_USAGE_GAME */);
    AAudioStreamBuilder_setFormat(builder, toAAudioFormat(format));
    AAudioStreamBuilder_setChannelCount(builder, channelCount);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);

    result = AAudioStreamBuilder_openStream(builder, &stream);
    if (result != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "AAudio open failed format=%d channels=%d rate=%d frames=%d: %s",
                            format, channelCount, sampleRate, bufferSize,
                            AAudio_convertResultToText(result));
        AAudioStreamBuilder_delete(builder);
        return NULL;
    }

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "AAudio opened format=%d channels=%d rate=%d frames=%d burst=%d",
                        AAudioStream_getFormat(stream),
                        AAudioStream_getChannelCount(stream),
                        AAudioStream_getSampleRate(stream), bufferSize,
                        AAudioStream_getFramesPerBurst(stream));

    int32_t requestedFrames = bufferSize;
    if (burstAligned) {
        const int32_t burst = AAudioStream_getFramesPerBurst(stream);
        if (burst > 0) {
            const int32_t twoBursts = burst * 2;
            requestedFrames = bufferSize < twoBursts ? bufferSize : twoBursts;
            if (requestedFrames < burst) requestedFrames = burst;
        }
    }
    int32_t actualFrames = AAudioStream_setBufferSizeInFrames(stream, requestedFrames);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "AAudio buffer=%d capacity=%d performanceMode=%d",
                        actualFrames, AAudioStream_getBufferCapacityInFrames(stream),
                        AAudioStream_getPerformanceMode(stream));

    /*
     * AudioTrack-backed AAudio rounds this device's 1920-frame guest ring up
     * to a 1922-frame start threshold. Wine fills its ring, then waits for the
     * hardware cursor; Android waits for two more frames. Neither advances.
     * Prime only the rounding gap, before guest PCM, so the first complete
     * guest ring can start playback. These silent host frames deliberately do
     * not enter ALSA's accepted-frame counter. The consumed-frame clock still
     * subtracts the entire native queue, so no guest frame is credited early.
     */
    if (actualFrames > bufferSize) {
        int32_t paddingFrames = actualFrames - bufferSize;
        int32_t channels = AAudioStream_getChannelCount(stream);
        size_t sampleBytes = AAudioStream_getFormat(stream) == AAUDIO_FORMAT_PCM_FLOAT ? 4 : 2;
        void *silence = paddingFrames <= 262144 && channels > 0 && channels <= 8
                ? calloc((size_t)paddingFrames * (size_t)channels, sampleBytes) : NULL;
        int32_t primed = silence ? AAudioStream_write(stream, silence, paddingFrames, 0)
                                : AAUDIO_ERROR_NO_MEMORY;
        free(silence);
        if (primed != paddingFrames) {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                                "AAudio startup padding failed: requested=%d accepted=%d",
                                paddingFrames, primed);
            AAudioStream_close(stream);
            AAudioStreamBuilder_delete(builder);
            return NULL;
        }
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                            "AAudio primed %d silent host frames for guest ring=%d output=%d",
                            primed, bufferSize, actualFrames);
    }

    result = AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK) {
        AAudioStream_close(stream);
        return NULL;
    }

    return stream;
}

static int aaudioWrite(AAudioStream *aaudioStream, void *buffer, int numFrames) {
    aaudio_result_t framesWritten = AAudioStream_write(aaudioStream, buffer, numFrames, WAIT_COMPLETION_TIMEOUT);
    return framesWritten;
}

static void aaudioStart(AAudioStream *aaudioStream) {
    aaudio_stream_state_t next;
    if (AAudioStream_requestStart(aaudioStream) == AAUDIO_OK)
        AAudioStream_waitForStateChange(aaudioStream, AAUDIO_STREAM_STATE_STARTING, &next, WAIT_COMPLETION_TIMEOUT);
}

static void aaudioStop(AAudioStream *aaudioStream) {
    aaudio_stream_state_t next;
    if (AAudioStream_requestStop(aaudioStream) == AAUDIO_OK)
        AAudioStream_waitForStateChange(aaudioStream, AAUDIO_STREAM_STATE_STOPPING, &next, WAIT_COMPLETION_TIMEOUT);
}

static void aaudioPause(AAudioStream *aaudioStream) {
    aaudio_stream_state_t next;
    if (AAudioStream_requestPause(aaudioStream) == AAUDIO_OK)
        AAudioStream_waitForStateChange(aaudioStream, AAUDIO_STREAM_STATE_PAUSING, &next, WAIT_COMPLETION_TIMEOUT);
}

static void aaudioFlush(AAudioStream *aaudioStream) {
    aaudio_stream_state_t next;
    if (AAudioStream_requestFlush(aaudioStream) == AAUDIO_OK)
        AAudioStream_waitForStateChange(aaudioStream, AAUDIO_STREAM_STATE_FLUSHING, &next, WAIT_COMPLETION_TIMEOUT);
}

JNIEXPORT jlong JNICALL
Java_com_wemu_engine_alsaserver_ALSAClient_create(JNIEnv *env, jobject obj, jint format,
                                               jbyte channelCount, jint sampleRate, jint bufferSize,
                                               jboolean burstAligned) {
    return (jlong)aaudioCreate(format, channelCount, sampleRate, bufferSize,
                              burstAligned == JNI_TRUE);
}

JNIEXPORT jint JNICALL
Java_com_wemu_engine_alsaserver_ALSAClient_write(JNIEnv *env, jobject obj, jlong streamPtr, jobject buffer,
                                              jint byteOffset, jint numFrames) {
    AAudioStream *aaudioStream = (AAudioStream*)streamPtr;
    void *address = buffer ? (*env)->GetDirectBufferAddress(env, buffer) : NULL;
    jlong capacity = buffer ? (*env)->GetDirectBufferCapacity(env, buffer) : -1;
    if (aaudioStream && address && numFrames > 0 && byteOffset >= 0) {
        int bytesPerSample = AAudioStream_getFormat(aaudioStream) == AAUDIO_FORMAT_PCM_FLOAT ? 4 : 2;
        int64_t bytes = (int64_t)numFrames * AAudioStream_getChannelCount(aaudioStream) * bytesPerSample;
        if (bytes <= 0 || (int64_t)byteOffset + bytes > capacity) return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
        return aaudioWrite(aaudioStream, (uint8_t*)address + byteOffset, numFrames);
    }
    else return -1;
}

JNIEXPORT void JNICALL
Java_com_wemu_engine_alsaserver_ALSAClient_start(JNIEnv *env, jobject obj, jlong streamPtr) {
    AAudioStream *aaudioStream = (AAudioStream*)streamPtr;
    if (aaudioStream) aaudioStart(aaudioStream);
}

JNIEXPORT void JNICALL
Java_com_wemu_engine_alsaserver_ALSAClient_stop(JNIEnv *env, jobject obj, jlong streamPtr) {
    AAudioStream *aaudioStream = (AAudioStream*)streamPtr;
    if (aaudioStream) aaudioStop(aaudioStream);
}

JNIEXPORT void JNICALL
Java_com_wemu_engine_alsaserver_ALSAClient_pause(JNIEnv *env, jobject obj, jlong streamPtr) {
    AAudioStream *aaudioStream = (AAudioStream*)streamPtr;
    if (aaudioStream) aaudioPause(aaudioStream);
}

JNIEXPORT void JNICALL
Java_com_wemu_engine_alsaserver_ALSAClient_flush(JNIEnv *env, jobject obj, jlong streamPtr) {
    AAudioStream *aaudioStream = (AAudioStream*)streamPtr;
    if (aaudioStream) aaudioFlush(aaudioStream);
}

JNIEXPORT void JNICALL
Java_com_wemu_engine_alsaserver_ALSAClient_close(JNIEnv *env, jobject obj, jlong streamPtr) {
    AAudioStream *aaudioStream = (AAudioStream*)streamPtr;
    if (aaudioStream) AAudioStream_close(aaudioStream);
}

JNIEXPORT jint JNICALL
Java_com_wemu_engine_alsaserver_ALSAClient_getXRunCount(JNIEnv *env, jobject obj, jlong streamPtr) {
    return streamPtr ? AAudioStream_getXRunCount((AAudioStream*)streamPtr) : -1;
}

JNIEXPORT jlong JNICALL
Java_com_wemu_engine_alsaserver_ALSAClient_getPendingFrames(JNIEnv *env, jobject obj, jlong streamPtr) {
    if (!streamPtr) return -1;
    AAudioStream* stream = (AAudioStream*)streamPtr;
    // Reading the endpoint last cannot overestimate the queue if it advances
    // between these calls. ALSAClient holds streamLock against write/close.
    int64_t written = AAudioStream_getFramesWritten(stream);
    int64_t read = AAudioStream_getFramesRead(stream);
    if (written < 0 || read < 0) return -1;
    return written > read ? written - read : 0;
}
