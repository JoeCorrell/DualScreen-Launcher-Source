#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <algorithm>
#include <mutex>

#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>
#include <poll.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <linux/input.h>

#define EXPORT __attribute__((visibility("default"))) extern "C"

std::unordered_map<int, std::string> controller_map;
static std::mutex controller_mutex;
static bool initialized = false;
static const char *hook_dir = nullptr;
static bool vibration_enabled = true;
// Opt-in compatibility for games that gate XInput behind a DirectInput VID/PID check.
static bool xbox360_identity = false;
volatile sig_atomic_t stop_flag = 0;

static int (*my_open)(const char *, int, ...) = nullptr;
static int (*my_openat)(int, const char *, int, ...) = nullptr;
static int (*my_stat)(const char *, struct stat *) = nullptr;
static int (*my_fstat)(int fd, struct stat *buf) = nullptr;
static int (*my_scandir)(const char *, struct dirent***, int(*)(const struct dirent *), int(*)(const struct dirent**, const struct dirent**));
static int (*my_inotify_add_watch)(int, const char *, uint32_t);
static int (*my_close)(int);
static ssize_t (*my_write)(int, const void *, size_t) = nullptr;

namespace Logger {
	int log_enabled;

	void init() {
		log_enabled = getenv("FAKE_EVDEV_LOG") && atoi(getenv("FAKE_EVDEV_LOG"));
	}

	void log(const char *message, ...) {
		if (!log_enabled)
			return;

		va_list args;
		va_start(args, message);
		vfprintf(stderr, message, args);
		va_end(args);

		std::cerr.flush();
	}
}

void handle_sigint(int sig)  { 
    stop_flag = 1;
} 

void setup_signal_handler() {
    if (!initialized) {
        signal(SIGINT, handle_sigint);
        initialized = true;
    }
}

static std::unordered_map<int, struct ff_effect> ff_effects;
static int next_ff_id = 0;
static std::mutex ff_mutex;

void send_vibration(int strong, int weak, uint16_t duration_ms, uint16_t slot) {
  if (!vibration_enabled)
    return;

  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0)
    return;

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  const char *name = "wemu_vibration";
  memcpy(addr.sun_path + 1, name, strlen(name));
  socklen_t addrlen = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(name);

  if (connect(sock, (struct sockaddr *)&addr, addrlen) < 0) {
    syscall(SYS_close, sock);
    return;
  }

  uint16_t data[4];
  data[0] = (uint16_t)strong;
  data[1] = (uint16_t)weak;
  data[2] = duration_ms;
  data[3] = slot;
  send(sock, data, sizeof(data), 0);
  syscall(SYS_close, sock);
}

__attribute__((constructor))
static void library_init() {
	if (!hook_dir)
		hook_dir = getenv("FAKE_EVDEV_DIR") ? getenv("FAKE_EVDEV_DIR") : "/data/data/com.termux/files/home/fake-input";

	vibration_enabled = getenv("FAKE_EVDEV_VIBRATION") && atoi(getenv("FAKE_EVDEV_VIBRATION"));
	xbox360_identity = getenv("FAKE_EVDEV_XBOX360") && !strcmp(getenv("FAKE_EVDEV_XBOX360"), "1");
	Logger::init();
	if (xbox360_identity) Logger::log("Controller compatibility: Xbox 360 identity 045e:028e enabled\n");
}
  
__attribute__((visibility("hidden"))) 
char *from_real_to_fake_path(const char *pathname) {
	const char *event = strrchr(pathname, '/') + 1;
	char *fake_path;
	asprintf(&fake_path, "%s/%s", hook_dir, event);
	return fake_path;
}

__attribute__((visibility("hidden")))
const char *get_event(const char *pathname) {
	const char *event = strrchr(pathname, '/') + 1;
	return event;
}

__attribute__((visibility("hidden")))
int get_event_number(const char *event) {
    int event_number = atoi(event + strlen(event) - 1);
    return event_number;
}

