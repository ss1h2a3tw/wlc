#include "internal.h"
#include "session/fd.h"
#include "udev.h"

#include "compositor/compositor.h"
#include "compositor/output.h"

#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <libudev.h>
#include <libinput.h>
#include <wayland-server.h>

static struct input {
   struct libinput *handle;
   struct wl_event_source *event_source;
} input;

static struct udev {
   struct udev *handle;
   struct udev_monitor *monitor;
   struct wl_event_source *event_source;
} udev;

static int
input_open_restricted(const char *path, int flags, void *user_data)
{
   (void)user_data;
   return wlc_fd_open(path, flags, WLC_FD_INPUT);
}

static void
input_close_restricted(int fd, void *user_data)
{
   (void)user_data;
   wlc_fd_close(fd);
}

static const struct libinput_interface libinput_implementation = {
   .open_restricted = input_open_restricted,
   .close_restricted = input_close_restricted,
};

static double
pointer_abs_x(void *internal, uint32_t width)
{
   struct libinput_event_pointer *pev = internal;
   return libinput_event_pointer_get_absolute_x_transformed(pev, width);
}

static double
pointer_abs_y(void *internal, uint32_t height)
{
   struct libinput_event_pointer *pev = internal;
   return libinput_event_pointer_get_absolute_y_transformed(pev, height);
}

static int
input_event(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask;
   struct input *input = data;

   if (libinput_dispatch(input->handle) != 0)
      wlc_log(WLC_LOG_WARN, "Failed to dispatch libinput");

   struct libinput_event *event;
   while ((event = libinput_get_event(input->handle))) {
      struct libinput *handle = libinput_event_get_context(event);
      (void)handle;

      switch (libinput_event_get_type(event)) {
         case LIBINPUT_EVENT_DEVICE_ADDED:
            wlc_log(WLC_LOG_INFO, "INPUT DEVICE ADDED");
            break;

         case LIBINPUT_EVENT_DEVICE_REMOVED:
            wlc_log(WLC_LOG_INFO, "INPUT DEVICE REMOVED");
            break;

         case LIBINPUT_EVENT_POINTER_MOTION:
            {
               struct libinput_event_pointer *pev = libinput_event_get_pointer_event(event);
               struct wlc_input_event ev;
               ev.type = WLC_INPUT_EVENT_MOTION;
               ev.time = libinput_event_pointer_get_time(pev);
               ev.motion.dx = libinput_event_pointer_get_dx(pev);
               ev.motion.dy = libinput_event_pointer_get_dy(pev);
               wl_signal_emit(&wlc_system_signals()->input, &ev);
            }
            break;

         case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
            {
               struct libinput_event_pointer *pev = libinput_event_get_pointer_event(event);
               struct wlc_input_event ev;
               ev.type = WLC_INPUT_EVENT_MOTION_ABSOLUTE;
               ev.time = libinput_event_pointer_get_time(pev);
               ev.motion_abs.x = pointer_abs_x;
               ev.motion_abs.y = pointer_abs_y;
               ev.motion_abs.internal = pev;
               wl_signal_emit(&wlc_system_signals()->input, &ev);
            }
            break;

         case LIBINPUT_EVENT_POINTER_BUTTON:
            {
               struct libinput_event_pointer *pev = libinput_event_get_pointer_event(event);
               struct wlc_input_event ev;
               ev.type = WLC_INPUT_EVENT_BUTTON;
               ev.time = libinput_event_pointer_get_time(pev);
               ev.button.code  = libinput_event_pointer_get_button(pev);
               ev.button.state = (enum wl_pointer_button_state)libinput_event_pointer_get_button_state(pev);
               wl_signal_emit(&wlc_system_signals()->input, &ev);
            }
            break;

         case LIBINPUT_EVENT_POINTER_AXIS:
            {
               struct libinput_event_pointer *pev = libinput_event_get_pointer_event(event);
               struct wlc_input_event ev;
               ev.type = WLC_INPUT_EVENT_SCROLL;
               ev.time = libinput_event_pointer_get_time(pev);
               ev.scroll.amount = libinput_event_pointer_get_axis_value(pev);
               ev.scroll.axis  = (enum wl_pointer_axis)libinput_event_pointer_get_axis(pev);
               wl_signal_emit(&wlc_system_signals()->input, &ev);
            }
            break;

         case LIBINPUT_EVENT_KEYBOARD_KEY:
            {
               struct libinput_event_keyboard *kev = libinput_event_get_keyboard_event(event);
               struct wlc_input_event ev;
               ev.type = WLC_INPUT_EVENT_KEY;
               ev.time = libinput_event_keyboard_get_time(kev);
               ev.key.code = libinput_event_keyboard_get_key(kev);
               ev.key.state = (enum wl_keyboard_key_state)libinput_event_keyboard_get_key_state(kev);
               wl_signal_emit(&wlc_system_signals()->input, &ev);
            }
            break;

         default:
            break;
      }

      libinput_event_destroy(event);
   }

   return 0;
}

static bool
input_set_event_loop(struct wl_event_loop *loop)
{
   if (input.event_source) {
      wl_event_source_remove(input.event_source);
      input.event_source = NULL;
   }

   if (input.handle && loop) {
      if (!(input.event_source = wl_event_loop_add_fd(loop, libinput_get_fd(input.handle), WL_EVENT_READABLE, input_event, &input)))
         return false;

      wl_event_source_check(input.event_source);
   }
   return true;
}

