#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <NVCtrl/NVCtrl.h>
#include <NVCtrl/NVCtrlLib.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { WIDTH = 440, HEIGHT = 336, VIBRANCE = 0, GAMMA = 6, RESET = 11 };
typedef struct { int x, y, w, h; double value; const char *label; } Control;
static const Control controls[] = {
    {24, 68, 392, 28, 0, NULL},
    {24, 108, 72, 28, 0, "0"}, {104, 108, 72, 28, 200, "200"},
    {184, 108, 72, 28, 500, "500"}, {264, 108, 72, 28, 750, "750"},
    {344, 108, 72, 28, 1023, "Max"},
    {24, 192, 392, 28, 0, NULL},
    {24, 232, 92, 28, 0.8, "0.8"}, {124, 232, 92, 28, 1.0, "1.0"},
    {224, 232, 92, 28, 1.2, "1.2"}, {324, 232, 92, 28, 1.5, "1.5"},
    {24, 280, 120, 28, 0, "Reset"}
};
static struct {
    Display *display;
    Window window;
    Pixmap frame;
    GC gc;
    XFontStruct *font;
    int output, maximum, focus, drag;
    double value[2], applied[2];
    int dirty[2];
    long long due;
    unsigned long background, foreground, border, accent, selected;
    char status[96];
} ui;
static char config_path[PATH_MAX];

static int settings_path(void)
{
    const char *base = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char directory[PATH_MAX];
    int length = base && *base ? snprintf(directory, sizeof(directory), "%s", base)
        : home ? snprintf(directory, sizeof(directory), "%s/.config", home) : -1;
    if (length < 0 || length >= (int)sizeof(directory)) return 0;
    length = snprintf(config_path, sizeof(config_path), "%s/plasmaglow-lite.conf", directory);
    return length > 0 && length < (int)sizeof(config_path);
}

static int load_settings(double values[2])
{
    FILE *file = fopen(config_path, "r");
    if (!file) return errno == ENOENT ? 0 : -1;
    int vibrance;
    double gamma;
    char extra;
    int count = fscanf(file, "vibrance=%d gamma=%lf %c", &vibrance, &gamma, &extra);
    int okay = count == 2 && !ferror(file) && vibrance >= 0 && vibrance <= 1023 &&
               isfinite(gamma) && gamma >= 0.5 && gamma <= 2.0;
    fclose(file);
    if (!okay) return -1;
    values[0] = vibrance; values[1] = gamma;
    return 1;
}

static int save_settings(void)
{
    char directory[PATH_MAX], temporary[PATH_MAX];
    strcpy(directory, config_path);
    *strrchr(directory, '/') = '\0';
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) return 0;
    int length = snprintf(temporary, sizeof(temporary), "%s.tmpXXXXXX", config_path);
    if (length < 0 || length >= (int)sizeof(temporary)) return 0;
    int fd = mkstemp(temporary);
    if (fd < 0) return 0;
    FILE *file = fdopen(fd, "w");
    if (!file) { close(fd); unlink(temporary); return 0; }
    int okay = fprintf(file, "vibrance=%.0f\ngamma=%.2f\n", ui.applied[0], ui.applied[1]) > 0;
    if (fclose(file) != 0) okay = 0;
    if (okay && rename(temporary, config_path) == 0) return 1;
    unlink(temporary);
    return 0;
}