static bool get_controller_event(int fd, std::string *event) {
    std::lock_guard<std::mutex> lock(controller_mutex);
    auto it = controller_map.find(fd);
    if (it == controller_map.end()) return false;
    if (event) *event = it->second;
    return true;
}

static void add_controller_fd(int fd, const char *event) {
    if (fd < 0 || !event) return;
    std::lock_guard<std::mutex> lock(controller_mutex);
    controller_map[fd] = event;
}

static bool remove_controller_fd(int fd, std::string *event) {
    std::lock_guard<std::mutex> lock(controller_mutex);
    auto it = controller_map.find(fd);
    if (it == controller_map.end()) return false;
    if (event) *event = it->second;
    controller_map.erase(it);
    return true;
}

static int copy_ioctl_result(void *argp, int op,
                             const void *source, size_t source_size) {
    const size_t requested = _IOC_SIZE(op);
    if (!argp || requested == 0) {
        errno = EFAULT;
        return -1;
    }
    memcpy(argp, source, std::min(requested, source_size));
    return 0;
}

static void set_mask_bit(std::vector<unsigned char> *mask, unsigned int bit) {
    if (!mask || bit / 8 >= mask->size()) return;
    (*mask)[bit / 8] |= static_cast<unsigned char>(1u << (bit % 8));
}

EXPORT int open(const char *pathname, int flags, ...) {
    va_list va;
    mode_t mode;
    int fd;
    bool hasMode;
    bool isFromInput;
    char *redirectedPath = nullptr;

    va_start(va, flags);

    hasMode = flags & O_CREAT;
    isFromInput = false;
    
    if (hasMode) {
        mode = va_arg(va, mode_t);
    }
    
    va_end(va);

	if (!my_open)
    	*(void **)&my_open = dlsym(RTLD_NEXT, "open");

	if (pathname) {
		if (strstr(pathname, "/dev/input/event")) {
		    redirectedPath = from_real_to_fake_path(pathname);
		    pathname = redirectedPath;
		    isFromInput = true;
		}
		else if (!strcmp(pathname, "/dev/input")) {
			pathname = hook_dir;
		}
	}
	    
	if (hasMode)
	    fd = my_open(pathname, flags, mode);
	else
	    fd = my_open(pathname, flags);

	if (isFromInput && fd >= 0) {
		Logger::log("Adding controller, fd %d event %s\n", fd, get_event(pathname));
		add_controller_fd(fd, get_event(pathname));
    }
	free(redirectedPath);
	    
	return fd;
}

EXPORT int openat(int dirfd, const char *pathname, int flags, ...) {
    va_list va;
    mode_t mode;
    int fd;
    bool hasMode;
    bool isFromInput;
    char *redirectedPath = nullptr;
    
    va_start(va, flags);

    isFromInput = false;
    hasMode = flags & O_CREAT;
    
    if (hasMode) {
        mode = va_arg(va, mode_t);
    }
    
    va_end(va);

    if (!my_openat)
    	*(void **)&my_openat = dlsym(RTLD_NEXT, "openat");
    
    if (pathname) {
        if (strstr(pathname, "/dev/input/event")) {
            redirectedPath = from_real_to_fake_path(pathname);
            pathname = redirectedPath;
            isFromInput = true;
        }
        else if (!strcmp(pathname, "/dev/input")) {                                    
            pathname = hook_dir;             
        }
    }
    
    if (hasMode)
        fd = my_openat(dirfd, pathname, flags, mode);
    else
        fd = my_openat(dirfd, pathname, flags);

    if (isFromInput && fd >= 0) {
        Logger::log("Adding controller, fd %d event %s\n", fd, get_event(pathname));
        add_controller_fd(fd, get_event(pathname));
    }

    free(redirectedPath);

    return fd;
}

