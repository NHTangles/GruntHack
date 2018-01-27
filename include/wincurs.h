/* vim:set cin ft=c sw=4 sts=4 ts=8 et ai cino=Ls\:0t0(0 : -*- mode:c;fill-column:80;tab-width:8;c-basic-offset:4;indent-tabs-mode:nil;c-file-style:"k&r" -*-*/

#ifndef WINCURS_H
# define WINCURS_H

/* Global declarations for curses interface */

/* Don't move #include "uncursed.h" here. This results in a name clash with
   uncursed delay_output vs NetHack's delay_output */

int term_rows, term_cols; /* size of underlying terminal */

WINDOW *mapwin, *statuswin, *messagewin;    /* Main windows */

# define TEXTCOLOR   /* Allow color */
# define NHW_END 19
# define OFF 0
# define ON 1
# define NONE -1
# define DIALOG_BORDER_COLOR CLR_MAGENTA
# define ALERT_BORDER_COLOR CLR_RED
# define DIALOG_BORDER_ATTR curses_color_attr(DIALOG_BORDER_COLOR, 0)
# define SCROLLBAR_COLOR CLR_MAGENTA
# define SCROLLBAR_BACK_COLOR CLR_BLACK
# define HIGHLIGHT_COLOR CLR_WHITE
# define MORECOLOR CLR_ORANGE
# define STAT_UP_COLOR CLR_GREEN
# define STAT_DOWN_COLOR CLR_RED

# define BASE_WIN    1
# define MESSAGE_WIN 2
# define STATUS_WIN  3
# define MAP_WIN     4
# define INV_WIN     5
# define COUNT_WIN   6
# define OTHER_WIN   7
# define NHWIN_MAX   8
# define MESG_HISTORY_MAX   200
# define USE_DARKGRAY /* Allow "bright" black; delete if not visible */
# define CURSES_DARK_GRAY    17
# define MAP_SCROLLBARS
# define FRAME_PAIR 499
# define HIGH_PAIR 500 /* see cursinit.c */

typedef enum orient_type {
    CENTER,
    UP,
    DOWN,
    RIGHT,
    LEFT,
    INV_RIGHT,
    BELOW_SPLASH,
    UNDEFINED
} orient;

enum roletyp {
    CR_NONE,
    CR_ROLE,
    CR_RACE,
    CR_GEND,
    CR_ALIGN,
    CR_SPECIAL,
};

/* Window type */
enum curswintyp {
    CW_BASE,
    CW_DIAL,
};

# define curses_is_mainwin(typ)                         \
    ((typ) >= CW_FIRST_MAIN && (typ) <= CW_LAST_MAIN)

/* Curses window info. Curses windows are a linked list of windows, in the order
   of when they were created. Upon a refresh, windows are sequentially refreshed
   (using the refresh callback) from first to last. If refresh isn't defined,
   wnoutrefresh will be called instead, potentially twice in case a border is
   involved (once for a temporary window just for the border). Resize being
   undefined will cause a fatal error. */
struct curswin {
    struct curswin *next;
    WINDOW *uwin; /* uncursed window */
    enum curswintyp typ;
    winid id; /* window ID */
    boolean border; /* ignored for base windows */
    attr_t border_attr; /* potential special border attribute */
    orient align;
    struct curswin *win_align;
    /* TRUE if align+wid_align means "align *from* win rather than *onto* it"
7       (if align is CENTER and xy 0, this is ignored) */
    boolean align_outside_win;

    /* If nonzero, this is drawn on top. If this is
       also 2, we will draw the cursor here.
       If no window has this set, the map window
       will have focus 2 unless any dialog window
       exist, in which the last one will have focus 1.
       If more than one window has focus, the last
       window focus will apply. */
    int focus;

    /* Whether we're in a counting state, and if so,
       progress on it. */
    int count;

    /* These are suggested sizes. Use uwin to check real dimensions. uwin data
       does not include the border, nor does height/width. x/y positioning,
       however, does (so if you set them to 1,1, the actual window starts at
       3,2; border + horizontal padding) */
    /* height/width 0 means "cover the rest of the screen/window" */
    int height;
    int width;
    /* 0: use align, >0: from top/left, <0: from bottom/right */
    int x;
    int y;
    void (*resize) (struct curswin *); /* called upon a screen resize */
    void (*redraw) (struct curswin *); /* called upon refreshing */
    void *extra; /* extra data specific to certain window types */
};

