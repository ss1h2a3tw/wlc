#ifndef _WLC_SEAT_H_
#define _WLC_SEAT_H_

#include <stdint.h>
#include <wayland-server-protocol.h>

struct wl_global;
struct wlc_view;
struct wlc_pointer;
struct wlc_keyboard;
struct wlc_compositor;
struct wlc_data_device;
struct wlc_origin;

struct wlc_seat {
   struct wl_global *global;
   struct wlc_keymap *keymap;
   struct wlc_pointer *pointer;
   struct wlc_keyboard *keyboard;
   struct wlc_compositor *compositor;
   struct wlc_data_device *device;

   struct {
      // FIXME: use listener instead, these are global events
      void (*pointer_motion)(struct wlc_seat *seat, const struct wlc_origin *pos);
      void (*pointer_scroll)(struct wlc_seat *seat, enum wl_pointer_axis axis, double amount);
      void (*pointer_button)(struct wlc_seat *seat, uint32_t button, enum wl_pointer_button_state state);
      void (*keyboard_key)(struct wlc_seat *seat, uint32_t key, enum wl_keyboard_key_state state);
      void (*keyboard_focus)(struct wlc_seat *seat, struct wlc_view *view);
      void (*view_unfocus)(struct wlc_seat *seat, struct wlc_view *view);
   } notify;
};

void wlc_seat_free(struct wlc_seat *seat);
struct wlc_seat* wlc_seat_new(struct wlc_compositor *compositor);

#endif /* _WLC_SEAT_H_ */
