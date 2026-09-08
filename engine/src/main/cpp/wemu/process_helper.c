#define _GNU_SOURCE  
#include <jni.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>

JNIEXPORT jint JNICALL
Java_com_wemu_engine_core_ProcessHelper_nativeSetProcessAffinity(
        JNIEnv *env, jclass clazz, jint pid, jint mask) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    for (int i = 0; i < 32; i++) {
        if ((uint32_t)mask & (UINT32_C(1) << i))
            CPU_SET(i, &cpuset);
    }

    int result = sched_setaffinity((pid_t) pid, sizeof(cpuset), &cpuset);

    return result == 0 ? 0 : errno;
}

JNIEXPORT jint JNICALL
Java_com_wemu_engine_core_ProcessHelper_nativeGetProcessAffinity(
        JNIEnv *env, jclass clazz, jint pid) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    if (sched_getaffinity((pid_t) pid, sizeof(cpuset), &cpuset) != 0)
        return -1; 

    uint32_t mask = 0;
    for (int i = 0; i < 32; i++) {
        if (CPU_ISSET(i, &cpuset))
            mask |= (UINT32_C(1) << i);
    }
    return (jint)mask;
}
