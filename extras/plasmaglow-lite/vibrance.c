#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <X11/Xlib.h>
#include <NVCtrl/NVCtrl.h>
#include <NVCtrl/NVCtrlLib.h>

static int integer(const char *text, int *value)
{
    char *end;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno || end == text || *end || parsed < INT_MIN || parsed > INT_MAX)
        return 0;
    *value = (int)parsed;
    return 1;
}

int main(int argc, char **argv)
{
    int selected = -1, requested = 0;
    if (argc != 1 && (argc != 3 || !integer(argv[1], &selected) ||
                     selected < 0 || !integer(argv[2], &requested))) {
        fprintf(stderr, "Usage: %s [DISPLAY_ID RAW_VALUE]\n", argv[0]);
        return 2;
    }
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Cannot open X display; run in the desktop session.\n");
        return 1;
    }
    int result = 1;
    for (int screen = 0; screen < ScreenCount(display); ++screen) {
        if (!XNVCTRLIsNvScreen(display, screen))
            continue;
        unsigned char *data = NULL;
        int length = 0;
        if (!XNVCTRLQueryTargetBinaryData(display, NV_CTRL_TARGET_TYPE_X_SCREEN,
                screen, 0, NV_CTRL_BINARY_DATA_DISPLAYS_ENABLED_ON_XSCREEN,
                &data, &length))
            continue;
        int *ids = (int *)data;
        for (int i = 1; i <= ids[0]; ++i) {
            int id = ids[i], current;
            NVCTRLAttributeValidValuesRec valid;
            if (argc == 3 && id != selected)
                continue;
            if (!XNVCTRLQueryValidTargetAttributeValues(display,
                    NV_CTRL_TARGET_TYPE_DISPLAY, id, 0, NV_CTRL_DIGITAL_VIBRANCE,
                    &valid) || valid.type != ATTRIBUTE_TYPE_RANGE ||
                !XNVCTRLQueryTargetAttribute(display, NV_CTRL_TARGET_TYPE_DISPLAY,
                    id, 0, NV_CTRL_DIGITAL_VIBRANCE, &current)) {
                fprintf(stderr, "DPY-%d: Digital Vibrance unavailable\n", id);
                continue;
            }
            printf("DPY-%d: current=%d range=%" PRId64 "..%" PRId64
                   " writable=%s\n", id, current, valid.u.range.min,
                   valid.u.range.max,
                   (valid.permissions & ATTRIBUTE_TYPE_WRITE) ? "yes" : "no");
            if (argc == 3) {
                if (!(valid.permissions & ATTRIBUTE_TYPE_WRITE) ||
                    requested < valid.u.range.min || requested > valid.u.range.max) {
                    fprintf(stderr, "Value is outside the writable range.\n");
                    continue;
                }
                if (!XNVCTRLSetTargetAttributeAndGetStatus(display,
                        NV_CTRL_TARGET_TYPE_DISPLAY, id, 0,
                        NV_CTRL_DIGITAL_VIBRANCE, requested) ||
                    !XNVCTRLQueryTargetAttribute(display, NV_CTRL_TARGET_TYPE_DISPLAY,
                        id, 0, NV_CTRL_DIGITAL_VIBRANCE, &current) ||
                    current != requested) {
                    fprintf(stderr, "Setting Digital Vibrance failed.\n");
                    continue;
                }
                printf("DPY-%d: applied=%d\n", id, current);
            }
            result = 0;
        }
        XFree(data);
    }
    XCloseDisplay(display);
    if (result)
        fprintf(stderr, "No usable Digital Vibrance control found.\n");
    return result;
}