static bool
is_hotplug(uint32_t drm_id, struct udev_device *device)
{
   const char *sysnum;
   if (!(sysnum = udev_device_get_sysnum(device)) || strtol(sysnum, NULL, 10) != drm_id)
      return false;

   const char *val;
   if (!(val = udev_device_get_property_value(device, "HOTPLUG")))
      return false;

   return (strcmp(val, "1") == 0);
}


static int
udev_event(int fd, uint32_t mask, void *data)
{
   (void)fd, (void)mask;
   struct udev *udev = data;
   struct udev_device *device;

   if (!(device = udev_monitor_receive_device(udev->monitor)))
      return 0;

   wlc_log(WLC_LOG_INFO, "udev: got device %s", udev_device_get_sysname(device));

   // FIXME: pass correct drm id
   if (is_hotplug(0, device)) {
      wlc_log(WLC_LOG_INFO, "udev: hotplug");
      // FIXME: update drm outputs here
      goto out;
   }

   const char *action;
   if (!(action = udev_device_get_action(device)))
      goto out;

   if (!strncmp("event", udev_device_get_sysname(device), sizeof("event")) != 0)
      goto out;

   // XXX: Free event loop for any other stuff. We probably should expose this to api.
   if (!strcmp(action, "add"))
      wlc_log(WLC_LOG_INFO, "udev: device added");
   else if (!strcmp(action, "remove")) {
      wlc_log(WLC_LOG_INFO, "udev: device removed");
   }

out:
   udev_device_unref(device);
   return 0;
}

static bool
udev_set_event_loop(struct wl_event_loop *loop)
{
   if (udev.event_source) {
      wl_event_source_remove(udev.event_source);
      udev.event_source = NULL;
   }

   if (udev.handle && udev.monitor && loop) {
      if (!(udev.event_source = wl_event_loop_add_fd(loop, udev_monitor_get_fd(udev.monitor), WL_EVENT_READABLE, udev_event, &udev)))
         return false;

      wl_event_source_check(udev.event_source);
   }
   return true;
}

static void
activated(struct wl_listener *listener, void *data)
{
   (void)listener;
   bool activated = *(bool*)data;

   if (input.handle) {
      if (!activated) {
         libinput_suspend(input.handle);
      } else {
         libinput_resume(input.handle);
      }
   }
}

static struct wl_listener activated_listener = {
   .notify = activated,
};

static void
cb_input_log_handler(struct libinput *input, enum libinput_log_priority priority, const char *format, va_list args)
{
   (void)input, (void)priority;
   wlc_vlog(WLC_LOG_INFO, format, args);
}

bool
wlc_input_has_init(void)
{
   return (input.handle ? true : false);
}

void
wlc_input_terminate(void)
{
   input_set_event_loop(NULL);

   if (input.handle)
      libinput_unref(input.handle);

   memset(&input, 0, sizeof(input));
}

bool
wlc_input_init(void)
{
   assert(udev.handle && "call wlc_udev_init first");

   if (input.handle)
      return true;

   if (!(input.handle = libinput_udev_create_context(&libinput_implementation, &input, udev.handle))) {
      wlc_log(WLC_LOG_WARN, "Failed to create libinput udev context");
      return false;
   }

   const char *xdg_seat = getenv("XDG_SEAT");
   if (libinput_udev_assign_seat(input.handle, (xdg_seat ? xdg_seat : "seat0")) != 0) {
      wlc_log(WLC_LOG_WARN, "Failed to assign seat to libinput");
      return false;
   }

   libinput_log_set_handler(input.handle, &cb_input_log_handler);
   libinput_log_set_priority(input.handle, LIBINPUT_LOG_PRIORITY_ERROR);
   return input_set_event_loop(wlc_event_loop());
}

void
wlc_udev_terminate(void)
{
   if (udev.handle)
      wl_list_remove(&activated_listener.link);

   udev_set_event_loop(NULL);

   if (udev.handle)
      udev_unref(udev.handle);

   memset(&udev, 0, sizeof(udev));
}

bool
wlc_udev_init(void)
{
   if (udev.handle)
      return true;

   if (!(udev.handle = udev_new()))
      return false;

   if (!(udev.monitor = udev_monitor_new_from_netlink(udev.handle, "udev")))
      goto monitor_fail;

   udev_monitor_filter_add_match_subsystem_devtype(udev.monitor, "drm", NULL);
   udev_monitor_filter_add_match_subsystem_devtype(udev.monitor, "input", NULL);

   if (udev_monitor_enable_receiving(udev.monitor) < 0)
      goto monitor_receiving_fail;

   if (!udev_set_event_loop(wlc_event_loop()))
      goto fail;

   wl_signal_add(&wlc_system_signals()->activated, &activated_listener);
   return true;

monitor_fail:
   wlc_log(WLC_LOG_WARN, "Failed to create udev-monitor from netlink");
   goto fail;
monitor_receiving_fail:
   wlc_log(WLC_LOG_WARN, "Failed to enable udev-monitor receiving");
fail:
   udev_set_event_loop(NULL);
   if (udev.handle)
      udev_unref(udev.handle);
   return false;
}