/* struct grouping curses globals */
struct cursstate {
    boolean ingame; /* whether or not we are ingame */
    int in_error; /* 0: no error, 1: nonfatal, 2: fatal */
     /* If we're in resize logic. Causes key input to pass KEY_RESIZE directly
        rather than trying to handle it. This is used to prevent an unbounded
        stack chain: window too small -> resize -> -> too small -> ... */
    boolean in_resize;

    /*
     * Border can be one of:
     * 0: None
     * 1: Windows/menus only
     * 2: Everywhere except the outer game frame
     * 3: Everywhere
     * Border will automatically be set to 1 on init/resize if they don't fit,
     * otherwise it will respect the iflags.windowborder variable
     */
    int border;
    int frame; /* color of the main frame */

    /* orient left/right will revert to top (msg)/bottom (status), the defaults,
       if the selected orientation doesn't fit. */
    orient msgorient;
    orient statorient;

    /* usually respects perm_invent, might be disabled if it doesn't fit */
    boolean sidebar;

    /* The game doesn't tell us if sidebar state changes... so keep track of
       old state here. Used to figure out if we need to redraw to try to fit
       it in, so we don't constantly try to fit it in just because the option
       is inconsistent with reality because it doesn't fit. */
    boolean last_perm_invent;
    struct curswin *winlist;
    int next_wid; /* next window ID */
};

struct cursstate curses_state;

/* cursmain.c */

extern struct window_procs curses_procs;

extern void curses_init_nhwindows(int* argcp, char** argv);
extern void curses_player_selection(void);
extern void curses_askname(void);
extern void curses_get_nh_event(void);
extern void curses_exit_nhwindows(const char *str);
extern void curses_suspend_nhwindows(const char *str);
extern void curses_resume_nhwindows(void);
extern winid curses_create_nhwindow(int type);
extern void curses_clear_nhwindow(winid wid);
extern void curses_display_nhwindow(winid wid, BOOLEAN_P block);
extern void curses_destroy_nhwindow(winid wid);
extern void curses_curs(winid wid, int x, int y);
extern void curses_putstr(winid wid, int attr, const char *text);
extern void curses_display_file(const char *filename,BOOLEAN_P must_exist);
extern void curses_start_menu(winid wid);
extern void curses_add_menu(winid wid, int glyph, const ANY_P * identifier,
                            CHAR_P accelerator, CHAR_P group_accel, int attr,
                            const char *str, BOOLEAN_P presel);
extern void curses_end_menu(winid wid, const char *prompt);
extern int curses_select_menu(winid wid, int how, MENU_ITEM_P **selected);
extern void curses_update_inventory(void);
extern void curses_mark_synch(void);
extern void curses_wait_synch(void);
extern void curses_cliparound(int x, int y);
extern void curses_print_glyph(winid wid,XCHAR_P x,XCHAR_P y,int glyph);
extern void curses_raw_print(const char *str);
extern void curses_raw_print_bold(const char *str);
extern int curses_nhgetch(void);
extern int curses_nh_poskey(int *x, int *y, int *mod);
extern void curses_nhbell(void);
extern int curses_doprev_message(void);
extern char curses_yn_function(const char *question, const char *choices,
                               CHAR_P def);
extern void curses_getlin(const char *question, char *input);
extern int curses_get_ext_cmd(void);
extern void curses_number_pad(int state);
extern void curses_delay_output(void);
extern void curses_start_screen(void);
extern void curses_end_screen(void);
extern void curses_outrip(winid wid, int how);
extern void genl_outrip(winid tmpwin, int how);
extern void curses_preference_update(const char *pref);

/* curswins.c */

extern struct curswin *curswin_new(winid);
extern WINDOW *curswin_newuwin(struct curswin *);
extern void curswin_del(struct curswin *);
extern void curswin_deluwin(struct curswin *);
extern struct curswin *curswin_get(winid);
extern WINDOW *curswin_getuwin(winid);
extern void curswin_redraw(struct curswin *);
extern void curswin_resize(struct curswin *);
extern void curses_redraw2(void);
extern void curses_resize(void);
extern void curses_drawframe(WINDOW *, attr_t, xchar *);
extern void curses_addframe(WINDOW *, int, int, int, int, xchar *);
extern WINDOW *curses_create_window(int width, int height, orient orientation);
extern void curses_destroy_win(WINDOW *win);
extern void curses_set_nhwin(winid wid, WINDOW *win);
extern WINDOW *curses_get_nhwin(winid wid);
extern void curses_add_nhwin(winid wid, int height, int width, int y,
                             int x, orient orientation, boolean border);
