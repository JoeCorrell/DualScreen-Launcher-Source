#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <jni.h>
#include <stdbool.h>
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
    AAudioStream_setBufferSizeInFrames(stream, requestedFrames);

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
    AAudioStream_requestStart(aaudioStream);
    AAudioStream_waitForStateChange(aaudioStream, AAUDIO_STREAM_STATE_STARTING, NULL, WAIT_COMPLETION_TIMEOUT);
}

static void aaudioStop(AAudioStream *aaudioStream) {
    AAudioStream_requestStop(aaudioStream);
    AAudioStream_waitForStateChange(aaudioStream, AAUDIO_STREAM_STATE_STOPPING, NULL, WAIT_COMPLETION_TIMEOUT);
}

static void aaudioPause(AAudioStream *aaudioStream) {
    AAudioStream_requestPause(aaudioStream);
    AAudioStream_waitForStateChange(aaudioStream, AAUDIO_STREAM_STATE_PAUSING, NULL, WAIT_COMPLETION_TIMEOUT);
}

static void aaudioFlush(AAudioStream *aaudioStream) {
    AAudioStream_requestFlush(aaudioStream);
    AAudioStream_waitForStateChange(aaudioStream, AAUDIO_STREAM_STATE_FLUSHING, NULL, WAIT_COMPLETION_TIMEOUT);
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
                                              jint numFrames) {
    AAudioStream *aaudioStream = (AAudioStream*)streamPtr;
    void *address = buffer ? (*env)->GetDirectBufferAddress(env, buffer) : NULL;
    if (aaudioStream && address && numFrames > 0) {
        return aaudioWrite(aaudioStream, address, numFrames);
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
