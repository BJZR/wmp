#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
#define MW 512
#define MWS 16
#define MK 128
#define MC 512
#define CFG "/.config/wmp/wmp.ini"
typedef enum { T, F } m_t;
typedef struct {
  xcb_window_t id;
  int x, y, w, h, ox, oy, ow, oh, ws;
  m_t m;
  unsigned char hid : 1, map : 1, flt : 1;
} w_t;
typedef struct {
  xcb_keycode_t k;
  uint16_t mod;
  char c[MC];
  int a;
} b_t;
typedef struct {
  int n, mas, sel;
} ws_t;
enum {
  A_CMD,
  A_CLOSE,
  A_NEXT,
  A_PREV,
  A_TILE,
  A_FULL,
  A_WS,
  A_HIDE,
  A_UNHIDE,
  A_STACK,
  A_MASTER,
  A_RESIZE_H,
  A_RESIZE_V,
  A_MOVE_WS,
  A_RELOAD
};
static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_key_symbols_t *syms;
static w_t wins[MW];
static b_t keys[MK];
static ws_t ws[MWS];
static int nw, nk, cws, gap = 8, border = 2, mfact = 55;
static uint32_t fc = 0x4c7899, uc = 0x2e3440;
static xcb_window_t dw = XCB_NONE, rw = XCB_NONE;
static int dx, dy, rx, ry, rc;
static void die(const char *msg) {
  fprintf(stderr, "wmp: %s\n", msg);
  exit(1);
}
static void spawn(const char *cmd) {
  if (!fork()) {
    setsid();
    execl("/bin/sh", "sh", "-c", cmd, NULL);
    _exit(1);
  }
}
static xcb_keycode_t ks2kc(xcb_keysym_t ks) {
  xcb_keycode_t *kc = xcb_key_symbols_get_keycode(syms, ks);
  if (!kc)
    return 0;
  xcb_keycode_t ret = *kc;
  free(kc);
  return ret;
}
static int find_win(xcb_window_t id) {
  for (int i = 0; i < nw; i++)
    if (wins[i].id == id)
      return i;
  return -1;
}
static void set_border(int i, uint32_t color) {
  xcb_change_window_attributes(conn, wins[i].id, XCB_CW_BORDER_PIXEL, &color);
}
static void focus_win(int i) {
  if (i < 0 || i >= nw || wins[i].ws != cws || wins[i].hid)
    return;
  for (int j = 0; j < nw; j++)
    if (wins[j].ws == cws && j != i)
      set_border(j, uc);
  xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, wins[i].id,
                      XCB_CURRENT_TIME);
  xcb_configure_window(conn, wins[i].id, XCB_CONFIG_WINDOW_STACK_MODE,
                       (uint32_t[]){XCB_STACK_MODE_ABOVE});
  set_border(i, fc);
  ws[cws].sel = i;
}
static void save_geom(int i) {
  wins[i].ox = wins[i].x;
  wins[i].oy = wins[i].y;
  wins[i].ow = wins[i].w;
  wins[i].oh = wins[i].h;
}
static void restore_geom(int i) {
  wins[i].x = wins[i].ox;
  wins[i].y = wins[i].oy;
  wins[i].w = wins[i].ow;
  wins[i].h = wins[i].oh;
}
static void tile_wins(void) {
  int vis[MW], n = 0;
  for (int i = 0; i < nw; i++)
    if (wins[i].ws == cws && wins[i].m == T && !wins[i].hid && !wins[i].flt)
      vis[n++] = i;
  if (!n)
    return;
  ws[cws].n = n;
  int sw = scr->width_in_pixels, sh = scr->height_in_pixels;
  int mx = gap, my = gap,
      mw = ((n > ws[cws].mas) ? ((sw * mfact) / 100) : sw) - 2 * gap,
      mh = sh - 2 * gap;
  int tx = mx + mw + gap, ty = gap, tw = sw - tx - gap, th = sh - 2 * gap;
  for (int i = 0; i < n; i++) {
    int w = vis[i];
    save_geom(w);
    if (i < ws[cws].mas) {
      wins[w].x = mx;
      wins[w].y = my + (i * (mh / ws[cws].mas));
      wins[w].w = mw - border * 2;
      wins[w].h = (mh / ws[cws].mas) - gap - border * 2;
    } else {
      int si = i - ws[cws].mas;
      int sn = n - ws[cws].mas;
      wins[w].x = tx;
      wins[w].y = ty + (si * (th / sn));
      wins[w].w = tw - border * 2;
      wins[w].h = (th / sn) - gap - border * 2;
    }
    if (wins[w].w < 50)
      wins[w].w = 50;
    if (wins[w].h < 50)
      wins[w].h = 50;
    uint32_t vals[] = {wins[w].x, wins[w].y, wins[w].w, wins[w].h, border};
    xcb_configure_window(
        conn, wins[w].id,
        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
            XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
        vals);
  }
}
static void show_ws(int i) {
  if (i < 0 || i >= MWS || i == cws)
    return;
  for (int j = 0; j < nw; j++) {
    if (wins[j].ws == cws) {
      xcb_unmap_window(conn, wins[j].id);
      wins[j].map = 0;
    }
  }
  cws = i;
  for (int j = 0; j < nw; j++) {
    if (wins[j].ws == cws && !wins[j].hid) {
      xcb_map_window(conn, wins[j].id);
      wins[j].map = 1;
      if (wins[j].m == F) {
        uint32_t vals[] = {0, 0, scr->width_in_pixels, scr->height_in_pixels,
                           0};
        xcb_configure_window(conn, wins[j].id,
                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                                 XCB_CONFIG_WINDOW_WIDTH |
                                 XCB_CONFIG_WINDOW_HEIGHT |
                                 XCB_CONFIG_WINDOW_BORDER_WIDTH,
                             vals);
      }
    }
  }
  if (!ws[cws].mas)
    ws[cws].mas = 1;
  tile_wins();
  int sel = ws[cws].sel;
  if (sel >= 0 && sel < nw && wins[sel].ws == cws && !wins[sel].hid)
    focus_win(sel);
  else {
    for (int j = 0; j < nw; j++)
      if (wins[j].ws == cws && !wins[j].hid) {
        focus_win(j);
        break;
      }
  }
}
static void add_win(xcb_window_t id) {
  if (nw >= MW)
    return;
  xcb_get_geometry_reply_t *geom =
      xcb_get_geometry_reply(conn, xcb_get_geometry(conn, id), NULL);
  if (!geom)
    return;
  wins[nw] = (w_t){.id = id,
                   .x = geom->x,
                   .y = geom->y,
                   .w = geom->width,
                   .h = geom->height,
                   .ox = geom->x,
                   .oy = geom->y,
                   .ow = geom->width,
                   .oh = geom->height,
                   .ws = cws,
                   .m = T,
                   .hid = 0,
                   .map = 1,
                   .flt = 0};
  free(geom);
  uint32_t mask = XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE |
                  XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                  XCB_EVENT_MASK_POINTER_MOTION;
  xcb_change_window_attributes(conn, id, XCB_CW_EVENT_MASK, &mask);
  xcb_change_window_attributes(conn, id, XCB_CW_BORDER_PIXEL, &uc);
  int mas_idx = -1;
  for (int i = 0; i < nw; i++)
    if (wins[i].ws == cws && wins[i].m == T && !wins[i].hid && !wins[i].flt) {
      mas_idx = i;
      break;
    }
  nw++;
  if (mas_idx >= 0) {
    w_t temp = wins[mas_idx];
    wins[mas_idx] = wins[nw - 1];
    wins[nw - 1] = temp;
  }
  if (!ws[cws].mas)
    ws[cws].mas = 1;
  tile_wins();
  focus_win((mas_idx >= 0) ? mas_idx : (nw - 1));
}
static void del_win(xcb_window_t id) {
  int i = find_win(id);
  if (i < 0)
    return;
  for (int j = 0; j < MWS; j++)
    if (ws[j].sel == i)
      ws[j].sel = -1;
    else if (ws[j].sel > i)
      ws[j].sel--;
  memmove(&wins[i], &wins[i + 1], (--nw - i) * sizeof(w_t));
  tile_wins();
  if (nw > 0) {
    for (int j = 0; j < nw; j++)
      if (wins[j].ws == cws && !wins[j].hid) {
        focus_win(j);
        break;
      }
  }
}
static void close_win(void) {
  int sel = ws[cws].sel;
  if (sel >= 0 && sel < nw)
    xcb_kill_client(conn, wins[sel].id);
}
static void next_win(void) {
  if (!nw)
    return;
  int start = (ws[cws].sel < 0) ? 0 : ws[cws].sel;
  int next = (start + 1) % nw;
  while (next != start) {
    if (wins[next].ws == cws && !wins[next].hid) {
      focus_win(next);
      return;
    }
    next = (next + 1) % nw;
  }
}
static void prev_win(void) {
  if (!nw)
    return;
  int start = (ws[cws].sel < 0) ? nw - 1 : ws[cws].sel;
  int prev = (start - 1 + nw) % nw;
  while (prev != start) {
    if (wins[prev].ws == cws && !wins[prev].hid) {
      focus_win(prev);
      return;
    }
    prev = (prev - 1 + nw) % nw;
  }
}
static void set_mode(m_t mode) {
  int sel = ws[cws].sel;
  if (sel < 0 || sel >= nw)
    return;
  if (wins[sel].m == F && mode == T) {
    restore_geom(sel);
    uint32_t vals[] = {wins[sel].x, wins[sel].y, wins[sel].w, wins[sel].h,
                       border};
    xcb_configure_window(
        conn, wins[sel].id,
        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
            XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
        vals);
  }
  wins[sel].m = mode;
  if (mode == F) {
    save_geom(sel);
    uint32_t vals[] = {0, 0, scr->width_in_pixels, scr->height_in_pixels, 0};
    xcb_configure_window(
        conn, wins[sel].id,
        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
            XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
        vals);
    xcb_configure_window(conn, wins[sel].id, XCB_CONFIG_WINDOW_STACK_MODE,
                         (uint32_t[]){XCB_STACK_MODE_ABOVE});
  } else
    tile_wins();
}
static void hide_win(void) {
  int sel = ws[cws].sel;
  if (sel < 0 || sel >= nw || wins[sel].hid)
    return;
  wins[sel].hid = 1;
  xcb_unmap_window(conn, wins[sel].id);
  wins[sel].map = 0;
  tile_wins();
  next_win();
}
static void unhide_all(void) {
  for (int i = 0; i < nw; i++) {
    if (wins[i].ws == cws && wins[i].hid) {
      wins[i].hid = 0;
      xcb_map_window(conn, wins[i].id);
      wins[i].map = 1;
    }
  }
  tile_wins();
  for (int j = 0; j < nw; j++)
    if (wins[j].ws == cws && !wins[j].hid) {
      focus_win(j);
      break;
    }
}
static void cycle_stack(void) {
  int stk[MW], sn = 0, idx = 0;
  for (int i = 0; i < nw; i++)
    if (wins[i].ws == cws && wins[i].m == T && !wins[i].hid && !wins[i].flt) {
      if (idx >= ws[cws].mas)
        stk[sn++] = i;
      idx++;
    }
  if (sn <= 1)
    return;
  w_t temp = wins[stk[0]];
  memmove(&wins[stk[0]], &wins[stk[1]], (sn - 1) * sizeof(w_t));
  wins[stk[sn - 1]] = temp;
  tile_wins();
}
static void cycle_master(void) {
  int sel = ws[cws].sel;
  if (sel < 0 || sel >= nw || wins[sel].ws != cws)
    return;
  int mas = -1;
  for (int i = 0; i < nw; i++) {
    if (wins[i].ws == cws && wins[i].m == T && !wins[i].hid && !wins[i].flt) {
      mas = i;
      break;
    }
  }
  if (mas >= 0 && mas != sel) {
    w_t temp = wins[mas];
    wins[mas] = wins[sel];
    wins[sel] = temp;
    tile_wins();
    focus_win(mas);
  }
}
static void move_win_ws(int target_ws) {
  int sel = ws[cws].sel;
  if (sel < 0 || sel >= nw || target_ws < 0 || target_ws >= MWS ||
      target_ws == cws)
    return;
  wins[sel].ws = target_ws;
  xcb_unmap_window(conn, wins[sel].id);
  wins[sel].map = 0;
  ws[cws].sel = -1;
  tile_wins();
  next_win();
}
static void resize_master(int delta) {
  mfact += delta;
  if (mfact < 10)
    mfact = 10;
  else if (mfact > 90)
    mfact = 90;
  tile_wins();
}
static void parse_key(const char *ks, xcb_keysym_t *keysym, uint16_t *mod) {
  *mod = 0;
  *keysym = 0;
  const char *p = ks;
  while (strstr(p, "+")) {
    if (!strncmp(p, "super+", 6)) {
      *mod |= XCB_MOD_MASK_4;
      p += 6;
    } else if (!strncmp(p, "shift+", 6)) {
      *mod |= XCB_MOD_MASK_SHIFT;
      p += 6;
    } else if (!strncmp(p, "ctrl+", 5)) {
      *mod |= XCB_MOD_MASK_CONTROL;
      p += 5;
    } else if (!strncmp(p, "alt+", 4)) {
      *mod |= XCB_MOD_MASK_1;
      p += 4;
    } else
      break;
  }
  if (!strcmp(p, "Return"))
    *keysym = XK_Return;
  else if (!strcmp(p, "q"))
    *keysym = XK_q;
  else if (!strcmp(p, "j"))
    *keysym = XK_j;
  else if (!strcmp(p, "k"))
    *keysym = XK_k;
  else if (!strcmp(p, "t"))
    *keysym = XK_t;
  else if (!strcmp(p, "f"))
    *keysym = XK_f;
  else if (!strcmp(p, "m"))
    *keysym = XK_m;
  else if (!strcmp(p, "d"))
    *keysym = XK_d;
  else if (!strcmp(p, "w"))
    *keysym = XK_w;
  else if (!strcmp(p, "e"))
    *keysym = XK_e;
  else if (!strcmp(p, "l"))
    *keysym = XK_l;
  else if (!strcmp(p, "r"))
    *keysym = XK_r;
  else if (!strcmp(p, "s"))
    *keysym = XK_s;
  else if (!strcmp(p, "h"))
    *keysym = XK_h;
  else if (!strcmp(p, "u"))
    *keysym = XK_u;
  else if (!strcmp(p, "Tab"))
    *keysym = XK_Tab;
  else if (!strcmp(p, "space"))
    *keysym = XK_space;
  else if (!strcmp(p, "Left"))
    *keysym = XK_Left;
  else if (!strcmp(p, "Right"))
    *keysym = XK_Right;
  else if (!strcmp(p, "Up"))
    *keysym = XK_Up;
  else if (!strcmp(p, "Down"))
    *keysym = XK_Down;
  else if (p[0] >= '1' && p[0] <= '9' && !p[1])
    *keysym = XK_1 + (p[0] - '1');
}
static void load_config(void) {
  char path[512];
  snprintf(path, sizeof(path), "%s%s", getenv("HOME"), CFG);
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  char line[512], key[64], cmd[MC];
  int autostart = 0, settings = 0;
  nk = 0;
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == '\n')
      continue;
    if (strstr(line, "[autostart]")) {
      autostart = 1;
      settings = 0;
      continue;
    }
    if (strstr(line, "[keys]")) {
      autostart = 0;
      settings = 0;
      continue;
    }
    if (strstr(line, "[settings]")) {
      autostart = 0;
      settings = 1;
      continue;
    }
    if (autostart) {
      line[strcspn(line, "\n")] = 0;
      if (strlen(line) > 0)
        spawn(line);
      continue;
    }
    if (settings) {
      char var[64], val[64];
      if (sscanf(line, "%63s = %63s", var, val) == 2) {
        if (!strcmp(var, "gap"))
          gap = atoi(val);
        else if (!strcmp(var, "border"))
          border = atoi(val);
        else if (!strcmp(var, "mfact"))
          mfact = atoi(val);
        else if (!strcmp(var, "focused_color"))
          fc = (val[0] == '#') ? strtol(val + 1, NULL, 16)
                               : strtol(val, NULL, 16);
        else if (!strcmp(var, "unfocused_color"))
          uc = (val[0] == '#') ? strtol(val + 1, NULL, 16)
                               : strtol(val, NULL, 16);
      }
      continue;
    }
    if (sscanf(line, "%63s = %511[^\n]", key, cmd) == 2) {
      if (nk >= MK)
        continue;
      xcb_keysym_t keysym;
      uint16_t mod;
      parse_key(key, &keysym, &mod);
      if (keysym) {
        keys[nk].k = ks2kc(keysym);
        keys[nk].mod = mod;
        strcpy(keys[nk].c, cmd);
        if (!strcmp(cmd, "close"))
          keys[nk].a = A_CLOSE;
        else if (!strcmp(cmd, "next"))
          keys[nk].a = A_NEXT;
        else if (!strcmp(cmd, "prev"))
          keys[nk].a = A_PREV;
        else if (!strcmp(cmd, "tile"))
          keys[nk].a = A_TILE;
        else if (!strcmp(cmd, "full"))
          keys[nk].a = A_FULL;
        else if (!strcmp(cmd, "hide"))
          keys[nk].a = A_HIDE;
        else if (!strcmp(cmd, "unhide"))
          keys[nk].a = A_UNHIDE;
        else if (!strcmp(cmd, "stack"))
          keys[nk].a = A_STACK;
        else if (!strcmp(cmd, "master"))
          keys[nk].a = A_MASTER;
        else if (!strcmp(cmd, "resize_h"))
          keys[nk].a = A_RESIZE_H;
        else if (!strcmp(cmd, "resize_v"))
          keys[nk].a = A_RESIZE_V;
        else if (!strcmp(cmd, "reload"))
          keys[nk].a = A_RELOAD;
        else if (!strncmp(cmd, "move_ws_", 8) && cmd[8] >= '1' && cmd[8] <= '9')
          keys[nk].a = A_MOVE_WS;
        else if (cmd[0] >= '1' && cmd[0] <= '9' && !cmd[1])
          keys[nk].a = A_WS;
        else
          keys[nk].a = A_CMD;
        nk++;
      }
    }
  }
  fclose(f);
}
static void reload_config(void) {
  for (int i = 0; i < nk; i++)
    xcb_ungrab_key(conn, keys[i].k, scr->root, keys[i].mod);
  load_config();
  for (int i = 0; i < nk; i++)
    xcb_grab_key(conn, 1, scr->root, keys[i].mod, keys[i].k,
                 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  xcb_flush(conn);
}
static void handle_key(xcb_key_press_event_t *e) {
  for (int i = 0; i < nk; i++) {
    if (keys[i].k == e->detail && keys[i].mod == e->state) {
      switch (keys[i].a) {
      case A_CMD:
        spawn(keys[i].c);
        break;
      case A_CLOSE:
        close_win();
        break;
      case A_NEXT:
        next_win();
        break;
      case A_PREV:
        prev_win();
        break;
      case A_TILE:
        set_mode(T);
        break;
      case A_FULL:
        set_mode(F);
        break;
      case A_HIDE:
        hide_win();
        break;
      case A_UNHIDE:
        unhide_all();
        break;
      case A_STACK:
        cycle_stack();
        break;
      case A_MASTER:
        cycle_master();
        break;
      case A_RESIZE_H:
        resize_master(5);
        break;
      case A_RESIZE_V:
        resize_master(-5);
        break;
      case A_WS:
        show_ws(keys[i].c[0] - '1');
        break;
      case A_MOVE_WS:
        move_win_ws(keys[i].c[8] - '1');
        break;
      case A_RELOAD:
        reload_config();
        break;
      }
      break;
    }
  }
}
static void handle_button_press(xcb_button_press_event_t *e) {
  int i = find_win(e->event);
  if (i >= 0) {
    focus_win(i);
    if (e->detail == 1 && (e->state & XCB_MOD_MASK_4)) {
      dw = wins[i].id;
      dx = e->root_x - wins[i].x;
      dy = e->root_y - wins[i].y;
      wins[i].flt = 1;
      xcb_grab_pointer(conn, 0, scr->root,
                       XCB_EVENT_MASK_BUTTON_RELEASE |
                           XCB_EVENT_MASK_POINTER_MOTION,
                       XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE,
                       XCB_NONE, XCB_CURRENT_TIME);
    } else if (e->detail == 3 && (e->state & XCB_MOD_MASK_4)) {
      rw = wins[i].id;
      rx = e->root_x;
      ry = e->root_y;
      wins[i].flt = 1;
      rc = ((e->event_x > wins[i].w / 2) ? 1 : 0) |
           ((e->event_y > wins[i].h / 2) ? 2 : 0);
      xcb_grab_pointer(conn, 0, scr->root,
                       XCB_EVENT_MASK_BUTTON_RELEASE |
                           XCB_EVENT_MASK_POINTER_MOTION,
                       XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE,
                       XCB_NONE, XCB_CURRENT_TIME);
    }
  }
}
static void handle_button_release(xcb_button_release_event_t *e) {
  if (dw != XCB_NONE || rw != XCB_NONE) {
    xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
    dw = rw = XCB_NONE;
  }
}
static void handle_motion_notify(xcb_motion_notify_event_t *e) {
  if (dw != XCB_NONE) {
    int i = find_win(dw);
    if (i >= 0) {
      wins[i].x = e->root_x - dx;
      wins[i].y = e->root_y - dy;
      uint32_t vals[] = {wins[i].x, wins[i].y};
      xcb_configure_window(conn, wins[i].id,
                           XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, vals);
    }
  } else if (rw != XCB_NONE) {
    int i = find_win(rw);
    if (i >= 0) {
      int dx = e->root_x - rx;
      int dy = e->root_y - ry;
      if (rc & 1)
        wins[i].w += dx;
      else {
        wins[i].x += dx;
        wins[i].w -= dx;
      }
      if (rc & 2)
        wins[i].h += dy;
      else {
        wins[i].y += dy;
        wins[i].h -= dy;
      }
      if (wins[i].w < 50)
        wins[i].w = 50;
      if (wins[i].h < 50)
        wins[i].h = 50;
      uint32_t vals[] = {wins[i].x, wins[i].y, wins[i].w, wins[i].h};
      xcb_configure_window(conn, wins[i].id,
                           XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                               XCB_CONFIG_WINDOW_WIDTH |
                               XCB_CONFIG_WINDOW_HEIGHT,
                           vals);
      rx = e->root_x;
      ry = e->root_y;
    }
  }
}
static void setup(void) {
  conn = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(conn))
    die("cannot connect to X server");
  scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
  syms = xcb_key_symbols_alloc(conn);
  uint32_t mask = XCB_CW_EVENT_MASK;
  uint32_t values = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                    XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                    XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_BUTTON_PRESS;
  xcb_void_cookie_t cookie =
      xcb_change_window_attributes_checked(conn, scr->root, mask, &values);
  xcb_generic_error_t *error = xcb_request_check(conn, cookie);
  if (error) {
    free(error);
    die("another window manager is running");
  }
  xcb_change_window_attributes(conn, scr->root, XCB_CW_BACK_PIXEL,
                               &scr->black_pixel);
  xcb_clear_area(conn, 0, scr->root, 0, 0, 0, 0);
  for (int i = 0; i < MWS; i++)
    ws[i] = (ws_t){0, 1, -1};
  load_config();
  for (int i = 0; i < nk; i++)
    xcb_grab_key(conn, 1, scr->root, keys[i].mod, keys[i].k,
                 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  xcb_flush(conn);
}
int main(void) {
  setup();
  xcb_generic_event_t *e;
  while ((e = xcb_wait_for_event(conn))) {
    switch (e->response_type & ~0x80) {
    case XCB_MAP_REQUEST: {
      xcb_map_request_event_t *ev = (xcb_map_request_event_t *)e;
      xcb_map_window(conn, ev->window);
      add_win(ev->window);
      break;
    }
    case XCB_UNMAP_NOTIFY: {
      xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t *)e;
      del_win(ev->window);
      break;
    }
    case XCB_KEY_PRESS:
      handle_key((xcb_key_press_event_t *)e);
      break;
    case XCB_ENTER_NOTIFY: {
      xcb_enter_notify_event_t *ev = (xcb_enter_notify_event_t *)e;
      int i = find_win(ev->event);
      if (i >= 0)
        focus_win(i);
      break;
    }
    case XCB_BUTTON_PRESS:
      handle_button_press((xcb_button_press_event_t *)e);
      break;
    case XCB_BUTTON_RELEASE:
      handle_button_release((xcb_button_release_event_t *)e);
      break;
    case XCB_MOTION_NOTIFY:
      handle_motion_notify((xcb_motion_notify_event_t *)e);
      break;
    }
    free(e);
    xcb_flush(conn);
  }
  xcb_key_symbols_free(syms);
  xcb_disconnect(conn);
  return 0;
}
