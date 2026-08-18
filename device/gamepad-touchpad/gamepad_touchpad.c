#include "gamepad_touchpad.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/input.h>

#include <SDL_stdinc.h>
#include <SDL_version.h>

#include "logging.h"

#define SYSFS_ROOT "/sys"
#define DEV_INPUT_ROOT "/dev/input"

#define MAX_SIBLINGS 8
#define NODE_NAME_MAX 32

#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define NBITS(x) (((x) - 1) / BITS_PER_LONG + 1)
#define TEST_BIT(bit, array) (((array)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1ul)

struct gamepad_touchpad_t {
    int fd;
};

static bool hid_parent_for_devnum(const char *sysfs_root, dev_t rdev, char *out, size_t out_len);

static int sibling_event_nodes(const char *hid_path, char names[][NODE_NAME_MAX], int max_names);

static bool is_multitouch_pointer(int fd);

gamepad_touchpad_t *gamepad_touchpad_grab(SDL_GameController *controller) {
    if (controller == NULL) {
        return NULL;
    }
#if SDL_VERSION_ATLEAST(2, 24, 0)
    const char *dev_path = SDL_GameControllerPath(controller);
#else
    const char *dev_path = NULL;
#endif
    if (dev_path == NULL) {
        commons_log_debug("GamepadTouchpad", "No device path for controller");
        return NULL;
    }

    struct stat st;
    if (stat(dev_path, &st) != 0) {
        commons_log_debug("GamepadTouchpad", "Can't stat %s: %s", dev_path, strerror(errno));
        return NULL;
    }

    char hid_path[PATH_MAX];
    if (!hid_parent_for_devnum(SYSFS_ROOT, st.st_rdev, hid_path, sizeof(hid_path))) {
        commons_log_debug("GamepadTouchpad", "Can't resolve HID parent of %s", dev_path);
        return NULL;
    }

    char names[MAX_SIBLINGS][NODE_NAME_MAX];
    int num_names = sibling_event_nodes(hid_path, names, MAX_SIBLINGS);
    for (int i = 0; i < num_names; i++) {
        char node_path[PATH_MAX];
        if (snprintf(node_path, sizeof(node_path), "%s/%s", DEV_INPUT_ROOT, names[i]) >= (int) sizeof(node_path)) {
            continue;
        }
        int fd = open(node_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        if (!is_multitouch_pointer(fd)) {
            close(fd);
            continue;
        }
        if (ioctl(fd, EVIOCGRAB, 1) < 0) {
            commons_log_warn("GamepadTouchpad", "Can't grab %s: %s", node_path, strerror(errno));
            close(fd);
            continue;
        }
        gamepad_touchpad_t *touchpad = SDL_malloc(sizeof(gamepad_touchpad_t));
        if (touchpad == NULL) {
            ioctl(fd, EVIOCGRAB, 0);
            close(fd);
            return NULL;
        }
        touchpad->fd = fd;
        commons_log_info("GamepadTouchpad", "Grabbed %s", node_path);
        return touchpad;
    }
    return NULL;
}

void gamepad_touchpad_release(gamepad_touchpad_t *touchpad) {
    if (touchpad == NULL) {
        return;
    }
    ioctl(touchpad->fd, EVIOCGRAB, 0);
    close(touchpad->fd);
    SDL_free(touchpad);
}

/**
 * Find the device that owns the input children of whatever node SDL bound.
 *
 * SDL reports an "event" or "js" node with the Linux driver, or a "hidraw" one
 * with HIDAPI; both live below the same HID device. /sys/class is not
 * always visible to a sandboxed app, so resolve through /sys/dev/char instead
 * and walk up until we reach the ancestor holding the input/ directory.
 *
 * @param sysfs_root Mount point of sysfs, parameterised for tests.
 */
static bool hid_parent_for_devnum(const char *sysfs_root, dev_t rdev, char *out, size_t out_len) {
    char link[PATH_MAX];
    if (snprintf(link, sizeof(link), "%s/dev/char/%u:%u", sysfs_root, major(rdev), minor(rdev)) >=
        (int) sizeof(link)) {
        return false;
    }
    char resolved[PATH_MAX];
    if (realpath(link, resolved) == NULL) {
        return false;
    }

    // Never walk above <sysfs_root>/devices.
    char devices_root[PATH_MAX];
    if (snprintf(link, sizeof(link), "%s/devices", sysfs_root) >= (int) sizeof(link) ||
        realpath(link, devices_root) == NULL) {
        return false;
    }
    size_t devices_root_len = strlen(devices_root);
    if (strncmp(resolved, devices_root, devices_root_len) != 0) {
        return false;
    }

    while (strlen(resolved) > devices_root_len) {
        char probe[PATH_MAX];
        if (snprintf(probe, sizeof(probe), "%s/input", resolved) < (int) sizeof(probe)) {
            struct stat probe_st;
            if (stat(probe, &probe_st) == 0 && S_ISDIR(probe_st.st_mode)) {
                if (strlen(resolved) >= out_len) {
                    return false;
                }
                strcpy(out, resolved);
                return true;
            }
        }
        char *slash = strrchr(resolved, '/');
        if (slash == NULL) {
            break;
        }
        *slash = '\0';
    }
    return false;
}

/**
 * Collect the event node names below a device's input children, e.g. the
 * "event14" in <hid>/input/input15/event14. Order follows readdir and carries
 * no meaning; callers must identify the touchpad by capability.
 *
 * @return Number of names written to @p names.
 */
static int sibling_event_nodes(const char *hid_path, char names[][NODE_NAME_MAX], int max_names) {
    char inputs_path[PATH_MAX];
    if (snprintf(inputs_path, sizeof(inputs_path), "%s/input", hid_path) >= (int) sizeof(inputs_path)) {
        return 0;
    }
    DIR *inputs = opendir(inputs_path);
    if (inputs == NULL) {
        return 0;
    }
    int num_names = 0;
    struct dirent *input_ent;
    while (num_names < max_names && (input_ent = readdir(inputs)) != NULL) {
        if (strncmp(input_ent->d_name, "input", 5) != 0) {
            continue;
        }
        char input_path[PATH_MAX];
        if (snprintf(input_path, sizeof(input_path), "%s/%s", inputs_path, input_ent->d_name) >=
            (int) sizeof(input_path)) {
            continue;
        }
        DIR *input_dir = opendir(input_path);
        if (input_dir == NULL) {
            continue;
        }
        struct dirent *event_ent;
        while (num_names < max_names && (event_ent = readdir(input_dir)) != NULL) {
            if (strncmp(event_ent->d_name, "event", 5) != 0 || strlen(event_ent->d_name) >= NODE_NAME_MAX) {
                continue;
            }
            strcpy(names[num_names++], event_ent->d_name);
        }
        closedir(input_dir);
    }
    closedir(inputs);
    return num_names;
}

/**
 * The kernel reports a controller touchpad as a pointer device
 * (INPUT_PROP_POINTER, not INPUT_PROP_DIRECT) with multitouch axes. The gamepad
 * node has no ABS_MT axes so it is never matched, and a real touchscreen is
 * excluded by INPUT_PROP_DIRECT.
 */
static bool is_multitouch_pointer(int fd) {
    unsigned long props[NBITS(INPUT_PROP_MAX)];
    memset(props, 0, sizeof(props));
    if (ioctl(fd, EVIOCGPROP(sizeof(props)), props) < 0) {
        return false;
    }
    if (!TEST_BIT(INPUT_PROP_POINTER, props) || TEST_BIT(INPUT_PROP_DIRECT, props)) {
        return false;
    }

    unsigned long absbits[NBITS(ABS_MAX)];
    memset(absbits, 0, sizeof(absbits));
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0) {
        return false;
    }
    return TEST_BIT(ABS_MT_POSITION_X, absbits) && TEST_BIT(ABS_MT_POSITION_Y, absbits);
}
