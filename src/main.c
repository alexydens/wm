/*
MIT License

Copyright (c) 2025 Alex Ydens

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/* Includes */
#include <unistd.h>               /* For execvp() */
#include <stdbool.h>              /* For booleans */
#include <stdint.h>               /* For fixed-size integers */
#include <stdarg.h>               /* For variadic arguments */
#include <stdlib.h>               /* For free() and abort() */
#include <string.h>               /* For strlen() */
#include <stdio.h>                /* For console output */
#include <xcb/xcb.h>              /* X (windowing system) C Bindings */
#include <xkbcommon/xkbcommon.h>  /* X KeyBoard helpers */

/* Log levels */
typedef enum {
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARNING,
  LOG_LEVEL_ERROR
} log_level_t;

/* Keymap */
typedef struct {
  uint16_t modifiers;
  xkb_keysym_t keysym;
  void (*handler)(xcb_key_press_event_t *event);
} keymap_t;

/* Shortcut handler declaractions */
static void handle_shortcut_quit(xcb_key_press_event_t *event);

/* Settings */
#define ANSI_LOGS 1
#define MOD1 XCB_MOD_MASK_1
#define SHIFT XCB_MOD_MASK_SHIFT
const keymap_t KEYMAPS[] = {
  { MOD1|SHIFT, XKB_KEY_c, handle_shortcut_quit }
};
#define NUM_KEYMAPS ((int)(sizeof(KEYMAPS)/sizeof(keymap_t)))

/* Constants */
const char *LOG_LEVELS[] = {
#if ANSI_LOGS
  [LOG_LEVEL_INFO] = "\x1b[1;4;96mINFO\x1b[0m: ",
  [LOG_LEVEL_WARNING] = "\x1b[1;4;93mWARNING\x1b[0m: ",
  [LOG_LEVEL_ERROR] = "\x1b[1;4;91mERROR\x1b[0m: ",
#else
#define ADD_LEVEL(lvl) [LOG_LEVEL_##lvl] = #lvl ": ",
  ADD_LEVEL(INFO)
  ADD_LEVEL(WARNING)
  ADD_LEVEL(ERROR)
#undef ADD_LEVEL
#endif
};

/* Global state */
static bool running = false;
static xcb_connection_t *connection = NULL;
static const xcb_setup_t *setup = NULL;
static xcb_screen_t *screen = NULL;
static xcb_window_t root = 0;
static xcb_atom_t WM_PROTOCOLS = 0;
static xcb_atom_t WM_TAKE_FOCUS = 0;
static xcb_atom_t WM_DELETE_WINDOW = 0;
static xcb_atom_t WM_CLASS = 0;
static xcb_atom_t WM_TRANSIENT_FOR = 0;
static xcb_atom_t _NET_WM_WINDOW_TYPE = 0;
static struct xkb_context *xkb_context = NULL;
static struct xkb_keymap *xkb_keymap = NULL;
static struct xkb_state *xkb_state = NULL;

/* Helper function declaractions */
static void log_msg(log_level_t level, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
static void connect(void);
static void cleanup(void);
static void get_setup_info(void);
static xcb_atom_t get_atom(const char *name);
static void set_event_mask(xcb_window_t window, uint32_t event_mask);
static void init_xkb(void);
static void grab_keys(void);

/* Event handler declaractions */
#define DECLARE_HANDLER(event, ident)\
static void handle_##ident (xcb_##ident##_event_t *event);\
static void event_handler_##event (xcb_generic_event_t *event) {\
  handle_##ident((xcb_##ident##_event_t *)event);\
}
DECLARE_HANDLER(CREATE_NOTIFY, create_notify)
DECLARE_HANDLER(DESTROY_NOTIFY, destroy_notify)
DECLARE_HANDLER(MAP_NOTIFY, map_notify)
DECLARE_HANDLER(UNMAP_NOTIFY, unmap_notify)
DECLARE_HANDLER(REPARENT_NOTIFY, reparent_notify)
DECLARE_HANDLER(CONFIGURE_NOTIFY, configure_notify)
DECLARE_HANDLER(GRAVITY_NOTIFY, gravity_notify)
DECLARE_HANDLER(MAP_REQUEST, map_request)
DECLARE_HANDLER(CONFIGURE_REQUEST, configure_request)
DECLARE_HANDLER(CIRCULATE_REQUEST, circulate_request)
DECLARE_HANDLER(KEY_PRESS, key_press)
DECLARE_HANDLER(KEY_RELEASE, key_release)
DECLARE_HANDLER(FOCUS_IN, focus_in)
DECLARE_HANDLER(FOCUS_OUT, focus_out)
#undef DECLARE_HANDLER
static void (*EVENT_HANDLERS[])(xcb_generic_event_t *) = {
#define ADD_HANDLER(event) [XCB_##event] = event_handler_##event,
  ADD_HANDLER(CREATE_NOTIFY)
  ADD_HANDLER(DESTROY_NOTIFY)
  ADD_HANDLER(MAP_NOTIFY)
  ADD_HANDLER(UNMAP_NOTIFY)
  ADD_HANDLER(REPARENT_NOTIFY)
  ADD_HANDLER(CONFIGURE_NOTIFY)
  ADD_HANDLER(GRAVITY_NOTIFY)
  ADD_HANDLER(MAP_REQUEST)
  ADD_HANDLER(CONFIGURE_REQUEST)
  ADD_HANDLER(CIRCULATE_REQUEST)
  ADD_HANDLER(KEY_PRESS)
  ADD_HANDLER(KEY_RELEASE)
  ADD_HANDLER(FOCUS_IN)
  ADD_HANDLER(FOCUS_OUT)
#undef ADD_HANDLER
};

