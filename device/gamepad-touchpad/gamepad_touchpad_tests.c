/*
 * Fixture tests for the sysfs traversal in gamepad_touchpad.c.
 *
 * The traversal is where the platform-specific assumptions live - which node
 * SDL reports, and how it relates to the touchpad's node - so it is tested
 * against synthetic sysfs trees rather than real hardware. Capability probing
 * and the grab itself need a real evdev device and are covered separately.
 *
 * The .c is included directly so the static helpers can be reached, following
 * the pattern used by evmouse_tests.c.
 */
#include "gamepad_touchpad.c"

#include <assert.h>
#include <stdarg.h>

#define DS4_HID "0003:054C:09CC.0021"

static char fixture_root[PATH_MAX];

static void fixture_mkdir(const char *fmt, ...);

static void fixture_symlink(const char *target, const char *fmt, ...);

/**
 * Mirrors a DualShock 4 on webOS: one HID device owning three input children,
 * reachable through /sys/dev/char by either its hidraw or one of its event
 * nodes. Device numbers match what the TV reported.
 */
static void build_ds4_fixture(void) {
    fixture_mkdir("%s/dev/char", fixture_root);
    fixture_mkdir("%s/devices/platform/usb/%s/input/input14/event16", fixture_root, DS4_HID);
    fixture_mkdir("%s/devices/platform/usb/%s/input/input15/event14", fixture_root, DS4_HID);
    fixture_mkdir("%s/devices/platform/usb/%s/input/input16/event15", fixture_root, DS4_HID);
    fixture_mkdir("%s/devices/platform/usb/%s/hidraw/hidraw0", fixture_root, DS4_HID);

    // 13:78 is /dev/input/event14, 13:80 is event16, 234:0 is /dev/hidraw0
    fixture_symlink("../../devices/platform/usb/" DS4_HID "/input/input15/event14", "%s/dev/char/13:78",
                    fixture_root);
    fixture_symlink("../../devices/platform/usb/" DS4_HID "/input/input14/event16", "%s/dev/char/13:80",
                    fixture_root);
    fixture_symlink("../../devices/platform/usb/" DS4_HID "/hidraw/hidraw0", "%s/dev/char/234:0", fixture_root);

    // An LG remote: a virtual device with no HID parent and no touchpad sibling.
    fixture_mkdir("%s/devices/virtual/input/input11/event11", fixture_root);
    fixture_symlink("../../devices/virtual/input/input11/event11", "%s/dev/char/13:75", fixture_root);
}

/** Whichever node SDL bound, the walk must land on the same HID device. */
static void test_hid_parent_from_any_node(void) {
    char expected[PATH_MAX];
    snprintf(expected, sizeof(expected), "%s/devices/platform/usb/%s", fixture_root, DS4_HID);

    const dev_t nodes[] = {makedev(13, 78), makedev(13, 80), makedev(234, 0)};
    for (size_t i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++) {
        char out[PATH_MAX];
        assert(hid_parent_for_devnum(fixture_root, nodes[i], out, sizeof(out)));
        assert(strcmp(out, expected) == 0);
    }
}

/** A virtual device resolves to its own ancestor, not to anything HID-like. */
static void test_hid_parent_virtual_device(void) {
    char expected[PATH_MAX];
    snprintf(expected, sizeof(expected), "%s/devices/virtual", fixture_root);

    char out[PATH_MAX];
    assert(hid_parent_for_devnum(fixture_root, makedev(13, 75), out, sizeof(out)));
    assert(strcmp(out, expected) == 0);
}

/** An unknown device number must fail rather than resolve to something else. */
static void test_hid_parent_unknown_devnum(void) {
    char out[PATH_MAX];
    assert(!hid_parent_for_devnum(fixture_root, makedev(99, 99), out, sizeof(out)));
}

/** All three of the pad's event nodes are offered as candidates. */
static void test_sibling_nodes_of_hid_device(void) {
    char hid_path[PATH_MAX];
    snprintf(hid_path, sizeof(hid_path), "%s/devices/platform/usb/%s", fixture_root, DS4_HID);

    char names[MAX_SIBLINGS][NODE_NAME_MAX];
    int count = sibling_event_nodes(hid_path, names, MAX_SIBLINGS);
    assert(count == 3);

    bool seen_14 = false, seen_15 = false, seen_16 = false;
    for (int i = 0; i < count; i++) {
        seen_14 |= strcmp(names[i], "event14") == 0;
        seen_15 |= strcmp(names[i], "event15") == 0;
        seen_16 |= strcmp(names[i], "event16") == 0;
    }
    assert(seen_14 && seen_15 && seen_16);
}

/** The virtual device contributes one node, which capability probing rejects. */
static void test_sibling_nodes_of_virtual_device(void) {
    char hid_path[PATH_MAX];
    snprintf(hid_path, sizeof(hid_path), "%s/devices/virtual", fixture_root);

    char names[MAX_SIBLINGS][NODE_NAME_MAX];
    int count = sibling_event_nodes(hid_path, names, MAX_SIBLINGS);
    assert(count == 1);
    assert(strcmp(names[0], "event11") == 0);
}

/** A device with no input children yields nothing rather than misbehaving. */
static void test_sibling_nodes_without_input_dir(void) {
    char hid_path[PATH_MAX];
    snprintf(hid_path, sizeof(hid_path), "%s/devices/platform/usb/%s/hidraw", fixture_root, DS4_HID);

    char names[MAX_SIBLINGS][NODE_NAME_MAX];
    assert(sibling_event_nodes(hid_path, names, MAX_SIBLINGS) == 0);
}

/* --- fixture plumbing --- */

static void fixture_mkdir(const char *fmt, ...) {
    char path[PATH_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(path, sizeof(path), fmt, args);
    va_end(args);

    for (char *p = path + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "mkdir %s: %s\n", path, strerror(errno));
            exit(1);
        }
        *p = '/';
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir %s: %s\n", path, strerror(errno));
        exit(1);
    }
}

static void fixture_symlink(const char *target, const char *fmt, ...) {
    char path[PATH_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(path, sizeof(path), fmt, args);
    va_end(args);

    if (symlink(target, path) != 0 && errno != EEXIST) {
        fprintf(stderr, "symlink %s: %s\n", path, strerror(errno));
        exit(1);
    }
}

static void fixture_rmtree(const char *path) {
    DIR *dir = opendir(path);
    if (dir != NULL) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            char child[PATH_MAX];
            if (snprintf(child, sizeof(child), "%s/%s", path, ent->d_name) < (int) sizeof(child)) {
                fixture_rmtree(child);
            }
        }
        closedir(dir);
    }
    remove(path);
}

int main() {
    char tmpl[] = "/tmp/gamepad_touchpad_tests.XXXXXX";
    if (mkdtemp(tmpl) == NULL) {
        fprintf(stderr, "mkdtemp: %s\n", strerror(errno));
        return 1;
    }
    // hid_parent_for_devnum compares realpath() output, so the root must be
    // canonical too - /tmp is a symlink on some systems.
    if (realpath(tmpl, fixture_root) == NULL) {
        fprintf(stderr, "realpath: %s\n", strerror(errno));
        return 1;
    }
    build_ds4_fixture();

    test_hid_parent_from_any_node();
    test_hid_parent_virtual_device();
    test_hid_parent_unknown_devnum();
    test_sibling_nodes_of_hid_device();
    test_sibling_nodes_of_virtual_device();
    test_sibling_nodes_without_input_dir();

    fixture_rmtree(fixture_root);
    printf("All tests passed\n");
    return 0;
}