EXPORT int stat(const char *pathname, struct stat *statbuf) {
	if (!my_stat)
		*(void **)&my_stat = dlsym(RTLD_NEXT, "stat");

     const char *event = nullptr;
     int event_number = -1;
     char *redirectedPath = nullptr;

	if (pathname) {
		if (strstr(pathname, "/dev/input/event")) {
		    redirectedPath = from_real_to_fake_path(pathname);
		    pathname = redirectedPath;
		    event = get_event(pathname);
		    event_number = get_event_number(event);
		}
		else if (!strcmp(pathname, "/dev/input")) {                                    
		    pathname = hook_dir;             
		}
	}

	int ret = my_stat(pathname, statbuf);
    
    if (ret == 0 && event && event_number >= 0) {
		statbuf->st_rdev = makedev(1, event_number);
	}
	free(redirectedPath);

	return ret;
}

EXPORT int fstat(int fd, struct stat *buf) {
	if (!my_fstat)
    	*(void **)&my_fstat = dlsym(RTLD_NEXT, "fstat");

    int ret = my_fstat(fd, buf);

    std::string event;
    if (ret == 0 && get_controller_event(fd, &event)) {
		buf->st_rdev = makedev(1, get_event_number(event.c_str()));
    }

    return ret;
  }

EXPORT int scandir(const char *dirp, struct dirent ***namelist, int(*filter)(const struct dirent *), int(*compar)(const struct dirent **, const struct dirent **)) {
	if (!my_scandir)
		*(void **)&my_scandir = dlsym(RTLD_NEXT, "scandir");
	
	if (dirp) {
	    if (!strcmp(dirp, "/dev/input")) {
	        dirp = hook_dir;
	    }
    }

	return my_scandir(dirp, namelist, filter, compar);
}

EXPORT int inotify_add_watch(int fd, const char *pathname, uint32_t mask) {
	if (!my_inotify_add_watch)
		*(void **)&my_inotify_add_watch = dlsym(RTLD_NEXT, "inotify_add_watch");

    char *redirectedPath = nullptr;
    if (pathname) {
        if (strstr(pathname, "/dev/input/event")) {
            redirectedPath = from_real_to_fake_path(pathname);
            pathname = redirectedPath;
        }
        else if (!strcmp(pathname, "/dev/input")) {
            pathname = hook_dir;
    	}
    }

    int result = my_inotify_add_watch(fd, pathname, mask);
    free(redirectedPath);
    return result;
}

