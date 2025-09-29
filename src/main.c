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

/* Constants */
#define MAX_REGIONS 100
#define RESIZE_FACTOR 0.05f

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
static xcb_atom_t WM_CLASS = 0;
static xcb_atom_t WM_TRANSIENT_FOR = 0;
static xcb_atom_t _NET_WM_WINDOW_TYPE = 0;
static struct xkb_context *xkb_context = NULL;
static struct xkb_keymap *xkb_keymap = NULL;
static struct xkb_state *xkb_state = NULL;
static xcb_window_t focus_window = 0;
/* For tiling */
static region_t regions[MAX_REGIONS];
static int root_region = -1;

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
static void send_wm_event(xcb_atom_t atom, xcb_window_t window);
static void set_focus(xcb_window_t window);
static void set_focus_region(int region);
static int get_focus_region(void);
static void change_window_rect(
    xcb_window_t window, uint16_t x, uint16_t y, uint16_t width, uint16_t height
);
static int get_empty_region(void);
static void refresh_layout(
    int region, uint16_t x, uint16_t y, uint16_t width, uint16_t height
);
static void add_region(xcb_window_t window);
static void remove_region(int region);
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
static void handle_focus_in(xcb_focus_in_event_t *event);
static void handle_focus_out(xcb_focus_out_event_t *event);

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
      case XCB_FOCUS_IN:
        handle_focus_in((xcb_focus_in_event_t *)event);
        break;
      case XCB_FOCUS_OUT:
        handle_focus_out((xcb_focus_out_event_t *)event);
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
  GET_ATOM("WM_CLASS", WM_CLASS);
  GET_ATOM("WM_TRANSIENT_FOR", WM_TRANSIENT_FOR);
  GET_ATOM("_NET_WM_WINDOW_TYPE", WM_TRANSIENT_FOR);
}
static void set_event_mask(void) {
  xcb_generic_error_t *error = NULL;

  uint32_t event_mask[] = {
    XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
    | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
    | XCB_EVENT_MASK_KEY_PRESS
    | XCB_EVENT_MASK_KEY_RELEASE
    | XCB_EVENT_MASK_FOCUS_CHANGE
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
static void set_focus(xcb_window_t window) {
  send_wm_event(WM_TAKE_FOCUS, window);
  xcb_void_cookie_t cookie = xcb_set_input_focus(
      connection,
      XCB_INPUT_FOCUS_POINTER_ROOT,
      window,
      XCB_CURRENT_TIME
  );
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if (error) {
    printf("ERROR: Failed to set input focus (%d)\n", error->error_code);
    free(error);
  }
  xcb_flush(connection);
}
static xcb_window_t get_focus(void) {
  xcb_generic_error_t *error = NULL;
  xcb_get_input_focus_cookie_t cookie = xcb_get_input_focus(connection);
  xcb_get_input_focus_reply_t *reply = xcb_get_input_focus_reply(
      connection, cookie, &error
  );
  if (!reply) {
    if (error)
      printf("ERROR: Failed to get input focus (%d)\n", error->error_code);
    else
      printf("ERROR: Failed to get input focus (no generic error)\n");
    abort();
  }
  xcb_window_t window = reply->focus;
  free(reply);
  return window;
}
static void set_focus_region(int region) {
  set_focus(regions[region].handle);
}
static int get_focus_region(void) {
  xcb_window_t focus = get_focus();
  printf("INFO: Focus region: %d\n", (int)focus);
  int focus_region = -1;
  for (int i = 0; i < MAX_REGIONS; i++)
    if (regions[i].handle == focus)
      focus = i;
  if (focus_region < 0)
    printf("WARNING: Couldn't find input focus in region tree\n");
  return focus_region;
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
  for (int i = 0; i < MAX_REGIONS; i++) {
    if (!(regions[i].exists)) {
      region = i;
      break;
    }
  }
  if (region < 0) {
    printf("ERROR: Too many regions\n");
    abort();
  }
  return region;
}
/*
Listen, I hate recursion as much as the next person, but the iterative
solution here would be an absolute mess. A mess that would quite possibly
require dynamic memory management.
*/
static void refresh_layout(
    int region, uint16_t x, uint16_t y, uint16_t width, uint16_t height
) {
  if (regions[region].handle) {
    change_window_rect(
        regions[region].handle,
        x, y, width, height
    );
    return;
  }
  uint16_t w = width
    * (regions[region].split == DIR_HORIZONTAL ? regions[region].factor : 1.0f);
  uint16_t h = height
    * (regions[region].split == DIR_VERTICAL ? regions[region].factor : 1.0f);
  refresh_layout(region[regions].child0, x, y, w, h);
  refresh_layout(
      region[regions].child1,
      x + (regions[region].split == DIR_HORIZONTAL) * w,
      y + (regions[region].split == DIR_VERTICAL) * h,
      regions[region].split == DIR_HORIZONTAL ? (width-w) : w,
      regions[region].split == DIR_VERTICAL ? (height-h) : h
  );
}
static void add_region(xcb_window_t window) {
  if (root_region < 0) {
    regions[0].handle = window;
    regions[0].parent = -1;
    regions[0].child0 = -1;
    regions[0].child1 = -1;
    regions[0].split = DIR_HORIZONTAL;
    regions[0].factor = 0.0f;
    regions[0].exists = true;
    root_region = 0;
    //set_focus_region(0);
    refresh_layout(
        root_region,
        0, 0, screen->width_in_pixels, screen->height_in_pixels
    );
    return;
  }
  int focus_region = get_focus_region();
  if (focus_region < 0) focus_region = root_region;
  int new_region = get_empty_region();
  regions[new_region].exists = true;
  int new_window_region = get_empty_region();
  regions[new_window_region].exists = true;
  int parent = regions[focus_region].parent;
  if (parent < 0) root_region = new_region;
  regions[focus_region].parent = new_region;
  regions[new_region].handle = 0;
  regions[new_region].parent = parent;
  regions[new_region].child0 = new_window_region;
  regions[new_region].child1 = focus_region;
  regions[new_region].split = DIR_HORIZONTAL;
  regions[new_region].factor = 0.5f;
  if (parent >= 0) {
    if (regions[parent].child0 == focus_region)
      regions[parent].child0 = new_region;
    else if (regions[parent].child1 == focus_region)
      regions[parent].child1 = new_region;
    else {
      printf("ERROR: Corrupted region tree\n");
      abort();
    }
  }
  regions[new_window_region].handle = window;
  regions[new_window_region].parent = new_region;
  regions[new_window_region].child0 = -1;
  regions[new_window_region].child1 = -1;
  regions[new_window_region].split = DIR_HORIZONTAL;
  regions[new_window_region].factor = 0.0f;
  //set_focus_region(new_window_region);
  refresh_layout(
      root_region,
      0, 0, screen->width_in_pixels, screen->height_in_pixels
  );
}
static void remove_region(int region) {
  regions[region].exists = false;
  int parent = regions[region].parent;
  if (parent < 0) {
    if (region != root_region) {
      printf("ERROR: Corrupted region tree\n");
      abort();
    }
    root_region = -1;
    regions[root_region].exists = false;
    return;
  }
  regions[parent].exists = false;
  int sibling = -1;
  if (regions[parent].child0 == region)
    sibling = regions[parent].child1;
  else if (regions[parent].child1 == region)
    sibling = regions[parent].child0;
  else {
    printf("ERROR: Corrupted region tree\n");
    abort();
  }
  //set_focus_region(sibling);
  int grandparent = regions[parent].parent;
  if (grandparent < 0) {
    root_region = sibling;
    regions[sibling].parent = -1;
    refresh_layout(
        root_region,
        0, 0, screen->width_in_pixels, screen->height_in_pixels
    );
    return;
  }
  regions[sibling].parent = grandparent;
  if (regions[grandparent].child0 == parent)
    regions[grandparent].child0 = sibling;
  else if (regions[grandparent].child1 == parent)
    regions[grandparent].child1 = sibling;
  else {
    printf("ERROR: Corrupted region tree\n");
    abort();
  }
  refresh_layout(
      root_region,
      0, 0, screen->width_in_pixels, screen->height_in_pixels
  );
}
/* Event handler definitions */
static void handle_create_notify(xcb_create_notify_event_t *event) {
  printf("INFO: Recieved create notify\n");
  xcb_generic_error_t *error = NULL;
  xcb_get_window_attributes_cookie_t attributes_cookie =
    xcb_get_window_attributes(connection, event->window);
  xcb_get_window_attributes_reply_t *attributes =
    xcb_get_window_attributes_reply(connection, attributes_cookie, &error);
  if (!attributes) {
    if (error)
      printf(
          "ERROR: Failed to get window attributes (%d)\n",
          error->error_code
      );
    else
      printf("ERROR: Failed to get window attributes (no generic error)\n");
    abort();
  }
  if (attributes->override_redirect) return;
  free(attributes);
  add_region(event->window);
}
static void handle_destroy_notify(xcb_destroy_notify_event_t *event) {
  printf("INFO: recieved destroy notify\n");
  int region = -1;
  for (int i = 0; i < MAX_REGIONS; i++)
    if (regions[i].exists && (regions[i].handle == event->window))
      region = i;
  if (region < 0)
    printf("WARNING: Recieved destroy notify for window not in region tree\n");
  remove_region(region);
}
static void handle_map_notify(xcb_map_notify_event_t *event) { }
static void handle_unmap_notify(xcb_unmap_notify_event_t *event) { }
static void handle_reparent_notify(xcb_reparent_notify_event_t *event) { }
static void handle_configure_notify(xcb_configure_notify_event_t *event) { }
static void handle_gravity_notify(xcb_gravity_notify_event_t *event) { }
static void handle_map_request(xcb_map_request_event_t *event) {
  printf("INFO: Processing map request\n");

  xcb_void_cookie_t cookie = xcb_map_window(connection, event->window);
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if (error) {
    printf("ERROR: Failed to map window (%d)\n", error->error_code);
    free(error);
  }
  xcb_flush(connection);
}
static void handle_configure_request(xcb_configure_request_event_t *event) {
  printf("INFO: Processing configure request\n");

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
    send_wm_event(WM_DELETE_WINDOW, focus_window);
    xcb_flush(connection);
  }
  if (keysym == XKB_KEY_d && (event->state & XCB_MOD_MASK_1)) {
    char *args[] = { "dmenu_run", "-m", "0", NULL };
    if (!fork()) execvp(args[0], args);
  }
  if (keysym == XKB_KEY_Return && (event->state & XCB_MOD_MASK_1)) {
    char *args[] = { "st", NULL };
    if (!fork()) execvp(args[0], args);
  }
  if (keysym == XKB_KEY_k && (event->state & XCB_MOD_MASK_1)) {
    int focus_region = get_focus_region();
    if (focus_region >= 0) {
      int parent = regions[focus_region].parent;
      if (parent >= 0) {
        if (regions[parent].split == DIR_HORIZONTAL)
          regions[parent].split = DIR_VERTICAL;
        else
          regions[parent].split = DIR_HORIZONTAL;
      }
      refresh_layout(
          root_region,
          0, 0, screen->width_in_pixels, screen->height_in_pixels
      );
    }
  }
  if (keysym == XKB_KEY_l && (event->state & XCB_MOD_MASK_1)) {
    int focus_region = get_focus_region();
    if (focus_region >= 0) {
      int parent = regions[focus_region].parent;
      if (parent >= 0) regions[parent].factor += RESIZE_FACTOR;
      if (regions[parent].factor > (1.0f-RESIZE_FACTOR))
        regions[parent].factor = (1.0f-RESIZE_FACTOR);
      refresh_layout(
          root_region,
          0, 0, screen->width_in_pixels, screen->height_in_pixels
      );
    }
  }
  if (keysym == XKB_KEY_h && (event->state & XCB_MOD_MASK_1)) {
    int focus_region = get_focus_region();
    if (focus_region >= 0) {
      int parent = regions[focus_region].parent;
      if (parent >= 0) regions[parent].factor -= RESIZE_FACTOR;
      if (regions[parent].factor < RESIZE_FACTOR)
        regions[parent].factor = RESIZE_FACTOR;
      refresh_layout(
          root_region,
          0, 0, screen->width_in_pixels, screen->height_in_pixels
      );
    }
  }
  if (
    keysym == XKB_KEY_n && (event->state & XCB_MOD_MASK_1)
  ) {
    for (int i = get_focus_region()+1;; i = (i+1)%MAX_REGIONS) {
      if (regions[i].exists && regions[i].handle) {
        set_focus_region(i);
        break;
      }
    }
  }
}
static void handle_key_release(xcb_key_release_event_t *event) { }
static void handle_focus_in(xcb_focus_in_event_t *event) {
  focus_window = event->event;
}
static void handle_focus_out(xcb_focus_out_event_t *event) { }