static long long milliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int gamma_command(int apply, double *value)
{
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return 0;
    char argument[32];
    snprintf(argument, sizeof(argument), "%.2f", *value);
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        if (apply)
            execlp("xgamma", "xgamma", "-gamma", argument, (char *)NULL);
        else
            execlp("xgamma", "xgamma", (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    if (pid < 0) {
        close(pipefd[0]);
        return 0;
    }
    char output[512];
    size_t used = 0;
    ssize_t count;
    while (used < sizeof(output) - 1 &&
           (count = read(pipefd[0], output + used, sizeof(output) - 1 - used)) != 0) {
        if (count < 0) {
            if (errno == EINTR) continue;
            break;
        }
        used += (size_t)count;
    }
    output[used] = '\0';
    close(pipefd[0]);
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 0;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return 0;
    int found = 0;
    for (char *line = strtok(output, "\n"); line; line = strtok(NULL, "\n")) {
        double red, green, blue;
        if (strlen(line) >= 2 &&
            ((line[0] == '-' && line[1] == '>') || (line[0] == '<' && line[1] == '-')) &&
            sscanf(line + 2, " Red %lf, Green %lf, Blue %lf", &red, &green, &blue) == 3) {
            *value = red;
            found = 1;
        }
    }
    return found;
}

static int find_vibrance(void)
{
    for (int screen = 0; screen < ScreenCount(ui.display); ++screen) {
        if (!XNVCTRLIsNvScreen(ui.display, screen)) continue;
        unsigned char *data = NULL;
        int length;
        if (!XNVCTRLQueryTargetBinaryData(ui.display, NV_CTRL_TARGET_TYPE_X_SCREEN,
                screen, 0, NV_CTRL_BINARY_DATA_DISPLAYS_ENABLED_ON_XSCREEN,
                &data, &length)) continue;
        int *ids = (int *)data;
        for (int i = 1; i <= ids[0]; ++i) {
            NVCTRLAttributeValidValuesRec valid;
            int value;
            if (XNVCTRLQueryValidTargetAttributeValues(ui.display,
                    NV_CTRL_TARGET_TYPE_DISPLAY, ids[i], 0, NV_CTRL_DIGITAL_VIBRANCE,
                    &valid) && valid.type == ATTRIBUTE_TYPE_RANGE &&
                (valid.permissions & ATTRIBUTE_TYPE_WRITE) &&
                XNVCTRLQueryTargetAttribute(ui.display, NV_CTRL_TARGET_TYPE_DISPLAY,
                    ids[i], 0, NV_CTRL_DIGITAL_VIBRANCE, &value)) {
                ui.output = ids[i];
                ui.maximum = (int)valid.u.range.max;
                ui.value[0] = value;
                XFree(data);
                return 1;
            }
        }
        XFree(data);
    }
    return 0;
}

static unsigned long color(const char *name)
{
    XColor exact, allocated;
    XAllocNamedColor(ui.display, DefaultColormap(ui.display, DefaultScreen(ui.display)),
                    name, &allocated, &exact);
    return allocated.pixel;
}

static void rectangle(int x, int y, int w, int h, unsigned long fill)
{
    XSetForeground(ui.display, ui.gc, fill);
    XFillRectangle(ui.display, ui.frame, ui.gc, x, y, (unsigned)w, (unsigned)h);
}

static void text(int x, int y, const char *value)
{
    XSetForeground(ui.display, ui.gc, ui.foreground);
    XDrawString(ui.display, ui.frame, ui.gc, x, y, value, (int)strlen(value));
}

static void draw(void)
{
    rectangle(0, 0, WIDTH, HEIGHT, ui.background);
    text(24, 48, "Vibrance");
    text(24, 172, "Gamma");
    char number[32];
    snprintf(number, sizeof(number), "%.0f", ui.value[0]);
    text(416 - XTextWidth(ui.font, number, (int)strlen(number)), 48, number);
    snprintf(number, sizeof(number), "%.2f", ui.value[1]);
    text(416 - XTextWidth(ui.font, number, (int)strlen(number)), 172, number);
    for (int i = 0; i <= RESET; ++i) {
        const Control *control = &controls[i];
        if (i == VIBRANCE || i == GAMMA) {
            int which = (i == GAMMA);
            double fraction = which ? (ui.value[1] - 0.5) / 1.5 : ui.value[0] / ui.maximum;
            fraction = fmax(0, fmin(1, fraction));
            int position = control->x + 7 + (int)lround(fraction * (control->w - 14));
            rectangle(control->x + 7, control->y + 12, control->w - 14, 4, ui.border);
            rectangle(control->x + 7, control->y + 12, position - control->x - 7, 4, ui.accent);
            rectangle(position - 6, control->y + 4, 12, 20, ui.accent);
        } else {
            int active = i != RESET && fabs(ui.value[i > GAMMA] - control->value) < 0.005;
            rectangle(control->x, control->y, control->w, control->h,
                      active ? ui.selected : ui.background);
            XSetForeground(ui.display, ui.gc, ui.border);
            XDrawRectangle(ui.display, ui.frame, ui.gc, control->x, control->y,
                           (unsigned)control->w - 1, (unsigned)control->h - 1);
            text(control->x + (control->w - XTextWidth(ui.font, control->label,
                 (int)strlen(control->label))) / 2, control->y + 19, control->label);
        }
        if (i == ui.focus) {
            XSetForeground(ui.display, ui.gc, ui.accent);
            XDrawRectangle(ui.display, ui.frame, ui.gc, control->x - 3, control->y - 3,
                           (unsigned)control->w + 5, (unsigned)control->h + 5);
        }
    }
    text(160, 299, "Tab / arrows / Esc");
    text(24, 328, ui.status);
    XCopyArea(ui.display, ui.frame, ui.window, ui.gc, 0, 0, WIDTH, HEIGHT, 0, 0);
    XFlush(ui.display);
}

static void change(int which, double value)
{
    double minimum = which ? 0.5 : 0;
    double maximum = which ? 2.0 : ui.maximum;
    ui.value[which] = fmax(minimum, fmin(maximum, value));
    ui.value[which] = which ? round(ui.value[which] * 100) / 100 : round(ui.value[which]);
    ui.dirty[which] = 1;
    if (!ui.due) ui.due = milliseconds() + 100;
}

static int apply(int persist)
{
    int failed = 0, changed = 0;
    for (int which = 0; which < 2; ++which) {
        if (!ui.dirty[which]) continue;
        double requested = ui.value[which];
        int okay;
        if (which) {
            okay = gamma_command(1, &requested) && fabs(requested - ui.value[1]) < 0.011;
        } else {
            int actual;
            okay = XNVCTRLSetTargetAttributeAndGetStatus(ui.display,
                    NV_CTRL_TARGET_TYPE_DISPLAY, ui.output, 0, NV_CTRL_DIGITAL_VIBRANCE,
                    (int)requested) && XNVCTRLQueryTargetAttribute(ui.display,
                    NV_CTRL_TARGET_TYPE_DISPLAY, ui.output, 0, NV_CTRL_DIGITAL_VIBRANCE,
                    &actual) && actual == (int)requested;
        }
        if (okay) { ui.applied[which] = requested; changed = 1; }
        else {
            ui.value[which] = ui.applied[which];
            snprintf(ui.status, sizeof(ui.status), "%s could not be applied",
                     which ? "Gamma" : "Vibrance");
            failed = 1;
        }
        ui.dirty[which] = 0;
    }
    if (persist && changed && !save_settings()) {
        strcpy(ui.status, "Applied, but settings could not be saved");
        failed = 1;
    }
    if (!failed) strcpy(ui.status, persist ? "Applied and saved" : "Applied");
    ui.due = 0;
    return !failed;
}

static void activate(int control)
{
    if (control == RESET) { change(0, 0); change(1, 1); }
    else if (control != VIBRANCE && control != GAMMA)
        change(control > GAMMA, controls[control].value);
}

static void slide(int control, int x)
{
    const Control *slider = &controls[control];
    double fraction = (double)(x - slider->x - 7) / (slider->w - 14);
    change(control == GAMMA, control == GAMMA ? 0.5 + fraction * 1.5 : fraction * ui.maximum);
}

int main(int argc, char **argv)
{
    setenv("LC_ALL", "C", 1);
    int restore = argc == 2 && strcmp(argv[1], "--apply") == 0;
    if (argc != 1 && !restore) {
        fprintf(stderr, "Usage: %s [--apply]\n", argv[0]);
        return 2;
    }
    if (!settings_path()) { fprintf(stderr, "Cannot determine settings path\n"); return 1; }
    double saved[2];
    if (restore) {
        int loaded = load_settings(saved);
        if (!loaded) return 0;
        if (loaded < 0) { fprintf(stderr, "Cannot read valid saved settings\n"); return 1; }
    }
    ui.display = XOpenDisplay(NULL);
    if (!ui.display) { fprintf(stderr, "Cannot open X display\n"); return 1; }
    if (!find_vibrance() || (!restore && !gamma_command(0, &ui.value[1]))) {
        fprintf(stderr, "Writable NVIDIA Digital Vibrance and xgamma are required\n");
        XCloseDisplay(ui.display);
        return 1;
    }
    ui.applied[0] = ui.value[0]; ui.applied[1] = ui.value[1];
    if (restore) {
        if (saved[0] > ui.maximum) {
            fprintf(stderr, "Saved Vibrance exceeds the display limit\n");
            XCloseDisplay(ui.display);
            return 1;
        }
        ui.value[0] = saved[0]; ui.value[1] = saved[1];
        ui.dirty[0] = ui.dirty[1] = 1;
        int okay = apply(0);
        if (!okay) fprintf(stderr, "%s\n", ui.status);
        XCloseDisplay(ui.display);
        return okay ? 0 : 1;
    }
    ui.drag = -1;
    strcpy(ui.status, "Changes apply immediately");
    int screen = DefaultScreen(ui.display);
    ui.background = color("#ededed"); ui.foreground = color("#242424");
    ui.border = color("#aaaaaa"); ui.accent = color("#657586"); ui.selected = color("#d3dce4");
    ui.window = XCreateSimpleWindow(ui.display, RootWindow(ui.display, screen),
                                   100, 100, WIDTH, HEIGHT, 0, ui.border, ui.background);
    XStoreName(ui.display, ui.window, "PlasmaGlow Lite");
    XClassHint class_hint = { "plasmaglow-lite", "PlasmaGlowLite" };
    XSetClassHint(ui.display, ui.window, &class_hint);
    XSizeHints hints = { .flags = PMinSize | PMaxSize,
        .min_width = WIDTH, .min_height = HEIGHT, .max_width = WIDTH, .max_height = HEIGHT };
    XSetWMNormalHints(ui.display, ui.window, &hints);
    Atom close_atom = XInternAtom(ui.display, "WM_DELETE_WINDOW", False);
    Atom protocols = XInternAtom(ui.display, "WM_PROTOCOLS", False);
    XSetWMProtocols(ui.display, ui.window, &close_atom, 1);
    XSelectInput(ui.display, ui.window, ExposureMask | ButtonPressMask |
                 ButtonReleaseMask | Button1MotionMask | KeyPressMask);
    ui.gc = XCreateGC(ui.display, ui.window, 0, NULL);
    XSetGraphicsExposures(ui.display, ui.gc, False);
    ui.font = XLoadQueryFont(ui.display, "9x15");
    if (!ui.font) ui.font = XLoadQueryFont(ui.display, "fixed");
    XSetFont(ui.display, ui.gc, ui.font->fid);
    ui.frame = XCreatePixmap(ui.display, ui.window, WIDTH, HEIGHT, (unsigned)DefaultDepth(ui.display, screen));
    XMapWindow(ui.display, ui.window);
    int running = 1;
    while (running) {
        while (XPending(ui.display)) {
            XEvent event;
            XNextEvent(ui.display, &event);
            if (event.type == ClientMessage && event.xclient.message_type == protocols &&
                (Atom)event.xclient.data.l[0] == close_atom) {
                running = 0;
            }
            else if (event.type == ButtonPress) {
                for (int i = 0; i <= RESET; ++i) {
                    const Control *c = &controls[i];
                    if (event.xbutton.x < c->x || event.xbutton.x >= c->x + c->w ||
                        event.xbutton.y < c->y || event.xbutton.y >= c->y + c->h) continue;
                    ui.focus = i;
                    if (event.xbutton.button == Button1) {
                        if (i == VIBRANCE || i == GAMMA) { ui.drag = i; slide(i, event.xbutton.x); }
                        else activate(i);
                    } else if ((i == VIBRANCE || i == GAMMA) &&
                               (event.xbutton.button == Button4 || event.xbutton.button == Button5)) {
                        int which = i == GAMMA;
                        change(which, ui.value[which] + (event.xbutton.button == Button4 ? 1 : -1) * (which ? 0.05 : 10));
                    }
                }
            } else if (event.type == MotionNotify && ui.drag >= 0) slide(ui.drag, event.xmotion.x);
            else if (event.type == ButtonRelease && event.xbutton.button == Button1) ui.drag = -1;
            else if (event.type == KeyPress) {
                KeySym key = XLookupKeysym(&event.xkey, 0);
                if (key == XK_Escape) {
                    running = 0;
                }
                else if (key == XK_Tab) ui.focus = (ui.focus + ((event.xkey.state & ShiftMask) ? RESET : 1)) % (RESET + 1);
                else if (key == XK_Return || key == XK_space) activate(ui.focus);
                else if (ui.focus == VIBRANCE || ui.focus == GAMMA) {
                    int which = ui.focus == GAMMA;
                    double step = which ? 0.05 : 10;
                    if (event.xkey.state & ShiftMask) step *= 5;
                    if (key == XK_Left || key == XK_Down) change(which, ui.value[which] - step);
                    else if (key == XK_Right || key == XK_Up) change(which, ui.value[which] + step);
                    else if (key == XK_Home) change(which, which ? 0.5 : 0);
                    else if (key == XK_End) change(which, which ? 2 : ui.maximum);
                }
            }
        }
        if (ui.due && (milliseconds() >= ui.due || !running)) apply(1);
        draw();
        if (!running) break;
        if (XPending(ui.display)) continue;
        int timeout = ui.due ? (int)fmax(0, ui.due - milliseconds()) : -1;
        struct pollfd fd = { .fd = ConnectionNumber(ui.display), .events = POLLIN };
        if (poll(&fd, 1, timeout) < 0 && errno != EINTR) {
            perror("poll");
            break;
        }
    }
    XFreePixmap(ui.display, ui.frame);
    XFreeFont(ui.display, ui.font);
    XFreeGC(ui.display, ui.gc);
    XDestroyWindow(ui.display, ui.window);
    XCloseDisplay(ui.display);
    return 0;
}