EXPORT int ioctl(int fd, int op, ...) {
	va_list va;
	void *argp;
	
	va_start(va, op);
	argp = va_arg(va, void *);
	va_end(va);

	std::string event_storage;
	if (!get_controller_event(fd, &event_storage)) {
		return syscall(SYS_ioctl, fd, op, argp);
	}

	int type = (op >> 8 & 0xFF);
	int number = (op >> 0 & 0xFF);
	const char *event = event_storage.c_str();
	int event_number = get_event_number(event);

    if (type == 0x45 && number == 0x1) {
        Logger::log("Hooking ioctl EVIOCGVERSION for event %s\n", event);
        int version = 65536;
        return copy_ioctl_result(argp, op, &version, sizeof(version));
    }
    else if (type == 0x45 && number == 0x2) {
        Logger::log("Hooking ioctl EVIOCGID for event %s\n", event);
        struct input_id id;
        memset(&id, 0, sizeof(id));
        id.bustype = 0x03;
        id.vendor = xbox360_identity ? 0x045e : 0x1234 + event_number;
        id.product = xbox360_identity ? 0x028e : 0x5678 + event_number;
        id.version = 0x0110;
        return copy_ioctl_result(argp, op, &id, sizeof(id));
    }
    else if (type == 0x45 && number == 0x6) {
    	Logger::log("Hooking ioctl EVIOCGNAME for event %s\n", event);
		const size_t requested = _IOC_SIZE(op);
		if (!argp || requested == 0) { errno = EFAULT; return -1; }
		snprintf(static_cast<char *>(argp), requested,
		         xbox360_identity ? "Controller (XBOX 360 For Windows)" : "Generic HID Gamepad %d", event_number);
    	return 0;
    }
    else if (type == 0x45 && number == 0x9) {
        Logger::log("Hooking ioctl EVIOCGPROP for event %s\n", event);
		const size_t requested = _IOC_SIZE(op);
		if (!argp || requested == 0) { errno = EFAULT; return -1; }
		memset(argp, 0, requested);
        return 0;
    }
    else if (type == 0x45 && number == 0x18) {
    	Logger::log("Hooking ioctl EVIOCGKEY(len) for event %s\n", event);
		const size_t requested = _IOC_SIZE(op);
		if (!argp || requested == 0) { errno = EFAULT; return -1; }
		memset(argp, 0, requested);
        return 0;
    }
    else if (type == 0x45 && number == 0x20) {
    	Logger::log("Hooking ioctl EVIOCGBIT(0, len) for event %s\n", event);
		const size_t requested = _IOC_SIZE(op);
		if (!argp || requested == 0) { errno = EFAULT; return -1; }
		std::vector<unsigned char> bitmask(requested, 0);
		set_mask_bit(&bitmask, EV_SYN);
		set_mask_bit(&bitmask, EV_KEY);
		set_mask_bit(&bitmask, EV_ABS);
		memcpy(argp, bitmask.data(), bitmask.size());
    	return 0;	
    }
    else if (type == 0x45 && number == 0x21) {
        Logger::log("Hooking ioctl EVIOCGBIT(EV_KEY, len) for event %s\n", event);
		const size_t requested = _IOC_SIZE(op);
		if (!argp || requested == 0) { errno = EFAULT; return -1; }
		std::vector<unsigned char> bitmask(requested, 0);
        for (int i = 0x130; i <= 0x13e; i++) {
            if (i == 0x130)
                set_mask_bit(&bitmask, BTN_A);
            else if (i == 0x131)
                set_mask_bit(&bitmask, BTN_B);
            else if (i == 0x132)
                continue;
            else if (i == 0x133)
                set_mask_bit(&bitmask, BTN_X);
            else if (i == 0x134)
                set_mask_bit(&bitmask, BTN_Y);
            else if (i == 0x135)
                continue;
            else
				set_mask_bit(&bitmask, static_cast<unsigned int>(i));
        }
		memcpy(argp, bitmask.data(), bitmask.size());
        return 0;
    }
    else if (type == 0x45 && number == 0x22) {
    	Logger::log("Hooking ioctl EVIOCGBIT(EV_REL, len) for event %s\n", event);
		const size_t requested = _IOC_SIZE(op);
		if (!argp || requested == 0) { errno = EFAULT; return -1; }
		memset(argp, 0, requested);
    	return 0;
    }
    else if (type == 0x45 && number == 0x23) {
    	Logger::log("Hooking ioctl EVIOCGBIT(EV_ABS, len) for event %s\n", event);
		const size_t requested = _IOC_SIZE(op);
		if (!argp || requested == 0) { errno = EFAULT; return -1; }
		std::vector<unsigned char> bitmask(requested, 0);
		set_mask_bit(&bitmask, ABS_X);
		set_mask_bit(&bitmask, ABS_Y);
		set_mask_bit(&bitmask, ABS_RX);
		set_mask_bit(&bitmask, ABS_RY);
		set_mask_bit(&bitmask, ABS_GAS);
		set_mask_bit(&bitmask, ABS_BRAKE);
		set_mask_bit(&bitmask, ABS_HAT0X);
		set_mask_bit(&bitmask, ABS_HAT0Y);
		memcpy(argp, bitmask.data(), bitmask.size());
    	return 0;
    }
    else if (type == 0x45 && number == 0x35) {
		Logger::log("Hooking ioctl EVIOCGBIT(EV_FF, len) for event %s\n", event);
		const size_t requested = _IOC_SIZE(op);
		if (!argp || requested == 0) { errno = EFAULT; return -1; }
		std::vector<unsigned char> bitmask(requested, 0);
		set_mask_bit(&bitmask, FF_RUMBLE);
		set_mask_bit(&bitmask, FF_PERIODIC);
		memcpy(argp, bitmask.data(), bitmask.size());
        return 0;
    }
    else if (type == 0x45 && number == 0x80) {
		if (!argp || _IOC_SIZE(op) < sizeof(struct ff_effect)) {
			errno = EFAULT;
			return -1;
		}
        struct ff_effect *effect = (struct ff_effect *)argp;
		std::lock_guard<std::mutex> lock(ff_mutex);
        if (effect->id == -1) {
            effect->id = next_ff_id++;
        }
        ff_effects[effect->id] = *effect;

        uint16_t duration = effect->replay.length;
        if (effect->type == FF_RUMBLE) {
            send_vibration(effect->u.rumble.strong_magnitude, effect->u.rumble.weak_magnitude, duration, (uint16_t)event_number);
        } else if (effect->type == FF_PERIODIC) {
            send_vibration(effect->u.periodic.magnitude, effect->u.periodic.magnitude, duration, (uint16_t)event_number);
        }
        return 0;
    }
    else if (type == 0x45 && number == 0x81) {
        int id = (intptr_t)argp;
		std::lock_guard<std::mutex> lock(ff_mutex);
        ff_effects.erase(id);
        return 0;
    }
    else if (type == 0x45 && number == 0x84) {
        int max_effects = 16;
		return copy_ioctl_result(argp, op, &max_effects, sizeof(max_effects));
    }
    else if (type == 0x45 && number >= 0x40 && number <= 0x51) {
    	Logger::log("Hooking ioctl EVIOCGABS(ABS) for event %s\n", event);
    	struct input_absinfo abs_info;
    	memset(&abs_info, 0, sizeof(abs_info));
    	if (number >= 0x40 && number <= 0x44) {
    		abs_info.value = 0;
    		abs_info.minimum = -32768;
    		abs_info.maximum = 32767;
    	}
    	else if (number >= 0x49 && number <= 0x4A) {
    		abs_info.value = 0;
    		abs_info.minimum = 0;
    		abs_info.maximum = 255;
    	}
    	else if (number >= 0x50 && number <= 0x51) {
    		abs_info.value = 0;
    		abs_info.minimum = -1;
    		abs_info.maximum = 1;
    	}
		return copy_ioctl_result(argp, op, &abs_info, sizeof(abs_info));
    }
    else if (type == 0x45 && number == 0x90) {
    	Logger::log("Hooking ioctl EVIOCGRAB for event %s\n", event);
    	/* Always pretend this succeeds */
    	return 0;
    }
    else if (type == 0x6A && number == 0x13) {
    	Logger::log("Hooking ioctl JSIOCGNAME(len) for event %s\n", event);
		const size_t requested = _IOC_SIZE(op);
		if (!argp || requested == 0) { errno = EFAULT; return -1; }
		snprintf(static_cast<char *>(argp), requested,
		         xbox360_identity ? "Controller (XBOX 360 For Windows)" : "Generic HID Gamepad %d", event_number);
    	return 0;
    }
    else {
    	Logger::log("Unhandled evdev ioctl, type %d number %d\n", type, number);
    	return syscall(SYS_ioctl, fd, op, argp);
    }
}