/* Entry point */
int main(int argc, char *argv[]) {
  /* Startup */
  log_msg(LOG_LEVEL_INFO, "Starting...");
  connect();
  get_setup_info();
  WM_PROTOCOLS = get_atom("WM_PROTOCOLS");
  WM_TAKE_FOCUS = get_atom("WM_TAKE_FOCUS");
  WM_DELETE_WINDOW = get_atom("WM_DELETE_WINDOW");
  WM_CLASS = get_atom("WM_CLASS");
  WM_TRANSIENT_FOR = get_atom("WM_TRANSIENT_FOR");
  _NET_WM_WINDOW_TYPE = get_atom("_NET_WM_WINDOW_TYPE");
  set_event_mask(
      root,
      XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
      | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
      | XCB_EVENT_MASK_KEY_PRESS
      | XCB_EVENT_MASK_KEY_RELEASE
      | XCB_EVENT_MASK_FOCUS_CHANGE
  );
  init_xkb();
  grab_keys();

  /* Event loop */
  log_msg(LOG_LEVEL_INFO, "Processing events...");
  running = true;
  while (running) {
    xcb_generic_event_t *event = xcb_wait_for_event(connection);
    uint8_t type = event->response_type & ~0x80;
    if (type < (sizeof(EVENT_HANDLERS)/sizeof(EVENT_HANDLERS[0])))
      if (EVENT_HANDLERS[type])
        EVENT_HANDLERS[type](event);
    free(event);
  }

  /* Cleanup */
  log_msg(LOG_LEVEL_INFO, "Cleaning up...");
  cleanup();
  return 0;
}