extern void curses_add_wid(winid wid);
extern void curses_refresh_nhwin(winid wid);
extern void curses_refresh_nethack_windows(void);
extern void curses_del_nhwin(winid wid);
extern void curses_del_wid(winid wid);
extern void curses_putch(winid wid, int x, int y, int ch, int color, int attrs);
extern void curses_get_window_size(winid wid, int *height, int *width);
extern boolean curses_window_has_border(winid wid);
extern boolean curses_window_exists(winid wid);
extern int curses_get_window_orientation(winid wid);
extern void curses_get_window_xy(winid wid, int *x, int *y);
extern void curses_puts(winid wid, int attr, const char *text);
extern void curses_clear_nhwin(winid wid);
extern void curses_alert_win_border(winid wid, boolean onoff);
extern void curses_alert_main_borders(boolean onoff);
extern void curses_draw_map(int sx, int sy, int ex, int ey);
extern boolean curses_map_borders(int *sx, int *sy, int *ex, int *ey,
                                  int ux, int uy);

/* cursmisc.c */

extern void curses_redraw(void (*callback) (void *), void *arg);
extern int curses_getch(WINDOW *, void (*callback) (void *), void *arg);
extern int curses_read_char(void);
extern void curses_toggle_color_attr(WINDOW *win, int color, int attr,
                                     int onoff);
extern void curses_bail(const char *mesg);
extern winid curses_get_wid(int type);
extern char *curses_copy_of(const char *s);
extern int curses_num_lines(const char *str, int width);
extern char *curses_break_str(const char *str, int width, int line_num);
extern char *curses_str_remainder(const char *str, int width, int line_num);
extern boolean curses_is_menu(winid wid);
extern boolean curses_is_text(winid wid);
extern int curses_convert_glyph(int ch, int glyph);
extern void curses_move_cursor(winid wid, int x, int y);
extern void curses_prehousekeeping(void);
extern void curses_posthousekeeping(void);
extern void curses_view_file(const char *filename, boolean must_exist);
extern void curses_rtrim(char *str);
extern int curses_get_count(int first_digit);
extern int curses_convert_attr(int attr);
extern int curses_read_attrs(char *attrs);
extern int curses_convert_keys(int key);
extern int curses_get_mouse(int *mousex, int *mousey, int *mod);

/* cursdial.c */

extern void curses_line_input_dialog(const char *prompt, char *answer,
                                     int buffer, void (*callback) (void *),
                                     void *arg);
extern int curses_character_input_dialog(const char *prompt,
                                         const char *choices, CHAR_P def);
extern int curses_ext_cmd(void);
extern void curses_create_nhmenu(winid wid);
# ifdef MENU_COLOR
extern boolean get_menu_coloring(char *, int *, int *);
# endif
extern void curses_add_nhmenu_item(winid wid, int glyph,
                                   const ANY_P *identifier, CHAR_P accelerator,
                                   CHAR_P group_accel, int attr,
                                   const char *str, BOOLEAN_P presel);
extern void curses_finalize_nhmenu(winid wid, const char *prompt);
extern int curses_display_nhmenu(winid wid, int how, MENU_ITEM_P **_selected,
                                 boolean avoid_splash_overlap);
extern boolean curses_menu_exists(winid wid);
extern void curses_del_menu(winid wid);

/* cursstat.c */

extern attr_t curses_color_attr(int nh_color, int bg_color);
extern void curses_update_stats(void);
extern void curses_decrement_highlights(boolean);

/* cursinvt.c */

extern void curses_update_inv(void);
extern void curses_add_inv(int, int, CHAR_P, attr_t, const char *,
                           const ANY_P *);

/* cursinit.c */

extern void curses_create_main_windows(void);
extern void curses_init_nhcolors(void);
extern void curses_choose_character(void);
extern int curses_character_dialog(const char** choices, const char *prompt);
extern void curses_init_options(void);
extern int curses_display_splash_window(boolean count_only);
extern void curses_cleanup(void);

/* cursmesg.c */

extern void curses_message_win_puts(const char *message, boolean recursed);
extern int curses_block(boolean require_tab); /* for MSGTYPE=STOP */
extern int curses_more(void);
extern void curses_clear_unhighlight_message_window(void);
extern void curses_last_messages(void);
extern void curses_init_mesg_history(void);
extern void curses_prev_mesg(void);
extern void curses_count_window(const char *count_text);

#endif  /* WINCURS_H */
