#include <jni.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/eventfd.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <android/log.h>

#define printf(...) __android_log_print(ANDROID_LOG_DEBUG, "System.out", __VA_ARGS__);
#define MAX_EVENTS 10
#define MAX_FDS 32

static int64_t monotonic_millis(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    return (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int wait_until_writable(int fd, int64_t deadline_ms) {
    for (;;) {
        int64_t now_ms = monotonic_millis();
        if (now_ms < 0 || now_ms >= deadline_ms) return -ETIMEDOUT;

        int timeout_ms = (int) (deadline_ms - now_ms);
        struct pollfd pfd = {.fd = fd, .events = POLLOUT, .revents = 0};
        int result = poll(&pfd, 1, timeout_ms);
        if (result > 0) {
            if (pfd.revents & POLLOUT) return 0;
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -EPIPE;
            continue;
        }
        if (result == 0) return -ETIMEDOUT;
        if (errno != EINTR) return -errno;
    }
}

/*
 * Completes one ordered stream write without ever inheriting the descriptor's
 * blocking mode.  Returning after a partial X11 message would desynchronize
 * the protocol, so every error poisons the connection before it is reported.
 */
static int write_all_before(int fd, const char *data, int length, int64_t deadline_ms) {
    int total = 0;
    while (total < length) {
        int64_t now_ms = monotonic_millis();
        if (now_ms < 0 || now_ms >= deadline_ms) {
            shutdown(fd, SHUT_RDWR);
            return -ETIMEDOUT;
        }

        ssize_t written = send(fd, data + total, (size_t) (length - total),
                               MSG_DONTWAIT | MSG_NOSIGNAL);
        if (written < 0 && errno == ENOTSOCK) {
            /* ClientSocket is also used for the nonblocking shutdown eventfd. */
            written = write(fd, data + total, (size_t) (length - total));
        }
        if (written > 0) {
            total += (int) written;
            continue;
        }
        if (written == 0) {
            shutdown(fd, SHUT_RDWR);
            return -EPIPE;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            int wait_result = wait_until_writable(fd, deadline_ms);
            if (wait_result == 0) continue;
            shutdown(fd, SHUT_RDWR);
            return wait_result;
        }
        int error = -errno;
        shutdown(fd, SHUT_RDWR);
        return error;
    }
    return total;
}

JNIEXPORT jint JNICALL
Java_com_wemu_engine_xconnector_XConnectorEpoll_createAFUnixSocket(JNIEnv *env, jobject obj,
                                                                jstring path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sun_family = AF_LOCAL;

    const char *pathPtr = (*env)->GetStringUTFChars(env, path, 0);

    int addrLength = sizeof(sa_family_t) + strlen(pathPtr);
    strncpy(serverAddr.sun_path, pathPtr, sizeof(serverAddr.sun_path) - 1);

    (*env)->ReleaseStringUTFChars(env, path, pathPtr);

    unlink(serverAddr.sun_path);
    if (bind(fd, (struct sockaddr*) &serverAddr, addrLength) < 0) goto error;
    if (listen(fd, MAX_EVENTS) < 0) goto error;

    return fd;
    error:
    close(fd);
    return -1;
}

JNIEXPORT jint JNICALL
Java_com_wemu_engine_xconnector_XConnectorEpoll_createEpollFd(JNIEnv *env, jobject obj) {
    return epoll_create(MAX_EVENTS);
}

JNIEXPORT void JNICALL
Java_com_wemu_engine_xconnector_XConnectorEpoll_closeFd(JNIEnv *env, jobject obj, jint fd) {
    close(fd);
}

JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_xconnector_XConnectorEpoll_doEpollIndefinitely(JNIEnv *env, jobject obj,
                                                                 jint epollFd, jint serverFd,
                                                                 jboolean addClientToEpoll) {
    jclass cls = (*env)->GetObjectClass(env, obj);
    jmethodID handleNewConnection = (*env)->GetMethodID(env, cls, "handleNewConnection", "(I)V");
    jmethodID handleExistingConnection = (*env)->GetMethodID(env, cls, "handleExistingConnection", "(I)V");

    /*
     * The event buffer is per-call, and it has to be.
     *
     * This was one array at file scope, which is fine for one caller and there
     * are three: `XServerComponent`, `SysVSharedMemoryComponent` and
     * `ALSAServerComponent` each construct an `XConnectorEpoll` with a thread
     * of its own, and all three threads call this same function. Two of them
     * waking together means one overwrites the entries the other is partway
     * through iterating.
     *
     * Mostly that is self-correcting and invisible: the descriptors are
     * process-wide unique, so an entry from the wrong connector misses in
     * `connectedClients` and the level-triggered epoll simply re-reports the
     * event on the next pass. What is not invisible is a torn read of the
     * packed 12-byte entry producing a descriptor that *is* in this
     * connector's map but has nothing to read. There is no `O_NONBLOCK`
     * anywhere in this connector and `ClientSocket_read` is a bare `read()`,
     * so that lands the X server's only event thread in an indefinite block —
     * a session that freezes with no error and nothing in the log.
     *
     * `pollEpollEvents` below already does this correctly. Ten entries is 120
     * bytes of stack on an ordinary Java thread.
     */
    struct epoll_event events[MAX_EVENTS];

    int numFds = epoll_wait(epollFd, events, MAX_EVENTS, -1);
    for (int i = 0; i < numFds; i++) {
        if (events[i].data.fd == serverFd) {
            int clientFd = accept(serverFd, NULL, NULL);
            if (clientFd >= 0) {
                if (addClientToEpoll) {
                    struct epoll_event event;
                    event.data.fd = clientFd;
                    event.events = EPOLLIN | EPOLLRDHUP;

                    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &event) >= 0) {
                        (*env)->CallVoidMethod(env, obj, handleNewConnection, clientFd);
                    }
                }
                else (*env)->CallVoidMethod(env, obj, handleNewConnection, clientFd);
            }
        }
        else if (events[i].events & (EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
            /*
             * A bounded output failure shuts the stream down to preserve X11
             * framing.  HUP/ERR are level-triggered even without EPOLLIN;
             * forwarding them lets Java observe EOF and remove the client
             * instead of spinning this epoll thread forever.
             */
            (*env)->CallVoidMethod(env, obj, handleExistingConnection, events[i].data.fd);
        }
    }

    return numFds >= 0;
}

JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_xconnector_XConnectorEpoll_addFdToEpoll(JNIEnv *env, jobject obj,
                                                          jint epollFd,
                                                          jint fd) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event) < 0) return JNI_FALSE;
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_wemu_engine_xconnector_XConnectorEpoll_removeFdFromEpoll(JNIEnv *env, jobject obj,
                                                               jint epollFd, jint fd) {
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, NULL);
}

