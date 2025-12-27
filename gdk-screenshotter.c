#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>

#include <gdk-pixbuf-2.0/gdk-pixbuf/gdk-pixbuf.h>

#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

#define TEMPFILE "/tmp/upwork.png"
#define CACHEFILE "/tmp/upwork-cache.png"
#define CACHE_WINDOW_NAME_FILE "/tmp/upwork-cache-window-name"
#define CACHE_WINDOW_PID_FILE "/tmp/upwork-cache-window-pid"

// Flag to indicate if we're currently using cached data
// This is set when a cached screenshot is returned
static int using_cached_data = 0;

// Check if current workspace is in the allowed list (6, 7, 8, 9)
int is_allowed_workspace() {
    FILE *fp = popen("hyprctl activeworkspace -j | jq -r '.id'", "r");
    if(!fp) {
        printf("WARNING: Could not check workspace, allowing screenshot\n");
        return 1; // Default to allowing if we can't check
    }
    char buf[32];
    if(!fgets(buf, sizeof(buf), fp)) {
        printf("WARNING: Could not read workspace, allowing screenshot\n");
        pclose(fp);
        return 1;
    }
    pclose(fp);

    int workspace_id = atoi(buf);
    printf("Current workspace: %d\n", workspace_id);

    // Check if workspace is 6, 7, 8, or 9
    if(workspace_id >= 6 && workspace_id <= 9) {
        return 1;
    }

    return 0;
}

int screenshot() {
    // override environment
    int pid = fork();
    if(pid == 0) {
        // child
        printf("in child\n");
        char *wld = getenv("WAYLAND_DISPLAY_REAL");
        char *command = getenv("UPWORK_SCREENSHOT_COMMAND");
        int commandLen = strlen(command);
        int locationLen = strlen(TEMPFILE);
        char *complete_command = malloc(commandLen + locationLen + 2);
        memcpy(complete_command, command, commandLen);
        complete_command[commandLen] = ' ';
        memcpy(complete_command + commandLen + 1, TEMPFILE, locationLen + 1);
        printf("complete command %s\n", complete_command);
        printf("wl disp: %s\n", wld);
        if(wld) {
            setenv("WAYLAND_DISPLAY", wld, 1);
        } else {
            printf("WARNING: no WAYLAND_DISPLAY_REAL\n");
        }
        char *shell = getenv("SHELL");
        execlp("/usr/bin/env","-S",shell,"-c", complete_command,NULL);
        perror("exec");
        free(complete_command);
    } else {
        printf("in parent: %d\n", pid);
        int retval = -1;
        waitpid(pid, &retval, 0);
        return retval;
    }
}


// Helper to save current window info to cache files
void cache_current_window_info() {
    FILE *fp;
    char buf[2048];

    // Cache window name
    FILE *name_fp = popen("hyprctl activewindow -j | jq -r '.title'", "r");
    if(name_fp) {
        if(fgets(buf, sizeof(buf), name_fp)) {
            fp = fopen(CACHE_WINDOW_NAME_FILE, "w");
            if(fp) {
                fputs(buf, fp);
                fclose(fp);
                printf("Cached window name: %s", buf);
            }
        }
        pclose(name_fp);
    }

    // Cache window PID
    FILE *pid_fp = popen("hyprctl activewindow -j | jq -r '.pid'", "r");
    if(pid_fp) {
        if(fgets(buf, sizeof(buf), pid_fp)) {
            fp = fopen(CACHE_WINDOW_PID_FILE, "w");
            if(fp) {
                fputs(buf, fp);
                fclose(fp);
                printf("Cached window PID: %s", buf);
            }
        }
        pclose(pid_fp);
    }
}

