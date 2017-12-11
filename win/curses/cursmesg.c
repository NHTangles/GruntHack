/* vim:set cin ft=c sw=4 sts=4 ts=8 et ai cino=Ls\:0t0(0 : -*- mode:c;fill-column:80;tab-width:8;c-basic-offset:4;indent-tabs-mode:nil;c-file-style:"k&r" -*-*/

#include "uncursed.h"
#include "hack.h"
#include "wincurs.h"
#include "cursmesg.h"
#include <ctype.h>


/* Message window routines for curses interface */

/* Private declatations */

typedef struct nhpm {
    char *str;                  /* Message text */
    long turn;                  /* Turn number for message */
    struct nhpm *prev_mesg;     /* Pointer to previous message */
    struct nhpm *next_mesg;     /* Pointer to next message */
} nhprev_mesg;

static void scroll_window(winid wid);
static void unscroll_window(winid wid);
static void directional_scroll(winid wid, int nlines);
static void mesg_add_line(char *mline);
static nhprev_mesg *get_msg_line(boolean reverse, int mindex);

static int turn_lines = 1;
static int mx = 0;
static int my = 0;              /* message window text location */
static nhprev_mesg *first_mesg = NULL;
static nhprev_mesg *last_mesg = NULL;
static int max_messages;
static int num_messages = 0;



/* Write a string to the message window.  Attributes set by calling function. */

void
curses_message_win_puts(const char *message, boolean recursed)
{
    int height, width, linespace;
    char *tmpstr;
    WINDOW *win = curses_get_nhwin(MESSAGE_WIN);
    int message_length = strlen(message);
    static long suppress_turn = -1;

    if (strncmp("Count:", message, 6) == 0) {
        curses_count_window(message);
        return;
    }

    if (suppress_turn == moves) {
        return;
    }

    curses_get_window_size(MESSAGE_WIN, &height, &width);

    linespace = (width - 3) - mx;

    if (!recursed) {
        strcpy(toplines, message);
        mesg_add_line((char *) message);
    }

    if (linespace < message_length) {
        if (my >= (height - 1)) {        /* bottom of message win */
            if ((turn_lines > height) || (height == 1)) {
                /* Pause until key is hit - Esc suppresses any further
                   messages that turn */
                if (curses_more() == DOESCAPE) {
                    suppress_turn = moves;
                    return;
                }
            } else {
                scroll_window(MESSAGE_WIN);
                turn_lines++;
            }
        } else {
            if (mx) {
                my++;
                mx = 0;
            }
        }
    }

    if (height > 1) {
        curses_toggle_color_attr(win, NONE, A_BOLD, ON);
    }

    if (!mx && ((message_length + 2) > width)) {
        tmpstr = curses_break_str(message, (width - 2), 1);
        mvwprintw(win, my, mx, "%s", tmpstr);
        mx += strlen(tmpstr);
        if (strlen(tmpstr) < (width - 2)) {
            mx++;
        }
        free(tmpstr);
        if (height > 1) {
            curses_toggle_color_attr(win, NONE, A_BOLD, OFF);
        }
        curses_message_win_puts(tmpstr = curses_str_remainder(message, (width - 2), 1),
                                TRUE);
        free(tmpstr);
    } else {
        mvwprintw(win, my, mx, "%s", message);
        curses_toggle_color_attr(win, NONE, A_BOLD, OFF);
        mx += message_length + 1;
    }
    wrefresh(win);
}


int
curses_block(boolean require_tab)
{
    int height, width, ret;
    WINDOW *win = curses_get_nhwin(MESSAGE_WIN);

    curses_get_window_size(MESSAGE_WIN, &height, &width);
    curses_toggle_color_attr(win, MORECOLOR, NONE, ON);
    mvwprintw(win, my, mx, require_tab ? "<TAB!>" : ">>");
    curses_toggle_color_attr(win, MORECOLOR, NONE, OFF);
    if (require_tab)
        curses_alert_main_borders(TRUE);
    wrefresh(win);
    while ((ret = wgetch(win) != '\t') && require_tab);
    if (require_tab)
        curses_alert_main_borders(FALSE);
    if (height == 1) {
        curses_clear_unhighlight_message_window();
    } else {
        mvwprintw(win, my, mx, "      ");
        if (!require_tab) {
            scroll_window(MESSAGE_WIN);
            turn_lines = 1;
        }
    }

    return ret;
}

int
curses_more()
{
    return curses_block(FALSE);
}


/* Clear the message window if one line; otherwise unhighlight old messages */

void
curses_clear_unhighlight_message_window()
{
    int mh, mw, count;
    WINDOW *win = curses_get_nhwin(MESSAGE_WIN);

    turn_lines = 1;

    curses_get_window_size(MESSAGE_WIN, &mh, &mw);

    mx = 0;

    if (mh == 1) {
        curses_clear_nhwin(MESSAGE_WIN);
    } else {
        mx += mw;               /* Force new line on new turn */

        for (count = 0; count < mh; count++) {
            mvwchgat(win, count, 0, mw, COLOR_PAIR(8), A_NORMAL, NULL);
        }

        wnoutrefresh(win);
    }
}


/* Reset message window cursor to starting position, and display most
recent messages. */

void
curses_last_messages()
{
    mx = 0;
    my = 0;

    nhprev_mesg *mesg;
    int i;
    for (i = (num_messages - 1); i > 0; i--) {
        mesg = get_msg_line(TRUE, i);
        if (mesg && mesg->str && strcmp(mesg->str, ""))
            curses_message_win_puts(mesg->str, TRUE);
    }
    curses_message_win_puts(toplines, TRUE);
}


/* Initialize list for message history */