JNIEXPORT jint JNICALL
Java_com_wemu_engine_xconnector_ClientSocket_read(JNIEnv *env, jobject obj, jint fd, jobject data,
                                               jint offset, jint length) {
    char *dataAddr = (*env)->GetDirectBufferAddress(env, data);
    return read(fd, dataAddr + offset, length);
}

JNIEXPORT jint JNICALL
Java_com_wemu_engine_xconnector_ClientSocket_write(JNIEnv *env, jobject obj, jint fd, jobject data,
                                                jint offset, jint length, jint timeoutMs) {
    char *dataAddr = (*env)->GetDirectBufferAddress(env, data);
    if (dataAddr == NULL || offset < 0 || length < 0 || timeoutMs <= 0) return -EINVAL;
    int64_t now_ms = monotonic_millis();
    if (now_ms < 0) return -EIO;
    return write_all_before(fd, dataAddr + offset, length, now_ms + timeoutMs);
}

JNIEXPORT jint JNICALL
Java_com_wemu_engine_xconnector_XConnectorEpoll_createEventFd(JNIEnv *env, jobject obj) {
    return eventfd(0, EFD_NONBLOCK);
}

JNIEXPORT jint JNICALL
Java_com_wemu_engine_xconnector_ClientSocket_recvAncillaryMsg(JNIEnv *env, jobject obj, jint clientFd, jobject data,
                                                           jint offset, jint length) {
    char *dataAddr = (*env)->GetDirectBufferAddress(env, data);

    struct iovec iovmsg = {.iov_base = dataAddr + offset, .iov_len = length};
    struct {
        struct cmsghdr align;
        int fds[MAX_FDS];
    } ctrlmsg;

    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iovmsg,
        .msg_iovlen = 1,
        .msg_control = &ctrlmsg,
        .msg_controllen = sizeof(struct cmsghdr) + MAX_FDS * sizeof(int)
    };

    int size = recvmsg(clientFd, &msg, 0);

    if (size >= 0) {
        struct cmsghdr *cmsg;
        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
                int numFds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                if (numFds > 0) {
                    jclass cls = (*env)->GetObjectClass(env, obj);
                    jmethodID addAncillaryFd = (*env)->GetMethodID(env, cls, "addAncillaryFd", "(I)V");
                    for (int i = 0; i < numFds; i++) {
                        int ancillaryFd = ((int*)CMSG_DATA(cmsg))[i];
                        (*env)->CallVoidMethod(env, obj, addAncillaryFd, ancillaryFd);
                    }
                }
            }
        }
    }
    return size;
}