/* Helper function definitions */
static void log_msg(log_level_t level, const char *format, ...) {
  fprintf(level == LOG_LEVEL_ERROR ? stderr : stdout, LOG_LEVELS[level]);
  va_list args;
  va_start(args, format);
  vfprintf(level == LOG_LEVEL_ERROR ? stderr : stdout, format, args);
  va_end(args);
  fprintf(level == LOG_LEVEL_ERROR ? stderr : stdout, "\n");
  if (level == LOG_LEVEL_ERROR) abort();
}
static void connect(void) {
  connection = xcb_connect(NULL, NULL);
  int connection_error = xcb_connection_has_error(connection);
  if (connection_error) {
    xcb_disconnect(connection);
    log_msg(
        LOG_LEVEL_ERROR,
        "Failed to connect to X server (%d)", connection_error
    );
  }
}
static void cleanup(void) {
  xkb_state_unref(xkb_state);
  xkb_keymap_unref(xkb_keymap);
  xkb_context_unref(xkb_context);
  xcb_disconnect(connection);
}
static void get_setup_info(void) {
  setup = xcb_get_setup(connection);
  if (!setup)
    log_msg(LOG_LEVEL_ERROR, "Failed to get setup information");
  log_msg(
      LOG_LEVEL_INFO, "setup.protocol_major_version = %d",
      setup->protocol_major_version
  );
  log_msg(
      LOG_LEVEL_INFO, "setup.protocol_minor_version = %d",
      setup->protocol_minor_version
  );
  xcb_screen_iterator_t screen_iterator = xcb_setup_roots_iterator(setup);
  screen = screen_iterator.data;
  log_msg(
      LOG_LEVEL_INFO, "screen.width_in_millimeters = %d",
      screen->width_in_millimeters
  );
  log_msg(
      LOG_LEVEL_INFO, "screen.height_in_millimeters = %d",
      screen->height_in_millimeters
  );
  log_msg(
      LOG_LEVEL_INFO, "screen.width_in_pixels = %d",
      screen->width_in_pixels
  );
  log_msg(
      LOG_LEVEL_INFO, "screen.height_in_pixels = %d",
      screen->height_in_pixels
  );
  root = screen->root;
}
static xcb_atom_t get_atom(const char *name) {
  /*
  Theoretically, it would be better to send out requests for each atom,
  and then gather all the replies, taking advantage of XCB's asynchronous
  design. In practice, that won't save enough time to be worth the messier
  implementation.
  */
  xcb_generic_error_t *error = NULL;
  xcb_intern_atom_cookie_t cookie = xcb_intern_atom(
      connection, 0, strlen(name), name
  );
  xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
      connection, cookie, &error
  );
  if (!reply) {
    if (error)
      log_msg(
          LOG_LEVEL_ERROR,
          "Failed to get atom: %s (%d)", name, error->error_code
      );
    else
      log_msg(LOG_LEVEL_ERROR, "Failed to get atom: %s", name);
  }
  xcb_atom_t atom = reply->atom;
  free(reply);
  if (!atom)
    log_msg(LOG_LEVEL_ERROR, "Failed to get atom: %s", name);
  else
    log_msg(LOG_LEVEL_INFO, "Got atom: %s", name);
  return atom;
}
static void set_event_mask(xcb_window_t window, uint32_t event_mask) {
  xcb_generic_error_t *error = NULL;
  xcb_void_cookie_t cookie = xcb_change_window_attributes(
      connection, window, XCB_CW_EVENT_MASK, &event_mask
  );
  error = xcb_request_check(connection, cookie);
  if (error) {
    int error_code = error->error_code;
    free(error);
    log_msg(
        LOG_LEVEL_ERROR,
        "Failed to change event mask of window %d (%d)",
        (int)window, error_code
    );
  }
}
static void init_xkb(void) {
  xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb_keymap = xkb_keymap_new_from_names(
      xkb_context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS
  );
  xkb_state = xkb_state_new(xkb_keymap);
}
static void grab_keys(void) {
  /* TODO: Make this function take in a specific key combination and grab it */
  xcb_generic_error_t *error = NULL;

  xcb_void_cookie_t cookie = xcb_grab_key(
      connection,
      0,
      root,
      XCB_MOD_MASK_1, XCB_GRAB_ANY,
      XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC
  );
  error = xcb_request_check(connection, cookie);
  if (error) {
    int error_code = error->error_code;
    free(error);
    log_msg(LOG_LEVEL_ERROR, "Failed to grab keys: (%d)", error_code
    );
  }
}

/* Event handler definitions */
static void handle_create_notify(xcb_create_notify_event_t *event) { }
static void handle_destroy_notify(xcb_destroy_notify_event_t *event) { }
static void handle_map_notify(xcb_map_notify_event_t *event) { }
static void handle_unmap_notify(xcb_unmap_notify_event_t *event) { }
static void handle_reparent_notify(xcb_reparent_notify_event_t *event) { }
static void handle_configure_notify(xcb_configure_notify_event_t *event) { }
static void handle_gravity_notify(xcb_gravity_notify_event_t *event) { }
static void handle_map_request(xcb_map_request_event_t *event) { }
static void handle_configure_request(xcb_configure_request_event_t *event) { }
static void handle_circulate_request(xcb_circulate_request_event_t *event) { }
static void handle_key_press(xcb_key_press_event_t *event) {
  xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkb_state, event->detail);
  for (int i = 0; i < NUM_KEYMAPS; i++)
    if ((event->state & KEYMAPS[i].modifiers) && keysym == KEYMAPS[i].keysym)
      KEYMAPS[i].handler(event);
}
static void handle_key_release(xcb_key_release_event_t *event) { }
static void handle_focus_in(xcb_focus_in_event_t *event) { }
static void handle_focus_out(xcb_focus_out_event_t *event) { }

/* Shortcut handler declaractions */
static void handle_shortcut_quit(xcb_key_press_event_t *event) {
  running = false;
}
