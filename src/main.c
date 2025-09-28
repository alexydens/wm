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
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>

/* Direction */
typedef enum { DIR_HORIZONTAL, DIR_VERTICAL } direction_t;
/* Region of space */
typedef struct {
  xcb_window_t handle;
  int parent, child0, child1;
  direction_t split;
  float factor;
  bool exists;
} region_t;

/* Global state */
static bool running = false;
static xcb_connection_t *connection = NULL;
static const xcb_setup_t *setup = NULL;
static xcb_screen_t *screen = NULL;
static xcb_window_t root = 0;
static xcb_atom_t WM_PROTOCOLS = 0;
static xcb_atom_t WM_TAKE_FOCUS = 0;
static xcb_atom_t WM_DELETE_WINDOW = 0;
static struct xkb_context *xkb_context = NULL;
static struct xkb_keymap *xkb_keymap = NULL;
static struct xkb_state *xkb_state = NULL;
/* For tiling */
#define MAX_REGIONS 100
static region_t regions[MAX_REGIONS];
static int root_region = -1;
static direction_t next_direction;

/* Helper function declaractions */
static void connect(void);
static void get_setup_info(void);
static void get_screen_and_root(void);
static void get_atoms(void);
static void set_event_mask(void);
static void init_xkb(void);
static void grab_keys(void);
static void create_cursor(void);
static void cleanup(void);
static xcb_window_t get_focused_window(void);
static void send_wm_event(xcb_atom_t atom, xcb_window_t window);
static void change_window_rect(
    xcb_window_t window, uint16_t x, uint16_t y, uint16_t width, uint16_t height
);
static int get_empty_region(void);
static void refresh_layout(void);
/* Event handler declaractions */
static void handle_create_notify(xcb_create_notify_event_t *event);
static void handle_destroy_notify(xcb_destroy_notify_event_t *event);
static void handle_map_notify(xcb_map_notify_event_t *event);
static void handle_unmap_notify(xcb_unmap_notify_event_t *event);
static void handle_reparent_notify(xcb_reparent_notify_event_t *event);
static void handle_configure_notify(xcb_configure_notify_event_t *event);
static void handle_gravity_notify(xcb_gravity_notify_event_t *event);
static void handle_map_request(xcb_map_request_event_t *event);
static void handle_configure_request(xcb_configure_request_event_t *event);
static void handle_circulate_request(xcb_circulate_request_event_t *event);
static void handle_key_press(xcb_key_press_event_t *event);
static void handle_key_release(xcb_key_release_event_t *event);

/* Entry point */
int main(int argc, char *argv[]) {
  /* Setup */
  connect();
  get_setup_info();
  get_screen_and_root();
  get_atoms();
  set_event_mask();
  init_xkb();
  grab_keys();
  create_cursor();

  /* Flush changes */
  xcb_flush(connection);

  /* Clear regions */
  for (int i = 0; i < MAX_REGIONS; i++) {
    regions[i].handle = 0;
    regions[i].parent = -1;
    regions[i].child0 = -1;
    regions[i].child1 = -1;
    regions[i].split = DIR_HORIZONTAL;
    regions[i].factor = 0.0f;
    regions[i].exists = false;
  }

  /* Event loop */
  running = true;
  while (running) {
    xcb_generic_event_t *event = xcb_wait_for_event(connection);
    switch (event->response_type & ~0x80) {
      case XCB_CREATE_NOTIFY:
        handle_create_notify((xcb_create_notify_event_t *)event);
        break;
      case XCB_DESTROY_NOTIFY:
        handle_destroy_notify((xcb_destroy_notify_event_t *)event);
        break;
      case XCB_MAP_NOTIFY:
        handle_map_notify((xcb_map_notify_event_t *)event);
        break;
      case XCB_UNMAP_NOTIFY:
        handle_unmap_notify((xcb_unmap_notify_event_t *)event);
        break;
      case XCB_REPARENT_NOTIFY:
        handle_reparent_notify((xcb_reparent_notify_event_t *)event);
        break;
      case XCB_CONFIGURE_NOTIFY:
        handle_configure_notify((xcb_configure_notify_event_t *)event);
        break;
      case XCB_GRAVITY_NOTIFY:
        handle_gravity_notify((xcb_gravity_notify_event_t *)event);
        break;
      case XCB_MAP_REQUEST:
        handle_map_request((xcb_map_request_event_t *)event);
        break;
      case XCB_CONFIGURE_REQUEST:
        handle_configure_request((xcb_configure_request_event_t *)event);
        break;
      case XCB_CIRCULATE_REQUEST:
        handle_circulate_request((xcb_circulate_request_event_t *)event);
        break;
      case XCB_KEY_PRESS:
        handle_key_press((xcb_key_press_event_t *)event);
        break;
      case XCB_KEY_RELEASE:
        handle_key_release((xcb_key_release_event_t *)event);
        break;
      default: /* Ignore unknown events */
        break;
    }
    free(event);
  }
  /* Cleanup */
  cleanup();
  return 0;
  (void)argc;
  (void)argv;
}