extern GdkPixbuf* gdk_pixbuf_get_from_window(void *window, gint src_x, gint src_y, gint width, gint height) {
    printf("Preparing to take screenshot...\n");

    GError *err = NULL;
    GdkPixbuf *pixbuf = NULL;

    // Check if we're on an allowed workspace
    if(is_allowed_workspace()) {
        printf("On allowed workspace (6, 7, 8, 9) - taking fresh screenshot\n");
        using_cached_data = 0;  // Using real data

        int res = screenshot();
        if(res != 0) {
            printf("Screenshot call failed: %d\n", res);
            return NULL;
        }
        printf("Screenshot success\n");

        pixbuf = gdk_pixbuf_new_from_file(TEMPFILE, &err);
        if(err || !pixbuf) {
            printf("pixbuf failure: %s\n", err->message);
            if(err) g_error_free(err);
            unlink(TEMPFILE);
            return NULL;
        }

        // Save a copy to cache for use on other workspaces
        GError *save_err = NULL;
        gdk_pixbuf_save(pixbuf, CACHEFILE, "png", &save_err, NULL);
        if(save_err) {
            printf("Warning: Could not save cache: %s\n", save_err->message);
            g_error_free(save_err);
        } else {
            printf("Screenshot cached for other workspaces\n");
            // Also cache the current window info to match the screenshot
            cache_current_window_info();
        }

        unlink(TEMPFILE);
    } else {
        printf("On non-allowed workspace - using cached screenshot\n");

        // Try to load cached screenshot
        pixbuf = gdk_pixbuf_new_from_file(CACHEFILE, &err);
        if(err || !pixbuf) {
            printf("WARNING: No cached screenshot available! Taking fresh one anyway.\n");
            if(err) {
                printf("Cache load error: %s\n", err->message);
                g_error_free(err);
                err = NULL;
            }

            // Fallback: take a fresh screenshot (no cache available)
            using_cached_data = 0;
            int res = screenshot();
            if(res != 0) {
                printf("Screenshot call failed: %d\n", res);
                return NULL;
            }
            pixbuf = gdk_pixbuf_new_from_file(TEMPFILE, &err);
            unlink(TEMPFILE);
            if(err || !pixbuf) {
                printf("pixbuf failure: %s\n", err->message);
                if(err) g_error_free(err);
                return NULL;
            }
        } else {
            printf("Using cached screenshot successfully\n");
            using_cached_data = 1;  // Flag that we're using cached data
        }
    }

    printf("Pixbuf success\n");
    return pixbuf;
}

int wanna_break_dimensions = 0;

gboolean (*real_gdk_pixbuf_save_to_callback)(GdkPixbuf*, GdkPixbufSaveFunc, gpointer, const char*, GError**, ...);
extern gboolean gdk_pixbuf_save_to_callback(GdkPixbuf* pb, GdkPixbufSaveFunc fn, gpointer dat, const char* typ, GError** err, ...) {
    if(!real_gdk_pixbuf_save_to_callback) real_gdk_pixbuf_save_to_callback = dlsym(RTLD_NEXT, "gdk_pixbuf_save_to_callback");

    printf("gdk_pixbuf_save_to_callback args: ");
    va_list list;
    va_start(list, err);
    while(1) {
        char *arg = va_arg(list, char*);
        if(!arg) {
            break;
        }
        printf("%s ", (char*)arg);
    }
    va_end(list);
    printf("\n");

    wanna_break_dimensions = 1;

    gboolean res = real_gdk_pixbuf_save_to_callback(pb, fn, dat, typ, err, NULL); // FIXME
    printf("gdk_pixbuf_save_to_callback(%p, %p, %p, %s, %p, ...) => %d\n", pb, fn, dat, typ, err, res);
    return res;
}


Status (*real_XGetWindowAttributes)(Display *display, Window w, XWindowAttributes *attrs);
extern Status XGetWindowAttributes(Display *display, Window w, XWindowAttributes *attrs) {
    if(!real_XGetWindowAttributes) {
        real_XGetWindowAttributes = dlsym(RTLD_NEXT, "XGetWindowAttributes");
    }

    printf("XGetWindowAttrs for 0x%lX\n", w);
    Status res = real_XGetWindowAttributes(display, w, attrs);
    if(!res) {
        printf("Returned error! 0x%X\n", res);
        attrs->x = 0;
        attrs->y = 0;
        attrs->width = 622;
        attrs->height = 450;
        return 1;
    }
    printf("Coords: %dx%d,%dx%d\n", attrs->x, attrs->y, attrs->width, attrs->height);
    if(wanna_break_dimensions) {
        printf("Will break dimensions this time! :evil: Size is zero now! Ha ha ha!\n");
        // For Upwork 5.8.0.33:
        // When running on Xorg, Upwork first takes snapshot using its Native code.
        // But then it tries to take snapshot again using Electron methods.
        // If the latter succeeds then it is used — they name it "enhancing snapshot".
        // Native code uses gdk_pixbuf_get_from_window() which we override.
        // Electron uses something else - I didn't investigate it enough.
        // But first Electron gets screen dimensions (i.e. root window size).
        // If we spoil these values by setting both width and height to be <= 0
        // then Upwork's JS code will return the snapshot it already has,
        // i.e. the one it took with the native code.
        wanna_break_dimensions = 0;
        attrs->width = 0;
        attrs->height = 0;
    }
    return res;
}