EXPORT int close(int fd) {
	if (!my_close)
		*(void **)&my_close = dlsym(RTLD_NEXT, "close");

	std::string event;
	if (remove_controller_fd(fd, &event)) {
	    Logger::log("Removing controller, fd %d event %s\n", fd, event.c_str());
	}

	return my_close(fd);
}

EXPORT ssize_t read(int fd, void *buf, size_t count) {
    if (get_controller_event(fd, nullptr)) {
        ssize_t bytes_read = 0;
        int flags = fcntl(fd, F_GETFL);
        bool isNonBlock = flags & O_NONBLOCK;
        bytes_read = syscall(SYS_read, fd, buf, count);
        while(bytes_read == 0) {
            struct stat statbuf;
            if (!my_fstat) *(void **)&my_fstat = dlsym(RTLD_NEXT, "fstat");
            if (my_fstat && my_fstat(fd, &statbuf) == 0 && statbuf.st_nlink == 0) {
                errno = ENODEV;
                return -1;
            }
            if (isNonBlock) {
				errno = EAGAIN;
				return -1;
            }
            setup_signal_handler();
            if (stop_flag) {
            	bytes_read = -1;
            	errno = EINTR;
            	return bytes_read;
            }
            /*
             * eventN is an append-only regular file, not a pipe. poll() marks a
             * regular file readable forever at EOF, so the former loop spun at
             * 100% CPU and still did not wait for Android's next append. A 2 ms
             * bounded wait keeps controller latency below one frame while
             * releasing the CPU between samples.
             */
            usleep(2000);
            bytes_read = syscall(SYS_read, fd, buf, count);
        }
        
    	return bytes_read;
    }
    return syscall(SYS_read, fd, buf, count);
}