void
curses_init_mesg_history()
{
    max_messages = iflags.msg_history;

    if (max_messages < 1) {
        max_messages = 1;
    }

    if (max_messages > MESG_HISTORY_MAX) {
        max_messages = MESG_HISTORY_MAX;
    }
}


/* Display previous message window messages in reverse chron order */

void
curses_prev_mesg()
{
    int count;
    winid wid;
    long turn = 0;
    anything *identifier;
    nhprev_mesg *mesg;
    menu_item *selected = NULL;

    wid = curses_get_wid(NHW_MENU);
    curses_create_nhmenu(wid);
    identifier = malloc(sizeof (anything));
    identifier->a_void = NULL;

    for (count = 0; count < num_messages; count++) {
        mesg = get_msg_line(TRUE, count);
        if ((turn != mesg->turn) && (count != 0)) {
            curses_add_menu(wid, NO_GLYPH, identifier, 0, 0, A_NORMAL,
                            "---", FALSE);
        }
        curses_add_menu(wid, NO_GLYPH, identifier, 0, 0, A_NORMAL,
                        mesg->str, FALSE);
        turn = mesg->turn;
    }

    curses_end_menu(wid, "");
    curses_select_menu(wid, PICK_NONE, &selected);
}

/* Repositions the count window according to current size, if
   it's its seperate window (not if it's inline in the msgwin). */
void
curses_reset_count_window(void)
{
    WINDOW *win = curses_get_nhwin(COUNT_WIN);
    WINDOW *msgwin = curses_get_nhwin(MESSAGE_WIN);
    WINDOW *newwin;
    if (!win || win == msgwin)
        return;

    newwin = curses_create_window(25, 1, UP);
    overwrite(win, newwin);
    delwin(win);
    curses_set_nhwin(COUNT_WIN, newwin);
    curs_set(0);
    wrefresh(newwin);
}

/* Shows Count: in a separate window, or at the bottom of the message
   window, depending on the user's settings */
void
curses_count_window(const char *count_text)
{
    int startx, starty, winx, winy;
    int messageh, messagew;
    WINDOW *win = curses_get_nhwin(COUNT_WIN);
    WINDOW *msgwin = curses_get_nhwin(MESSAGE_WIN);

    if (win && win != msgwin) {
        curses_del_nhwin(COUNT_WIN);
        win = NULL;
    }

    if (!count_text) {
        if (win == msgwin) {
            curses_set_nhwin(COUNT_WIN, NULL);
            curses_clear_unhighlight_message_window();
        }

        return;
    }

    if (iflags.wc_popup_dialog) {
        startx = 1;
        starty = 1;

        if (win == NULL)
            win = curses_create_window(25, 1, UP);

        curses_set_nhwin(COUNT_WIN, win);
    } else {
        /* Display count at bottom of message window */
        if (!win)
            pline(" "); /* This goes unused. */
        curses_set_nhwin(COUNT_WIN, msgwin);
        win = msgwin;
        waddch(win, ' ');
        wrefresh(win);
        getyx(win, winy, winx);
        winx = 0;

        startx = winx;
        starty = winy;
    }

    if (win == msgwin)
        wattron(win, A_BOLD);
    mvwprintw(win, starty, startx, "%s", count_text);
    if (win == msgwin)
        wattroff(win, A_BOLD);
    wrefresh(win);
}


/* Scroll lines upward in given window, or clear window if only one line. */
static void
scroll_window(winid wid)
{
    directional_scroll(wid,1);
}

static void
unscroll_window(winid wid)
{
    directional_scroll(wid,-1);
}

static void
directional_scroll(winid wid, int nlines)
{
    int wh, ww, s_top, s_bottom;
    WINDOW *win = curses_get_nhwin(wid);

    curses_get_window_size(wid, &wh, &ww);
    if (wh == 1) {
        curses_clear_nhwin(wid);
        return;
    }
    s_top = 0;
    s_bottom = wh - 1;
    scrollok(win, TRUE);
    wscrl(win, nlines);
    scrollok(win, FALSE);
    if (wid == MESSAGE_WIN)
        mx = 0;

    wrefresh(win);
}

/* Add given line to message history */
static void
mesg_add_line(char *mline)
{
    nhprev_mesg *tmp_mesg = NULL;
    nhprev_mesg *current_mesg = malloc(sizeof (nhprev_mesg));

    current_mesg->str = curses_copy_of(mline);
    current_mesg->turn = moves;
    current_mesg->next_mesg = NULL;

    if (num_messages == 0) {
        first_mesg = current_mesg;
    }

    if (last_mesg != NULL) {
        last_mesg->next_mesg = current_mesg;
    }
    current_mesg->prev_mesg = last_mesg;
    last_mesg = current_mesg;


    if (num_messages < max_messages) {
        num_messages++;
    } else {
        tmp_mesg = first_mesg->next_mesg;
        if (last_mesg == first_mesg)
            last_mesg = NULL;
        free(first_mesg);
        first_mesg = tmp_mesg;
    }
}


/* Returns specified line from message history, or NULL if out of bounds */

static nhprev_mesg *
get_msg_line(boolean reverse, int mindex)
{
    int count;
    nhprev_mesg *current_mesg;

    if (reverse) {
        current_mesg = last_mesg;
        for (count = 0; count < mindex; count++) {
            if (current_mesg == NULL) {
                return NULL;
            }
            current_mesg = current_mesg->prev_mesg;
        }
        return current_mesg;
    } else {
        current_mesg = first_mesg;
        for (count = 0; count < mindex; count++) {
            if (current_mesg == NULL) {
                return NULL;
            }
            current_mesg = current_mesg->next_mesg;
        }
        return current_mesg;
    }
}
