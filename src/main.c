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

/*
# Because *real programmers* make their own window managers.
I wonder if I can get treesitter to highlight markdown in a c comment.
> That was a waste of time. This has completely ruined Vim for me. Better switch
to Emacs.
> That last sentence was a joke. Do I really seem like the kind of person to use
Emacs??!?!!!?
## Credits
- Wherever the XCB and XKBCOMMON docs are, saved me trawling through headers
- I did try to look at the DWM source code, but I gave up pretty quickly. That
being said, seeing how short it was might be the only reason I started writing
this. I use a lot the tools from suckless software, they work pretty well, and
are easy to understand. Definetly check them out.
## Here are some more stable and just generally better projects I have used:
### X:
- i3wm
- dwm
### Wayland:
- Hyprland
### If you want a more user-friendly experience:
- GNOME
- KDE Plasma
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

/* Function declaractions */
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
  /* Variables */
  xcb_generic_error_t *error = NULL;
  xcb_void_cookie_t void_cookie;

  /* Connect to X server */
  connection = xcb_connect(NULL, NULL);
  int connection_error = xcb_connection_has_error(connection);
  if (connection_error) {
    printf("ERROR: Failed to connect to X server (%d)\n", connection_error);
    xcb_disconnect(connection);
    abort();
  }

  /* Get conenection setup info */
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

  /* Get the first screen */
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

  /* Get the root window */
  root = screen->root;

  /* Get some atoms */
  xcb_intern_atom_cookie_t atom_cookie;
  xcb_intern_atom_reply_t *atom_reply = NULL;
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

  /* Set the event mask */
  uint32_t event_mask[] = {
    XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
    | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
    | XCB_EVENT_MASK_KEY_PRESS
    | XCB_EVENT_MASK_KEY_RELEASE
  };
  void_cookie = xcb_change_window_attributes(
      connection, root,
      XCB_CW_EVENT_MASK, event_mask
  );
  error = xcb_request_check(connection, void_cookie);
  if (error) {
    printf(
        "ERROR: Failed to change root window event mask (%d)\n",
        error->error_code
    );
    free(error);
    abort();
  }

  /* XKB setup for keyboard input */
  xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb_keymap = xkb_keymap_new_from_names(
      xkb_context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS
  );
  xkb_state = xkb_state_new(xkb_keymap);
  
  /* Grab keys */
  void_cookie = xcb_grab_key(
      connection,
      0,
      root,
      XCB_MOD_MASK_1, XCB_GRAB_ANY,
      XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC
  );
  error = xcb_request_check(connection, void_cookie);
  if (error) {
    printf("ERROR: Failed grab keys (%d)\n", error->error_code);
    free(error);
    abort();
  }

  /* Create cursor */
  xcb_font_t cursor_font = xcb_generate_id(connection);
  void_cookie = xcb_open_font(
      connection, cursor_font, strlen("cursor"), "cursor"
  );
  error = xcb_request_check(connection, void_cookie);
  if (error) {
    printf("ERROR: Failed to open cursor font (%d)\n", error->error_code);
    free(error);
    abort();
  }
  xcb_cursor_t cursor = xcb_generate_id(connection);
  void_cookie = xcb_create_glyph_cursor(
      connection,
      cursor,
      cursor_font, cursor_font,
      68, 69,
      0, 0, 0,
      0xffff, 0xffff, 0xffff
  );
  error = xcb_request_check(connection, void_cookie);
  if (error) {
    printf("ERROR: Failed to create cursor (%d)\n", error->error_code);
    free(error);
    abort();
  }
  uint32_t cursor_list[] = { cursor };
  void_cookie = xcb_change_window_attributes(
      connection, root, XCB_CW_CURSOR, cursor_list
  );
  error = xcb_request_check(connection, void_cookie);
  if (error) {
    printf("ERROR: Failed to set cursor (%d)\n", error->error_code);
    free(error);
    abort();
  }
  xcb_free_cursor(connection, cursor);
  xcb_close_font(connection, cursor_font);

  /* Flush changes */
  xcb_flush(connection);

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

  /* Cleanup XKB structures */
  xkb_state_unref(xkb_state);
  xkb_keymap_unref(xkb_keymap);
  xkb_context_unref(xkb_context);
  
  /* Disconnect from X server */
  xcb_disconnect(connection);
  return 0;
  (void)argc;
  (void)argv;
}

/* Funcion definitions */
static void handle_create_notify(xcb_create_notify_event_t *event) { }
static void handle_destroy_notify(xcb_destroy_notify_event_t *event) { }
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
  const xcb_client_message_event_t wm_event = {
    .response_type = XCB_CLIENT_MESSAGE,
    .format = 32,
    .window = event->window,
    .type = WM_PROTOCOLS,
    .data.data32 = { WM_TAKE_FOCUS, XCB_CURRENT_TIME, 0, 0, 0 }
  };
  cookie = xcb_send_event(
      connection,
      0, event->window,
      XCB_EVENT_MASK_NO_EVENT,
      (const char *)&wm_event
  );
  error = xcb_request_check(connection, cookie);
  if (error) {
    printf(
        "ERROR: Failed to send WM_TAKE_FOCUS event (%d)\n",
        error->error_code
    );
    free(error);
  }
  xcb_flush(connection);
}
static void handle_configure_request(xcb_configure_request_event_t *event) {
  printf("INFO: processing configure request\n");

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
  if (
    xkb_state_key_get_one_sym(xkb_state, event->detail) == XKB_KEY_c
    && (event->state & XCB_MOD_MASK_1)
  )
    running = false;
  if (
    xkb_state_key_get_one_sym(xkb_state, event->detail) == XKB_KEY_q
    && (event->state & (XCB_MOD_MASK_1|XCB_MOD_MASK_SHIFT))
  ) {
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
    const xcb_client_message_event_t wm_event = {
      .response_type = XCB_CLIENT_MESSAGE,
      .format = 32,
      .window = window,
      .type = WM_PROTOCOLS,
      .data.data32 = { WM_DELETE_WINDOW, XCB_CURRENT_TIME, 0, 0, 0 }
    };
    xcb_void_cookie_t cookie = xcb_send_event(
        connection,
        0, window,
        XCB_EVENT_MASK_NO_EVENT,
        (const char *)&wm_event
    );
    error = xcb_request_check(connection, cookie);
    if (error) {
      printf(
          "ERROR: Failed to send WM_DELETE_WINDOW event (%d)\n",
          error->error_code
      );
      free(error);
    }
    xcb_flush(connection);
  }
  if (
    xkb_state_key_get_one_sym(xkb_state, event->detail) == XKB_KEY_Return
    && (event->state & XCB_MOD_MASK_1)
  ) {
    char *args[] = { "st", NULL };
    if (!fork()) execvp(args[0], args);
  }
  if (
    xkb_state_key_get_one_sym(xkb_state, event->detail) == XKB_KEY_d
    && (event->state & XCB_MOD_MASK_1)
  ) {
    char *args[] = { "dmenu_run", "-m", "0", NULL };
    if (!fork()) execvp(args[0], args);
  }
}
static void handle_key_release(xcb_key_release_event_t *event) { }