int get_active_window_name(char *buf, int bufsize) {
    // If using cached screenshot, return cached window name to match
    if(using_cached_data) {
        FILE *fp = fopen(CACHE_WINDOW_NAME_FILE, "r");
        if(fp) {
            if(fgets(buf, bufsize, fp)) {
                fclose(fp);
                printf("Using cached window name: %s", buf);
                return strlen(buf)+1;
            }
            fclose(fp);
        }
        printf("Warning: Could not read cached window name, using current\n");
    }

    // Get current active window name from Hyprland
    FILE *fp = popen("hyprctl activewindow -j | jq -r '.title'", "r");
    if(!fp) {
        printf("Could not popen\n");
        return 0;
    }
    if(!fgets(buf, bufsize, fp)) {
        printf("Could not read popen\n");
        pclose(fp);
        return 0;
    }
    pclose(fp);
    return strlen(buf)+1;
}

int get_active_window_pid() {
    // If using cached screenshot, return cached window PID to match
    if(using_cached_data) {
        FILE *fp = fopen(CACHE_WINDOW_PID_FILE, "r");
        if(fp) {
            char buf[128];
            if(fgets(buf, sizeof(buf), fp)) {
                fclose(fp);
                int pid = atoi(buf);
                printf("Using cached window PID: %d\n", pid);
                return pid;
            }
            fclose(fp);
        }
        printf("Warning: Could not read cached window PID, using current\n");
    }

    // Get current active window PID from Hyprland
    FILE *fp = popen("hyprctl activewindow -j | jq -r '.pid'", "r");
    if(!fp) {
        printf("Could not popen\n");
        return 0;
    }
    char buf[128];
    if(!fgets(buf, sizeof(buf), fp)) {
        printf("Could not read popen\n");
        pclose(fp);
        return 0;
    }
    pclose(fp);
    return atoi(buf);
}


int (*real_XGetWindowProperty)(Display *display, Window w, Atom property, long offset, long length, Bool delete, Atom req_type, Atom *actual_type, int *actual_fmt, unsigned long *nitems, unsigned long *bytes_after, unsigned char **prop);
extern int XGetWindowProperty(Display *display, Window w, Atom property, long offset, long length, Bool delete, Atom req_type, Atom *actual_type, int *actual_fmt, unsigned long *nitems, unsigned long *bytes_after, unsigned char **prop) {
    // property:
    // x+35 = _NET_ACTIVE_WINDOW // nitems should be 1
    // x+36 = _NET_WM_PID // nitems should be 1
    // x+37 = _NET_WM_NAME
    // x+38 = _NAME

    if(!real_XGetWindowProperty) {
        real_XGetWindowProperty = dlsym(RTLD_NEXT, "XGetWindowProperty");
    }

    char *propname = XGetAtomName(display, property);
    printf("Requested property: %s\n", propname);
    if(strcmp(propname, "_NET_WM_PID") == 0) {
        *actual_type = 0;
        *actual_fmt = 32;
        *nitems = 1;
        *bytes_after = 0;
        int *val = malloc(sizeof(int));
        *val = get_active_window_pid();
        printf("Will return pid: %d\n", *val);
        *prop = (char*)val;
    }
    if(!strstr(propname, "NAME")) {
        return real_XGetWindowProperty(display, w, property, offset, length, delete, req_type, actual_type, actual_fmt, nitems, bytes_after, prop);
    }
    XFree(propname);

    *actual_type = 0; // seemingly unused by upw
    *actual_fmt = 8; // list of bytes
    *bytes_after = 0;

    char buf[2048];
    int len = get_active_window_name(buf, sizeof(buf));
    if(!len) {
        *nitems = 0;
        return -1;
    }
    *nitems = len;
    *prop = malloc(len);
    memcpy(*prop, buf, len);
    return Success;
}

// Get idle time in milliseconds from Hyprland using hyprctl
unsigned long get_idle_time_ms() {
    // Try to get idle time from hyprctl (Hyprland)
    // hyprctl doesn't have a direct idle time query, so we use a file-based approach
    // The workspace-monitor or a separate idle tracker should update this file
    FILE *fp = fopen("/tmp/upwork-idle-ms", "r");
    if(fp) {
        char buf[32];
        if(fgets(buf, sizeof(buf), fp)) {
            fclose(fp);
            unsigned long idle = strtoul(buf, NULL, 10);
            printf("Got idle time from file: %lu ms\n", idle);
            return idle;
        }
        fclose(fp);
    }
    // If no idle file exists, return 0 (user is active)
    // This ensures Upwork sees the user as active
    printf("No idle file found, reporting 0ms idle (active)\n");
    return 0;
}