static void check_ff_event(const struct input_event *ev, uint16_t slot) {
  if (ev->type == EV_FF) {
    int id = ev->code;
    int value = ev->value;
    if (value > 0) {
	  std::lock_guard<std::mutex> lock(ff_mutex);
      auto it = ff_effects.find(id);
      if (it != ff_effects.end()) {
        uint16_t duration = it->second.replay.length;
        if (it->second.type == FF_RUMBLE) {
          send_vibration(it->second.u.rumble.strong_magnitude,
                         it->second.u.rumble.weak_magnitude, duration, slot);
        } else if (it->second.type == FF_PERIODIC) {
          send_vibration(it->second.u.periodic.magnitude,
                         it->second.u.periodic.magnitude, duration, slot);
        }
      }
    } else {
      send_vibration(0, 0, 0, slot);
    }
  }
}

EXPORT ssize_t write(int fd, const void *buf, size_t count) {
  if (!my_write)
    *(void **)&my_write = dlsym(RTLD_NEXT, "write");

	std::string event;
  if (get_controller_event(fd, &event)) {
    if (count == sizeof(struct input_event)) {
      const struct input_event *ev = (const struct input_event *)buf;
		uint16_t slot = (uint16_t)get_event_number(event.c_str());
      check_ff_event(ev, slot);
      // EV_FF events are FF control commands sent by Wine to the fake device.
      // Writing them to the fake evdev file causes Wine to read them back as
      // input events, corrupting controller state and blocking input. Consume
      // them here and return success without writing to the file.
      if (ev->type == EV_FF)
        return (ssize_t)count;
    }
  }
  return my_write(fd, buf, count);
}

EXPORT ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
	if (!iov || iovcnt <= 0) {
		errno = EINVAL;
		return -1;
	}
	std::string event;
  if (get_controller_event(fd, &event)) {
    uint16_t slot = (uint16_t)get_event_number(event.c_str());
    // Separate FF control events from regular input events.
    // FF events must not be written to the fake evdev file (see write() above).
	std::vector<struct iovec> filtered;
	filtered.reserve(static_cast<size_t>(iovcnt));
    int filtered_count = 0;
	ssize_t consumed = 0;
    for (int i = 0; i < iovcnt; i++) {
      if (iov[i].iov_len == sizeof(struct input_event)) {
        const struct input_event *ev = (const struct input_event *)iov[i].iov_base;
        check_ff_event(ev, slot);
		if (ev->type == EV_FF) {
		  consumed += static_cast<ssize_t>(iov[i].iov_len);
          continue;
		}
      }
	  filtered.push_back(iov[i]);
	  filtered_count++;
    }
    if (filtered_count == 0)
	  return consumed;
	ssize_t written = syscall(SYS_writev, fd, filtered.data(), filtered_count);
	return written < 0 ? written : written + consumed;
  }
  return syscall(SYS_writev, fd, iov, iovcnt);
}