/* Helper function definitions */
static void connect(void) {
  connection = xcb_connect(NULL, NULL);
  int connection_error = xcb_connection_has_error(connection);
  if (connection_error) {
    printf("ERROR: Failed to connect to X server (%d)\n", connection_error);
    xcb_disconnect(connection);
    abort();
  }
}
static void get_setup_info(void) {
  setup = xcb_get_setup(connection);
  printf(
      "INFO: setup.protocol_major_version = %d\n",
      setup->protocol_major_version
  );
  printf(
      "INFO: setup.protocol_minor_version = %d\n",
      setup->protocol_minor_version
  );
  if (!setup) {
    printf("ERROR: Failed to get setup information from XCB connection\n");
    abort();
  }
}
static void get_screen_and_root(void) {
  xcb_screen_iterator_t screen_iterator = xcb_setup_roots_iterator(setup);
  screen = screen_iterator.data;
  printf(
      "INFO: screen.width_in_millimeters = %d\n",
      screen->width_in_millimeters
  );
  printf(
      "INFO: screen.height_in_millimeters = %d\n",
      screen->height_in_millimeters
  );
  printf("INFO: screen.width_in_pixels = %d\n", screen->width_in_pixels);
  printf("INFO: screen.height_in_pixels = %d\n", screen->height_in_pixels);
  root = screen->root;

}
static void get_atoms(void) {
  xcb_intern_atom_cookie_t atom_cookie;
  xcb_intern_atom_reply_t *atom_reply = NULL;
  xcb_generic_error_t *error = NULL;
#define GET_ATOM(name, var)\
  atom_cookie = xcb_intern_atom(\
      connection,\
      0,\
      strlen(name), name\
  );\
  atom_reply = xcb_intern_atom_reply(\
      connection, atom_cookie, &error\
  );\
  if (!atom_reply) {\
    if (error)\
      printf(\
          "ERROR: Failed to get atom: %s (%d)\n", name, error->error_code\
      );\
    else\
      printf("ERROR: Failed to get atom: %s (no generic error)\n", name);\
    abort();\
  }\
  var = atom_reply->atom;\
  free(atom_reply);
  GET_ATOM("WM_PROTOCOLS", WM_PROTOCOLS);
  GET_ATOM("WM_TAKE_FOCUS", WM_TAKE_FOCUS);
  GET_ATOM("WM_DELETE_WINDOW", WM_DELETE_WINDOW);

}
static void set_event_mask(void) {
  xcb_generic_error_t *error = NULL;

  uint32_t event_mask[] = {
    XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
    | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
    | XCB_EVENT_MASK_KEY_PRESS
    | XCB_EVENT_MASK_KEY_RELEASE
  };
  xcb_void_cookie_t cookie = xcb_change_window_attributes(
      connection, root,
      XCB_CW_EVENT_MASK, event_mask
  );
  error = xcb_request_check(connection, cookie);
  if (error) {
    printf(
        "ERROR: Failed to change root window event mask (%d)\n",
        error->error_code
    );
    free(error);
    abort();
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
    printf("ERROR: Failed grab keys (%d)\n", error->error_code);
    free(error);
    abort();
  }
}
static void create_cursor(void) {
  xcb_generic_error_t *error = NULL;

  xcb_font_t cursor_font = xcb_generate_id(connection);
  xcb_void_cookie_t cookie = xcb_open_font(
      connection, cursor_font, strlen("cursor"), "cursor"
  );
  error = xcb_request_check(connection, cookie);
  if (error) {
    printf("ERROR: Failed to open cursor font (%d)\n", error->error_code);
    free(error);
    abort();
  }
  xcb_cursor_t cursor = xcb_generate_id(connection);
  cookie = xcb_create_glyph_cursor(
      connection,
      cursor,
      cursor_font, cursor_font,
      68, 69,
      0, 0, 0,
      0xffff, 0xffff, 0xffff
  );
  error = xcb_request_check(connection, cookie);
  if (error) {
    printf("ERROR: Failed to create cursor (%d)\n", error->error_code);
    free(error);
    abort();
  }
  uint32_t cursor_list[] = { cursor };
  cookie = xcb_change_window_attributes(
      connection, root, XCB_CW_CURSOR, cursor_list
  );
  error = xcb_request_check(connection, cookie);
  if (error) {
    printf("ERROR: Failed to set cursor (%d)\n", error->error_code);
    free(error);
    abort();
  }
  xcb_free_cursor(connection, cursor);
  xcb_close_font(connection, cursor_font);
}
static void cleanup(void) {
  xkb_state_unref(xkb_state);
  xkb_keymap_unref(xkb_keymap);
  xkb_context_unref(xkb_context);
  xcb_disconnect(connection);
}
static xcb_window_t get_focused_window(void) {
  xcb_generic_error_t *error = NULL;
  xcb_get_input_focus_cookie_t focus_cookie = xcb_get_input_focus(connection);
  xcb_get_input_focus_reply_t *focus_reply = xcb_get_input_focus_reply(
      connection, focus_cookie, &error
  );
  if (!focus_reply) {
    if (error)
      printf("ERROR: Failed to get focused window (%d)\n", error->error_code);
    else
      printf("ERROR: Failed to get focused window (no generic error)\n");
    abort();
  }
  xcb_window_t window = focus_reply->focus;
  free(focus_reply);
  return window;
}
static void send_wm_event(xcb_atom_t atom, xcb_window_t window) {
  const xcb_client_message_event_t wm_event = {
    .response_type = XCB_CLIENT_MESSAGE,
    .format = 32,
    .window = window,
    .type = WM_PROTOCOLS,
    .data.data32 = { atom, XCB_CURRENT_TIME, 0, 0, 0 }
  };
  xcb_void_cookie_t cookie = xcb_send_event(
      connection,
      0, window,
      XCB_EVENT_MASK_NO_EVENT,
      (const char *)&wm_event
  );
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if (error) {
    printf(
        "ERROR: Failed to send WM_TAKE_FOCUS event (%d)\n",
        error->error_code
    );
    free(error);
  }
}
static void change_window_rect(
    xcb_window_t window, uint16_t x, uint16_t y, uint16_t width, uint16_t height
) {
  uint32_t value_list[4] = { x, y, width, height };
  xcb_void_cookie_t cookie = xcb_configure_window(
      connection, window,
      XCB_CONFIG_WINDOW_X
      | XCB_CONFIG_WINDOW_Y
      | XCB_CONFIG_WINDOW_WIDTH
      | XCB_CONFIG_WINDOW_HEIGHT,
      value_list
  );
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if (error) {
    printf("ERROR: Failed to configure window (%d)\n", error->error_code);
    free(error);
  }
}
static int get_empty_region(void) {
  int region = -1;
  for (int i = 0; i < MAX_REGIONS; i++)
    if (!(regions[i].exists))
      region = i;
  if (region < 0) {
    printf("ERROR: Too many regions\n");
    abort();
  }
  return region;
}
static void refresh_layout(void) {
  if (!root_region) return;
  int current = root_region;
  /* TODO */
}
/* Event handler definitions */
static void handle_create_notify(xcb_create_notify_event_t *event) {
  printf("INFO: recieved create notify\n");
  if (root_region < 0) {
    root_region = 0;
    regions[0].handle = event->window;
    regions[0].parent = -1;
    regions[0].child0 = -1;
    regions[0].child1 = -1;
    regions[0].split = DIR_HORIZONTAL;
    regions[0].factor = 0.0f;
    regions[0].exists = true;
    return;
  }
  int parent = -1;
  xcb_window_t focus = get_focused_window();
  for (int i = 0; i < MAX_REGIONS; i++)
    if (regions[i].handle == focus)
      parent = i;
  if (parent < 0) {
    printf("ERROR: Couldn't find parent\n");
    abort();
  }
  int new_region = get_empty_region();
  int new_window_region = get_empty_region();
  int grandparent = regions[parent].parent;
  if (regions[grandparent].child0 == parent)
    regions[grandparent].child0 = new_region;
  else if (regions[grandparent].child1 == parent)
    regions[grandparent].child1 = new_region;
  else {
    printf("ERROR: Corrupted region tree\n");
    abort();
  }
  regions[parent].parent = new_region;
  regions[new_region].handle = 0;
  regions[new_region].parent = grandparent;
  regions[new_region].child0 = new_window_region;
  regions[new_region].child1 = parent;
  regions[new_region].split = next_direction;
  regions[new_region].factor = 0.5f;
  regions[new_region].exists = true;
  regions[new_window_region].handle = event->window;
  regions[new_window_region].parent = new_region;
  regions[new_window_region].child0 = -1;
  regions[new_window_region].child1 = -1;
  regions[new_window_region].split = DIR_HORIZONTAL;
  regions[new_window_region].factor = 0.0f;
  regions[new_window_region].exists = true;
  refresh_layout();
}
static void handle_destroy_notify(xcb_destroy_notify_event_t *event) {
  printf("INFO: recieved destroy notify\n");
  if (regions[root_window].handle == event->window) {
    regions[root_window].exists = false;
    root_window = -1;
    return;
  }
  int deleting = -1;
  for (int i = 0; i < MAX_REGIONS; i++)
    if (regions[i].handle == event->window)
      deleting = i;
  if (!deleting) {
    printf("WARNING: Destroying window not found in region tree\n");
    return;
  }
  int parent = regions[deleting].parent;
  int grandparent = regions[parent].parent;
  int sibling = -1;
  if (regions[parent].child0 == deleting)
    sibling = regions[parent].child1;
  else if (regions[parent].child1 == deleting)
    sibling = regions[parent].child0;
  else {
    printf("ERROR: Corrupted region tree\n");
    abort();
  }
  regions[deleting].exists = false;
  regions[parent].exists = false;
  if (grandparent < 0) {
    root_region = sibling;
    return;
  }
  if (regions[grandparent].child0 == parent)
    regions[grandparent].child0 = sibling;
  else if (regions[grandparent].child1 == parent)
    regions[grandparent].child1 = sibling;
  else {
    printf("ERROR: Corrupted region tree\n");
    abort();
  }
  refresh_layout();
}
static void handle_map_notify(xcb_map_notify_event_t *event) { }
static void handle_unmap_notify(xcb_unmap_notify_event_t *event) { }
static void handle_reparent_notify(xcb_reparent_notify_event_t *event) { }
static void handle_configure_notify(xcb_configure_notify_event_t *event) { }
static void handle_gravity_notify(xcb_gravity_notify_event_t *event) { }
static void handle_map_request(xcb_map_request_event_t *event) {
  printf("INFO: processing map request\n");

  xcb_void_cookie_t cookie = xcb_map_window(connection, event->window);
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if (error) {
    printf("ERROR: Failed to map window (%d)\n", error->error_code);
    free(error);
  }
  send_wm_event(WM_TAKE_FOCUS, event->window);
  xcb_flush(connection);
}
static void handle_configure_request(xcb_configure_request_event_t *event) {
  printf("INFO: processing configure request\n");
  /* TODO: remove from region tree (allow floating) */

  uint32_t value_list[7];
  uint8_t num_values = 0;
  if (event->value_mask & XCB_CONFIG_WINDOW_X)
    value_list[num_values++] = event->x;
  if (event->value_mask & XCB_CONFIG_WINDOW_Y)
    value_list[num_values++] = event->y;
  if (event->value_mask & XCB_CONFIG_WINDOW_WIDTH)
    value_list[num_values++] = event->width;
  if (event->value_mask & XCB_CONFIG_WINDOW_HEIGHT)
    value_list[num_values++] = event->height;
  if (event->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
    value_list[num_values++] = event->border_width;
  if (event->value_mask & XCB_CONFIG_WINDOW_SIBLING)
    value_list[num_values++] = event->sibling;
  if (event->value_mask & XCB_CONFIG_WINDOW_STACK_MODE)
    value_list[num_values++] = event->stack_mode;

  xcb_void_cookie_t cookie = xcb_configure_window(
      connection, event->window,
      event->value_mask, value_list
  );
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if (error) {
    printf("ERROR: Failed to configure window (%d)\n", error->error_code);
    free(error);
  }
  xcb_flush(connection);
}
static void handle_circulate_request(xcb_circulate_request_event_t *event) { }
static void handle_key_press(xcb_key_press_event_t *event) {
  xkb_keysym_t keysym =
      xkb_state_key_get_one_sym(xkb_state, event->detail);
  if (keysym == XKB_KEY_c && (event->state & XCB_MOD_MASK_1))
    running = false;
  if (
    keysym == XKB_KEY_q
    && (event->state & (XCB_MOD_MASK_1|XCB_MOD_MASK_SHIFT))
  ) {
    send_wm_event(WM_DELETE_WINDOW, get_focused_window());
    xcb_flush(connection);
  }
  if (keysym == XKB_KEY_d && (event->state & XCB_MOD_MASK_1)) {
    char *args[] = { "dmenu_run", "-m", "0", NULL };
    if (!fork()) execvp(args[0], args);
  }
  if (keysym == XKB_KEY_Up && (event->state & XCB_MOD_MASK_1)) {
    char *args[] = { "st", NULL };
    if (!fork()) execvp(args[0], args);
    next_direction = DIR_VERTICAL;
  }
  if (keysym == XKB_KEY_Right && (event->state & XCB_MOD_MASK_1)) {
    char *args[] = { "st", NULL };
    if (!fork()) execvp(args[0], args);
    next_direction = DIR_HORIZONTAL;
  }
}
static void handle_key_release(xcb_key_release_event_t *event) { }