Bool (*real_XScreenSaverQueryExtension)(Display *dpy, int *event_base_return, int *error_base_return);
extern Bool XScreenSaverQueryExtension(Display *dpy, int *event_base_return, int *error_base_return) {
    if(!real_XScreenSaverQueryExtension) {
        real_XScreenSaverQueryExtension = dlsym(RTLD_NEXT, "XScreenSaverQueryExtension");
    }
    printf("XScreenSaverQueryExtension called\n");
    // Always return True to indicate the extension is available
    *event_base_return = 0;
    *error_base_return = 0;
    return True;
}

Status (*real_XScreenSaverQueryInfo)(Display *dpy, Drawable drawable, XScreenSaverInfo *info);
extern Status XScreenSaverQueryInfo(Display *dpy, Drawable drawable, XScreenSaverInfo *info) {
    printf("XScreenSaverQueryInfo called - this is where Upwork gets idle time!\n");

    // Get the actual idle time
    unsigned long idle_ms = get_idle_time_ms();

    // Fill in the info structure
    info->window = drawable;
    info->state = ScreenSaverOff;  // Screen saver is off
    info->kind = ScreenSaverBlanked;
    info->til_or_since = 0;
    info->idle = idle_ms;  // Idle time in milliseconds
    info->eventMask = 0;

    printf("Reporting idle time: %lu ms\n", idle_ms);
    return 1;  // Success
}

XScreenSaverInfo* (*real_XScreenSaverAllocInfo)(void);
extern XScreenSaverInfo* XScreenSaverAllocInfo(void) {
    printf("XScreenSaverAllocInfo called\n");
    XScreenSaverInfo *info = malloc(sizeof(XScreenSaverInfo));
    memset(info, 0, sizeof(XScreenSaverInfo));
    return info;
}

// Get REAL mouse position from Hyprland
// This reports actual cursor position, not fake data
int get_real_cursor_pos(int *x, int *y) {
    FILE *fp = popen("hyprctl cursorpos 2>/dev/null", "r");
    if(!fp) {
        return 0;
    }
    char buf[64];
    if(!fgets(buf, sizeof(buf), fp)) {
        pclose(fp);
        return 0;
    }
    pclose(fp);

    // Parse "X, Y" format
    if(sscanf(buf, "%d, %d", x, y) == 2) {
        return 1;
    }
    return 0;
}

// Intercept XQueryPointer to report REAL mouse position from Wayland
// This is necessary because XWayland doesn't properly report cursor position
// for windows that don't have focus
Bool (*real_XQueryPointer)(Display *display, Window w, Window *root_return, Window *child_return,
                           int *root_x_return, int *root_y_return, int *win_x_return, int *win_y_return,
                           unsigned int *mask_return);
extern Bool XQueryPointer(Display *display, Window w, Window *root_return, Window *child_return,
                          int *root_x_return, int *root_y_return, int *win_x_return, int *win_y_return,
                          unsigned int *mask_return) {
    if(!real_XQueryPointer) {
        real_XQueryPointer = dlsym(RTLD_NEXT, "XQueryPointer");
    }

    // Call the real function first
    Bool res = real_XQueryPointer(display, w, root_return, child_return,
                                   root_x_return, root_y_return, win_x_return, win_y_return, mask_return);

    // Get the REAL cursor position from Hyprland
    int real_x, real_y;
    if(get_real_cursor_pos(&real_x, &real_y)) {
        // Override with actual cursor position from Wayland compositor
        if(root_x_return) *root_x_return = real_x;
        if(root_y_return) *root_y_return = real_y;
        if(win_x_return) *win_x_return = real_x;
        if(win_y_return) *win_y_return = real_y;
    }

    return res;
}

#ifdef TEST
int main() {
    GdkPixbuf *res = gdk_pixbuf_get_from_window(NULL, 0, 0, 1024, 1024);
    printf("res: %p\n", res);
    gboolean success = gdk_pixbuf_save(res, "/tmp/upwork-out.jpg", "jpeg", NULL, NULL);
    printf("Pixbuf save: %d\n", success);
}
#endif