JNIEXPORT jint JNICALL
Java_com_wemu_engine_xconnector_ClientSocket_sendAncillaryMsg(JNIEnv *env, jobject obj, jint clientFd,
                                                           jobject data, jint offset, jint length,
                                                           jint ancillaryFd, jint timeoutMs) {
    char *dataAddr = (*env)->GetDirectBufferAddress(env, data);
    if (dataAddr == NULL || offset < 0 || length <= 0 || ancillaryFd < 0 || timeoutMs <= 0) {
        return -EINVAL;
    }

    struct iovec iovmsg = {.iov_base = dataAddr + offset, .iov_len = (size_t) length};
    struct {
        struct cmsghdr align;
        int fds[1];
    } ctrlmsg;

    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iovmsg,
        .msg_iovlen = 1,
        .msg_flags = 0,
        .msg_control = &ctrlmsg,
        .msg_controllen = sizeof(struct cmsghdr) + sizeof(int)
    };

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = msg.msg_controllen;
    ((int*)CMSG_DATA(cmsg))[0] = ancillaryFd;

    int64_t now_ms = monotonic_millis();
    if (now_ms < 0) return -EIO;
    int64_t deadline_ms = now_ms + timeoutMs;

    for (;;) {
        now_ms = monotonic_millis();
        if (now_ms < 0 || now_ms >= deadline_ms) {
            shutdown(clientFd, SHUT_RDWR);
            return -ETIMEDOUT;
        }

        ssize_t sent = sendmsg(clientFd, &msg, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) {
            if (sent == length) return (jint) sent;
            int remainder = write_all_before(clientFd, dataAddr + offset + sent,
                                             length - (int) sent, deadline_ms);
            return remainder >= 0 ? (jint) sent + remainder : remainder;
        }
        if (sent == 0) {
            shutdown(clientFd, SHUT_RDWR);
            return -EPIPE;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            int wait_result = wait_until_writable(clientFd, deadline_ms);
            if (wait_result == 0) continue;
            shutdown(clientFd, SHUT_RDWR);
            return wait_result;
        }
        int error = -errno;
        shutdown(clientFd, SHUT_RDWR);
        return error;
    }
}

JNIEXPORT jboolean JNICALL
Java_com_wemu_engine_xconnector_XConnectorEpoll_waitForSocketRead(JNIEnv *env, jobject obj, jint clientFd, jint shutdownFd) {
    struct pollfd pfds[2];
    pfds[0].fd = clientFd;
    pfds[0].events = POLLIN;

    pfds[1].fd = shutdownFd;
    pfds[1].events = POLLIN;

    int res = poll(pfds, 2, -1);
    if (res < 0 || (pfds[1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) {
        return JNI_FALSE;
    }

    /*
     * A closed ALSA/X peer commonly reports POLLHUP without POLLIN. Returning
     * true for that state made the Java client thread call poll() again
     * immediately forever, consuming one full core after the Wine session had
     * already gone. Error/hang-up/invalid descriptors are terminal; cleanup is
     * performed by the client thread's finally block.
     */
    if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) return JNI_FALSE;

    if (pfds[0].revents & POLLIN) {
        jclass cls = (*env)->GetObjectClass(env, obj);
        jmethodID handleExistingConnection = (*env)->GetMethodID(env, cls, "handleExistingConnection", "(I)V");
        (*env)->CallVoidMethod(env, obj, handleExistingConnection, clientFd);
    }
    return (pfds[0].revents & POLLIN) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jintArray JNICALL
Java_com_wemu_engine_xconnector_XConnectorEpoll_pollEpollEvents(JNIEnv *env, jobject obj,
                                                             jint epollFd, jint maxEvents) {
    struct epoll_event events[maxEvents];
    int numFds = epoll_wait(epollFd, events, maxEvents, -1); // Wait indefinitely

    if (numFds < 0) return NULL;

    jintArray result = (*env)->NewIntArray(env, numFds);
    if (result == NULL) return NULL;

    jint *r = (*env)->GetIntArrayElements(env, result, 0);

    for (int i = 0; i < numFds; i++) {
        r[i] = events[i].data.fd; // Store file descriptor
    }

    (*env)->ReleaseIntArrayElements(env, result, r, 0);
    return result;
}
