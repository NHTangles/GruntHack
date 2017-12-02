/* vim:set cin ft=c sw=4 sts=4 ts=8 et ai cino=Ls\:0t0(0 : -*- mode:c;fill-column:80;tab-width:8;c-basic-offset:4;indent-tabs-mode:nil;c-file-style:"k&r" -*-*/

#include "uncursed.h"
#include "hack.h"
#include "wincurs.h"
#include "cursdial.h"
#include "func_tab.h"
#include <ctype.h>

/* Dialog windows for curses interface */


/* Private declarations */

/* Used to group data together that getlin may need to update on a
   window resize. */
struct getlindata {
    WINDOW *win;
    const char *prompt;
    attr_t attr; /* attribute to use for the input line, usually underline */
    char *input;
    int pos;
    int x;
    int minx;
    int maxx;
    int posy;
};

typedef struct nhmi {
    winid wid;                  /* NetHack window id */
    int glyph;                  /* Menu glyphs */
    anything identifier;        /* Value returned if item selected - MUST BE obj if glyph is defined */
    CHAR_P accelerator;         /* Character used to select item from menu */
    CHAR_P group_accel;         /* Group accelerator for menu item, if any */
    int attr;                   /* Text attributes for item */
    const char *str;            /* Text of menu item */
    BOOLEAN_P presel;           /* Whether menu item should be preselected */
    boolean selected;           /* Whether item is currently selected */
    int page_num;               /* Display page number for entry */
    int line_num;               /* Line number on page where entry begins */
    int num_lines;              /* Number of lines entry uses on page */
    int count;                  /* Count for selected item */
    struct nhmi *prev_item;     /* Pointer to previous entry */
    struct nhmi *next_item;     /* Pointer to next entry */
} nhmenu_item;

typedef struct nhm {
    winid wid;                  /* NetHack window id */
    const char *prompt;         /* Menu prompt text */
    nhmenu_item *entries;       /* Menu entries */
    int num_entries;            /* Number of menu entries */
    int num_pages;              /* Number of display pages for entry */
    int height;                 /* Window height of menu */
    int width;                  /* Window width of menu */
    boolean reuse_accels;       /* Non-unique accelerators per page */
    struct nhm *prev_menu;      /* Pointer to previous entry */
    struct nhm *next_menu;      /* Pointer to next entry */
} nhmenu;

typedef enum menu_op_type {
    SELECT,
    DESELECT,
    INVERT
} menu_op;

#ifdef MENU_COLOR
extern struct menucoloring *menu_colorings;
#endif

static void reset_getlin(void *vgldat);
static void do_getlin(const char *prompt, char *answer, int buffer,
                      boolean extcmd);
static nhmenu *get_menu(winid wid);
static char menu_get_accel(boolean first);
static void menu_determine_pages(nhmenu *menu);
static boolean menu_is_multipage(nhmenu *menu, int width, int height);
static void menu_win_size(nhmenu *menu);
static void menu_display_page(nhmenu *menu, WINDOW * win, int page_num);
static int menu_get_selections(WINDOW * win, nhmenu *menu, int how);
static void menu_select_deselect(WINDOW * win, nhmenu_item *item,
                                 menu_op operation);
static int menu_operation(WINDOW * win, nhmenu *menu, menu_op operation,
                          int page_num);
static void menu_clear_selections(nhmenu *menu);
static int menu_max_height(void);

static nhmenu *nhmenus = NULL;  /* NetHack menu array */

/* Writes the input of a getlin buffer to its window. */
static void
print_getlin_input(struct getlindata *gldat)
{
    wmove(gldat->win, gldat->posy, gldat->minx);
    wattron(gldat->win, gldat->attr);

    /* Print all the visible input */
    int i;
    for (i = (gldat->pos - gldat->x);
         (i - gldat->pos + gldat->x + gldat->minx) < gldat->maxx; i++) {
        if (gldat->input[i])
            waddch(gldat->win, gldat->input[i]);
        else /* we're past the end of the string */
            waddch(gldat->win, ' ');
    }

    /* Done with outputting current input. Position the cursor to prepare
       for the next input. */
    wattroff(gldat->win, gldat->attr);
    wmove(gldat->win, gldat->posy, gldat->x + gldat->minx);
}

/* Performs window initialization, or reinitialization (if it was resized) */
static void
reset_getlin(void *vgldat)
{
    struct getlindata *gldat = vgldat;
    int height, width, winx, winy;
    WINDOW *bwin; /* will only exist briefly to paint a border. */
    WINDOW *msgwin = curses_get_nhwin(MESSAGE_WIN);
    int prompt_height = 1;
    int prompt_width = strlen(gldat->prompt);

    gldat->attr = A_UNDERLINE;

    if (iflags.window_inited && !iflags.wc_popup_dialog) {
        /* Using the message window. */
        curses_get_window_size(MESSAGE_WIN, &height, &width);
        if (!curses_window_has_border(MESSAGE_WIN))
            width--; /* We only care about the right, not the left, border. */

        pline("%s", gldat->prompt);
        waddch(msgwin, ' ');
        wrefresh(msgwin);
        getyx(msgwin, winy, winx);
        gldat->win = msgwin;
        gldat->attr = A_BOLD;
        if (height == 1)
            gldat->attr = 0;

        gldat->minx = winx;
        gldat->maxx = width;
        gldat->posy = winy;
    } else {
        /* Using a seperate window. */
        if (iflags.window_inited) /* height will not be used */
            curses_get_window_size(MAP_WIN, &height, &width);
        else
            width = term_cols;

        width -= 2; /* borders */
        if (prompt_width + 2 > width)
            prompt_height = curses_num_lines(gldat->prompt, width);

        height = prompt_height;
        height++; /* Fit the input line */

        /* Border window */
        bwin = curses_create_window(width, height,
                                    iflags.window_inited ? UP : CENTER);
        wrefresh(bwin);
        getbegyx(bwin, winy, winx);
        werase(bwin);
        delwin(bwin);

        /* create the input window (or move, if it already existed) */
        if (!gldat->win)
            gldat->win = newwin(height, width, winy + 1, winx + 1);
        else {
            werase(gldat->win);
            wresize(gldat->win, height, width);
            mvwin(gldat->win, winy + 1, winx + 1);
        }

        if (prompt_width + 2 > width) {
            int i;
            char *tmpstr;
            for (i = 0; i < prompt_height; i++) {
                tmpstr = curses_break_str(gldat->prompt, width, i + 1);
                mvwaddstr(gldat->win, i, 1, tmpstr);
                free(tmpstr);
            }
        } else
            mvwaddstr(gldat->win, 0, 1, gldat->prompt);

        gldat->minx = 1;
        gldat->maxx = (width - 1);
        gldat->posy = prompt_height;
    }

    while (gldat->x > (gldat->maxx - gldat->minx)) {
        gldat->x--;
        gldat->pos--;
    }

    print_getlin_input(gldat);
}

/* Get a line of text from the player, such as asking for a character name
   or a wish */
void
curses_line_input_dialog(const char *prompt, char *answer, int buffer)
{
    do_getlin(prompt, answer, buffer, FALSE);
}

/* Performs input handling of a line. If extcmd is TRUE, attempt to autocomplete
   into extended commands. */
static void
do_getlin(const char *prompt, char *answer, int buffer, boolean extcmd)
{
    int i;
    struct getlindata gldat = {0};
    gldat.input = calloc(buffer, sizeof (buffer));
    gldat.prompt = prompt;
    reset_getlin(&gldat);

    curs_set(1);
    int answer_ch;
    int buffer_cnt = 0; /* Length of current input (excludes NULL) */

    /* Extended command autocompletion: autocomplete if we have a single match
       only. */
    int extcmd_match = 0;
    int extcmd_match_total = 0;
    while (1) {
        /* First, print out what we have at the moment on the input line. */
        print_getlin_input(&gldat);

        /* Now we can do the actual poll for input and process it. */
        answer_ch = curses_getch(gldat.win, reset_getlin, &gldat);

        /* See if user is done, or is escaping first. */
        if (answer_ch == KEY_ESCAPE) {
            /* user escaped, so abort everything. */
            gldat.input[0] = '\0';
            break;
        } else if (answer_ch == '\r' || answer_ch == '\n' ||
                   answer_ch == '\0')
            break;

        switch (answer_ch) {
        case KEY_HOME:
            gldat.pos = 0;
            gldat.x = 0;
            break;
        case KEY_END:
            gldat.x += (buffer_cnt - gldat.pos);
            if (gldat.x > (gldat.maxx - gldat.minx))
                gldat.x = (gldat.maxx - gldat.minx);
            gldat.pos = buffer_cnt;
            break;
        case KEY_DC: /* Delete */
            if (gldat.pos == buffer_cnt)
                break;

            /* Delete is equal to right + backspace */
            gldat.pos++;
            gldat.x++;

            /* fallthrough */
        case KEY_BACKSPACE:
        case 8:
            /* Is this a valid action? cursor_pos==0 means we're
               at the beginning. */
            if (!gldat.pos)
                break;

            /* Remove a character, and potentially shift stuff
               after it back. */
            for (i = (gldat.pos - 1); i < buffer_cnt; i++)
                gldat.input[i] = gldat.input[i+1];

            buffer_cnt--;
            gldat.input[buffer_cnt] = '\0';
            /* fallthrough */
        case KEY_LEFT:
            if (gldat.pos) {
                gldat.pos--;
                if (gldat.x)
                    gldat.x--;
            }
            break;
        default:
            /* Character input is valid only for printable
               characters and if our buffer isn't filled.
               -1 since we also need to include the null
               terminator. */
            if (buffer_cnt == (buffer - 1) ||
                answer_ch >= 128 || answer_ch < ' ')
                break;

            /* Add the character at our input at the position
               we are at.  This might shift other characters. */
            for (i = buffer_cnt; i > gldat.pos; i--)
                gldat.input[i] = gldat.input[i-1];

            gldat.input[gldat.pos] = answer_ch;
            buffer_cnt++;

            /* If we are handling an extended command, maybe try to
               autocomplete it. */
            extcmd_match_total = 0;
            if (extcmd) {
                for (i = 0; extcmdlist[i].ef_txt; i++) {
                    if (!strncmpi(gldat.input, extcmdlist[i].ef_txt,
                                  gldat.pos + 1)) {
                        extcmd_match = i;
                        extcmd_match_total++;
                    }
                }

                /* Autocomplete if we have a single match */
                if (extcmd_match_total == 1) {
                    strcpy(gldat.input, extcmdlist[extcmd_match].ef_txt);
                    buffer_cnt = strlen(gldat.input);
                }
            }
            /* fallthrough */
        case KEY_RIGHT:
            if (gldat.pos < buffer_cnt) {
                gldat.pos++;
                if (gldat.x < (gldat.maxx - gldat.minx))
                    gldat.x++;
            }
            break;
        }
    }
    curs_set(0);
    strcpy(answer, gldat.input);
    free(gldat.input);
    if (!iflags.window_inited || gldat.win != curses_get_nhwin(MESSAGE_WIN))
        curses_destroy_win(gldat.win);
    else
        curses_clear_unhighlight_message_window();
}

/* Get a single character response from the player, such as a y/n prompt */
int
curses_character_input_dialog(const char *prompt, const char *choices,
                              CHAR_P def)
{
    WINDOW *askwin = NULL;
    int answer, count, maxwidth, map_height, map_width;
    char *linestr;
    char askstr[BUFSZ + QBUFSZ];
    char choicestr[QBUFSZ];
    int prompt_width = strlen(prompt);
    int prompt_height = 1;
    boolean any_choice = FALSE;
    boolean accept_count = FALSE;

    if (invent || (moves > 1)) {
        curses_get_window_size(MAP_WIN, &map_height, &map_width);
    } else {
        map_height = term_rows;
        map_width = term_cols;
    }

    maxwidth = map_width - 2;

    if (choices != NULL) {
        for (count = 0; choices[count] != '\0'; count++) {
            if (choices[count] == '#') { /* Accept a count */
                accept_count = TRUE;
            }
        }
        choicestr[0] = ' ';
        choicestr[1] = '[';
        for (count = 0; choices[count] != '\0'; count++) {
            if (choices[count] == DOESCAPE) { /* Escape */
                break;
            }
            choicestr[count + 2] = choices[count];
        }
        choicestr[count + 2] = ']';
        if (((def >= 'A') && (def <= 'Z')) || ((def >= 'a') && (def <= 'z'))) {
            choicestr[count + 3] = ' ';
            choicestr[count + 4] = '(';
            choicestr[count + 5] = def;
            choicestr[count + 6] = ')';
            choicestr[count + 7] = '\0';
        } else {                /* No usable default choice */

            choicestr[count + 3] = '\0';
            def = '\0';         /* Mark as no default */
        }
        strcpy(askstr, prompt);
        strcat(askstr, choicestr);
    } else {
        strcpy(askstr, prompt);
        any_choice = TRUE;
    }

    prompt_width = strlen(askstr);

    if ((prompt_width + 2) > maxwidth) {
        prompt_height = curses_num_lines(askstr, maxwidth);
        prompt_width = map_width - 2;
    }

    if (iflags.wc_popup_dialog || curses_stupid_hack) {
        askwin = curses_create_window(prompt_width, prompt_height, UP);
        for (count = 0; count < prompt_height; count++) {
            linestr = curses_break_str(askstr, maxwidth, count + 1);
            mvwaddstr(askwin, count + 1, 1, linestr);
            free(linestr);
        }

        wrefresh(askwin);
    } else {
        linestr = curses_copy_of(askstr);
        pline("%s", linestr);
        free(linestr);
        curs_set(1);
    }

    curses_stupid_hack = 0;

    while (1) {
        int res = timeout_get_wch(-1, &answer);

        if (res == ERR) {
            answer = def;
            break;
        }

        answer = curses_convert_keys(answer);

        if (answer == KEY_ESCAPE) {
            if (choices == NULL) {
                break;
            }
            answer = def;
            for (count = 0; choices[count] != '\0'; count++) {
                if (choices[count] == 'q') {    /* q is preferred over n */
                    answer = 'q';
                } else if ((choices[count] == 'n') && answer != 'q') {
                    answer = 'n';
                }
            }
            break;
        } else if ((answer == '\n') || (answer == '\r') || (answer == ' ')) {
            if ((choices != NULL) && (def != '\0')) {
                answer = def;
            }
            break;
        }

        if (digit(answer)) {
            if (accept_count) {
                if (answer != '0') {
                    yn_number = curses_get_count(answer - '0');
                    wrefresh(askwin);
                }

                answer = '#';
                break;
            }
        }

        if (any_choice) {
            break;
        }

        if (choices != NULL) {
            for (count = 0; count < strlen(choices); count++) {
                if (choices[count] == answer) {
                    break;
                }
            }
            if (choices[count] == answer) {
                break;
            }
        }
    }

    if (iflags.wc_popup_dialog) {
        /* Kludge to make prompt visible after window is dismissed
           when inputting a number */
        if (digit(answer)) {
            linestr = curses_copy_of(askstr);
            pline("%s", linestr);
            free(linestr);
            curs_set(1);
        }

        curses_destroy_win(askwin);
    } else {
        curses_clear_unhighlight_message_window();
        curs_set(0);
    }

    return answer;
}


/* Return an extended command from the user */

int
curses_ext_cmd()
{
    if (iflags.extmenu)
        return extcmd_via_menu();

    const char *prompt = "extended command: (? for help)";
    if (!iflags.wc_popup_dialog)
        prompt = "#"; /* Mimic the tty windowport here */

    char input[BUFSZ];
    do_getlin(prompt, input, BUFSZ, TRUE);
    if (!*input)
        return -1; /* escaped */

    int i;
    for (i = 0; extcmdlist[i].ef_txt; i++)
        if (!strcmpi(input, extcmdlist[i].ef_txt))
            return i;

    pline("Unknown extended command: %s", input);
    return -1;
}


/* Initialize a menu from given NetHack winid */

void
curses_create_nhmenu(winid wid)
{
    nhmenu *new_menu = NULL;
    nhmenu *menuptr = nhmenus;
    nhmenu_item *menu_item_ptr = NULL;
    nhmenu_item *tmp_menu_item = NULL;

    new_menu = get_menu(wid);

    if (new_menu != NULL) {
        /* Reuse existing menu, clearing out current entries */
        menu_item_ptr = new_menu->entries;

        if (menu_item_ptr != NULL) {
            while (menu_item_ptr->next_item != NULL) {
                tmp_menu_item = menu_item_ptr->next_item;
                free(menu_item_ptr);
                menu_item_ptr = tmp_menu_item;
            }
            free(menu_item_ptr);        /* Last entry */
            new_menu->entries = NULL;
        }
        if (new_menu->prompt != NULL) { /* Reusing existing menu */
            free((char *) new_menu->prompt);
        }
        return;
    }

    new_menu = malloc(sizeof (nhmenu));
    new_menu->wid = wid;
    new_menu->prompt = NULL;
    new_menu->entries = NULL;
    new_menu->num_pages = 0;
    new_menu->height = 0;
    new_menu->width = 0;
    new_menu->reuse_accels = FALSE;
    new_menu->next_menu = NULL;

    if (nhmenus == NULL) {      /* no menus in memory yet */
        new_menu->prev_menu = NULL;
        nhmenus = new_menu;
    } else {
        while (menuptr->next_menu != NULL) {
            menuptr = menuptr->next_menu;
        }
        new_menu->prev_menu = menuptr;
        menuptr->next_menu = new_menu;
    }
}


/* Add a menu item to the given menu window */

void
curses_add_nhmenu_item(winid wid, int glyph, const ANY_P * identifier,
                       CHAR_P accelerator, CHAR_P group_accel, int attr,
                       const char *str, BOOLEAN_P presel)
{
    char *new_str;
    nhmenu_item *new_item, *current_items, *menu_item_ptr;
    nhmenu *current_menu = get_menu(wid);

    if (str == NULL) {
        return;
    }

    new_str = curses_copy_of(str);
    curses_rtrim((char *) new_str);
    new_item = malloc(sizeof (nhmenu_item));
    new_item->wid = wid;
    new_item->glyph = glyph;
    new_item->identifier = *identifier;
    new_item->accelerator = accelerator;
    new_item->group_accel = group_accel;
    new_item->attr = attr;
    new_item->str = new_str;
    new_item->presel = presel;
    new_item->selected = FALSE;
    new_item->page_num = 0;
    new_item->line_num = 0;
    new_item->num_lines = 0;
    new_item->count = -1;
    new_item->next_item = NULL;

    if (current_menu == NULL) {
        panic
            ("curses_add_nhmenu_item: attempt to add item to nonexistant menu");
    }

    current_items = current_menu->entries;
    menu_item_ptr = current_items;

    if (current_items == NULL) {
        new_item->prev_item = NULL;
        current_menu->entries = new_item;
    } else {
        while (menu_item_ptr->next_item != NULL) {
            menu_item_ptr = menu_item_ptr->next_item;
        }
        new_item->prev_item = menu_item_ptr;
        menu_item_ptr->next_item = new_item;
    }
}


/* No more entries are to be added to menu, so details of the menu can be
 finalized in memory */

void
curses_finalize_nhmenu(winid wid, const char *prompt)
{
    int count = 0;
    nhmenu *current_menu = get_menu(wid);
    nhmenu_item *menu_item_ptr = current_menu->entries;

    if (current_menu == NULL) {
        panic("curses_finalize_nhmenu: attempt to finalize nonexistant menu");
    }

    while (menu_item_ptr != NULL) {
        menu_item_ptr = menu_item_ptr->next_item;
        count++;
    }

    current_menu->num_entries = count;

    current_menu->prompt = curses_copy_of(prompt);
}


/* Display a nethack menu, and return a selection, if applicable */

int
curses_display_nhmenu(winid wid, int how, MENU_ITEM_P ** _selected,
                      boolean avoid_splash_overlap)
{
    nhmenu *current_menu = get_menu(wid);
    nhmenu_item *menu_item_ptr;
    int num_chosen, count;
    WINDOW *win;
    MENU_ITEM_P *selected = NULL;

    *_selected = NULL;

    if (current_menu == NULL) {
        panic("curses_display_nhmenu: attempt to display nonexistant menu");
    }

    menu_item_ptr = current_menu->entries;

    if (menu_item_ptr == NULL) {
        panic("curses_display_nhmenu: attempt to display empty menu");
    }

    /* Reset items to unselected to clear out selections from previous
       invocations of this menu, and preselect appropriate items */
    while (menu_item_ptr != NULL) {
        menu_item_ptr->selected = menu_item_ptr->presel;
        menu_item_ptr = menu_item_ptr->next_item;
    }

    menu_win_size(current_menu);
    menu_determine_pages(current_menu);

    /* Display some pre-game menus below a potential splash screen */
    if (avoid_splash_overlap)
        win = curses_create_window(current_menu->width,
                                   current_menu->height, -1);
    /* Display (other) pre and post-game menus centered */
    else if (program_state.gameover)
        win = curses_create_window(current_menu->width,
                                   current_menu->height, CENTER);
    else /* Display during-game menus on the right out of the way */
        win = curses_create_window(current_menu->width,
                                   current_menu->height, RIGHT);

    num_chosen = menu_get_selections(win, current_menu, how);
    curses_destroy_win(win);

    if (num_chosen > 0) {
        selected = (MENU_ITEM_P *) malloc(num_chosen * sizeof (MENU_ITEM_P));
        count = 0;

        menu_item_ptr = current_menu->entries;

        while (menu_item_ptr != NULL) {
            if (menu_item_ptr->selected) {
                if (count == num_chosen) {
                    panic("curses_display_nhmenu: Selected items "
                          "exceeds expected number");
                }
                selected[count].item = menu_item_ptr->identifier;
                selected[count].count = menu_item_ptr->count;
                count++;
            }
            menu_item_ptr = menu_item_ptr->next_item;
        }

        if (count != num_chosen) {
            panic("curses_display_nhmenu: Selected items less than "
                  "expected number");
        }
    }

    *_selected = selected;

    return num_chosen;
}


boolean
curses_menu_exists(winid wid)
{
    if (get_menu(wid) != NULL) {
        return TRUE;
    } else {
        return FALSE;
    }
}

/* Delete the menu associated with the given NetHack winid from memory */

void
curses_del_menu(winid wid)
{
    nhmenu_item *tmp_menu_item;
    nhmenu_item *menu_item_ptr;
    nhmenu *tmpmenu;
    nhmenu *current_menu = get_menu(wid);

    if (current_menu == NULL) {
        return;
    }

    menu_item_ptr = current_menu->entries;

    /* First free entries associated with this menu from memory */
    if (menu_item_ptr != NULL) {
        while (menu_item_ptr->next_item != NULL) {
            tmp_menu_item = menu_item_ptr->next_item;
            free(menu_item_ptr);
            menu_item_ptr = tmp_menu_item;
        }
        free(menu_item_ptr);    /* Last entry */
        current_menu->entries = NULL;
    }

    /* Now unlink the menu from the list and free it as well */
    if (current_menu->prev_menu != NULL) {
        tmpmenu = current_menu->prev_menu;
        tmpmenu->next_menu = current_menu->next_menu;
    } else {
        nhmenus = current_menu->next_menu;      /* New head mode or NULL */
    }
    if (current_menu->next_menu != NULL) {
        tmpmenu = current_menu->next_menu;
        tmpmenu->prev_menu = current_menu->prev_menu;
    }

    free(current_menu);

    curses_del_wid(wid);
}


/* return a pointer to the menu associated with the given NetHack winid */

static nhmenu *
get_menu(winid wid)
{
    nhmenu *menuptr = nhmenus;

    while (menuptr != NULL) {
        if (menuptr->wid == wid) {
            return menuptr;
        }
        menuptr = menuptr->next_menu;
    }

    return NULL;                /* Not found */
}


static char
menu_get_accel(boolean first)
{
    char ret;
    static char next_letter = 'a';

    if (first) {
        next_letter = 'a';
    }

    ret = next_letter;

    if (((next_letter < 'z') && (next_letter >= 'a')) || ((next_letter < 'Z')
                                                          && (next_letter >=
                                                              'A')) ||
        ((next_letter < '9') && (next_letter >= '0'))) {
        next_letter++;
    } else if (next_letter == 'z') {
        next_letter = 'A';
    } else if (next_letter == 'Z') {
        next_letter = '0';
    }

    return ret;
}


/* Determine if menu will require multiple pages to display */

static boolean
menu_is_multipage(nhmenu *menu, int width, int height)
{
    int num_lines;
    int curline = 0;
    nhmenu_item *menu_item_ptr = menu->entries;

    if (strlen(menu->prompt) > 0) {
        curline += curses_num_lines(menu->prompt, width) + 1;
    }

    if (menu->num_entries <= (height - curline)) {
        while (menu_item_ptr != NULL) {
            menu_item_ptr->line_num = curline;
            if (menu_item_ptr->identifier.a_void == NULL) {
                num_lines = curses_num_lines(menu_item_ptr->str, width);
            } else {
                /* Add space for accelerator */
                num_lines = curses_num_lines(menu_item_ptr->str, width - 4);
            }
            menu_item_ptr->num_lines = num_lines;
            curline += num_lines;
            menu_item_ptr = menu_item_ptr->next_item;
            if ((curline > height) || ((curline > height - 2) &&
                                       (height == menu_max_height()))) {
                break;
            }
        }
        if (menu_item_ptr == NULL) {
            return FALSE;
        }
    }
    return TRUE;
}


/* Determine which entries go on which page, and total number of pages */

static void
menu_determine_pages(nhmenu *menu)
{
    int tmpline, num_lines;
    int curline = 0;
    int page_num = 1;
    nhmenu_item *menu_item_ptr = menu->entries;
    int width = menu->width;
    int height = menu->height;
    int page_end = height;


    if (strlen(menu->prompt) > 0) {
        curline += curses_num_lines(menu->prompt, width) + 1;
    }

    tmpline = curline;

    if (menu_is_multipage(menu, width, height)) {
        page_end -= 2;          /* Room to display current page number */
    }

    /* Determine what entries belong on which page */
    menu_item_ptr = menu->entries;

    while (menu_item_ptr != NULL) {
        menu_item_ptr->page_num = page_num;
        menu_item_ptr->line_num = curline;
        if (menu_item_ptr->identifier.a_void == NULL) {
            num_lines = curses_num_lines(menu_item_ptr->str, width);
        } else {
            /* Add space for accelerator */
            num_lines = curses_num_lines(menu_item_ptr->str, width - 4);
        }
        menu_item_ptr->num_lines = num_lines;
        curline += num_lines;
        if (curline > page_end) {
            page_num++;
            curline = tmpline;
            /* Move ptr back so entry will be reprocessed on new page */
            menu_item_ptr = menu_item_ptr->prev_item;
        }
        menu_item_ptr = menu_item_ptr->next_item;
    }

    menu->num_pages = page_num;
}


/* Determine dimensions of menu window based on term size and entries */

static void
menu_win_size(nhmenu *menu)
{
    int width, height, maxwidth, maxheight, curentrywidth, lastline;
    int maxentrywidth = strlen(menu->prompt);
    int maxheaderwidth = 0;
    nhmenu_item *menu_item_ptr = menu->entries;

    maxwidth = 38;              /* Reasonable minimum usable width */

    if ((term_cols / 2) > maxwidth) {
        maxwidth = (term_cols / 2);     /* Half the screen */
    }

    maxheight = menu_max_height();

    /* First, determine the width of the longest menu entry */
    while (menu_item_ptr != NULL)
    {
        if (menu_item_ptr->identifier.a_void == NULL) {
            curentrywidth = strlen(menu_item_ptr->str);

            if (curentrywidth > maxheaderwidth) {
                maxheaderwidth = curentrywidth;
            }
        } else {
            /* Add space for accelerator */
            curentrywidth=strlen(menu_item_ptr->str) + 4;
            if (menu_item_ptr->glyph != NO_GLYPH
                        && iflags.use_menu_glyphs)
                curentrywidth += 2;
        }
        if (curentrywidth > maxentrywidth) {
            maxentrywidth = curentrywidth;
        }
        menu_item_ptr = menu_item_ptr->next_item;
    }

    /* If the widest entry is smaller than maxwidth, reduce maxwidth accordingly */
    if (maxentrywidth < maxwidth) {
        maxwidth = maxentrywidth;
    }

    /* Try not to wrap headers/normal text lines if possible.  We can
       go wider than half the screen for this purpose if need be */

    if ((maxheaderwidth > maxwidth) && (maxheaderwidth < (term_cols - 2))) {
        maxwidth = maxheaderwidth;
    }

    width = maxwidth;

    /* Possibly reduce height if only 1 page */
    if (!menu_is_multipage(menu, maxwidth, maxheight)) {
        menu_item_ptr = menu->entries;

        while (menu_item_ptr->next_item != NULL) {
            menu_item_ptr = menu_item_ptr->next_item;
        }

        lastline = (menu_item_ptr->line_num) + menu_item_ptr->num_lines;

        if (lastline < maxheight) {
            maxheight = lastline;
        }
    } else {                    /* If multipage, make sure we have enough width for page footer */

        if (width < 20) {
            width = 20;
        }
    }

    height = maxheight;
    menu->width = width;
    menu->height = height;
}


/* Displays menu selections in the given window */

static void
menu_display_page(nhmenu *menu, WINDOW * win, int page_num)
{
    nhmenu_item *menu_item_ptr;
    int count, curletter, entry_cols, start_col, num_lines, footer_x;
    boolean first_accel = TRUE;

#ifdef MENU_COLOR
    int color = NO_COLOR;
    int attr = A_NORMAL;
    boolean menu_color = FALSE;
#endif /* MENU_COLOR */

    /* Cycle through entries until we are on the correct page */

    menu_item_ptr = menu->entries;

    while (menu_item_ptr != NULL) {
        if (menu_item_ptr->page_num == page_num) {
            break;
        }
        menu_item_ptr = menu_item_ptr->next_item;
    }

    if (menu_item_ptr == NULL) {        /* Page not found */
        panic("menu_display_page: attempt to display nonexistant page");
    }

    werase(win);

    if (strlen(menu->prompt) > 0) {
        num_lines = curses_num_lines(menu->prompt, menu->width);

        for (count = 0; count < num_lines; count++) {
            mvwprintw(win, count + 1, 1, "%s",
                      curses_break_str(menu->prompt, menu->width, count + 1));
        }
    }

    /* Display items for current page */

    while (menu_item_ptr != NULL) {
        if (menu_item_ptr->page_num != page_num) {
            break;
        }
        if (menu_item_ptr->identifier.a_void != NULL) {
            if (menu_item_ptr->accelerator != 0) {
                curletter = menu_item_ptr->accelerator;
            } else {
                if (first_accel) {
                    curletter = menu_get_accel(TRUE);
                    first_accel = FALSE;
                    if (!menu->reuse_accels && (menu->num_pages > 1)) {
                        menu->reuse_accels = TRUE;
                    }
                } else {
                    curletter = menu_get_accel(FALSE);
                }
                menu_item_ptr->accelerator = curletter;
            }

            if (menu_item_ptr->selected) {
                curses_toggle_color_attr(win, HIGHLIGHT_COLOR, A_REVERSE, ON);
                mvwaddch(win, menu_item_ptr->line_num + 1, 1, '<');
                mvwaddch(win, menu_item_ptr->line_num + 1, 2, curletter);
                mvwaddch(win, menu_item_ptr->line_num + 1, 3, '>');
                curses_toggle_color_attr(win, HIGHLIGHT_COLOR, A_REVERSE, OFF);
            } else {
                curses_toggle_color_attr(win, HIGHLIGHT_COLOR, NONE, ON);
                mvwaddch(win, menu_item_ptr->line_num + 1, 2, curletter);
                curses_toggle_color_attr(win, HIGHLIGHT_COLOR, NONE, OFF);
                mvwprintw(win, menu_item_ptr->line_num + 1, 3, ") ");
            }
        }
        entry_cols = menu->width;
        start_col = 1;

        if (menu_item_ptr->identifier.a_void != NULL) {
            entry_cols -= 4;
            start_col += 4;
        }        
        if (menu_item_ptr->glyph != NO_GLYPH && iflags.use_menu_glyphs) {
            unsigned special; /*notused */
            mapglyph_obj(menu_item_ptr->glyph, &curletter, &color, &special,
                                               u.ux, u.uy, menu_item_ptr->identifier.a_obj);
            curses_toggle_color_attr(win, color, NONE, ON);
            mvwaddch(win, menu_item_ptr->line_num + 1, start_col, curletter);
            curses_toggle_color_attr(win, color, NONE, OFF);
            mvwaddch(win, menu_item_ptr->line_num + 1, start_col + 1, ' ');
            entry_cols -= 2;
            start_col += 2;
        }
#ifdef MENU_COLOR
        if (iflags.use_menu_color && (menu_color = get_menu_coloring
                                      ((char *) menu_item_ptr->str, &color,
                                       &attr))) {
            if (color != NO_COLOR) {
                curses_toggle_color_attr(win, color, NONE, ON);
            }
            if (attr != A_NORMAL) {
                menu_item_ptr->attr = menu_item_ptr->attr | attr;
            }
        }
#endif /* MENU_COLOR */
        curses_toggle_color_attr(win, NONE, menu_item_ptr->attr, ON);

        num_lines = curses_num_lines(menu_item_ptr->str, entry_cols);

        for (count = 0; count < num_lines; count++) {
            if (strlen(menu_item_ptr->str) > 0) {
                mvwprintw(win, menu_item_ptr->line_num + count + 1,
                          start_col, "%s", curses_break_str(menu_item_ptr->str,
                                                            entry_cols,
                                                            count + 1));
            }
        }
#ifdef MENU_COLOR
        if (menu_color && (color != NO_COLOR)) {
            curses_toggle_color_attr(win, color, NONE, OFF);
        }
#endif /* MENU_COLOR */
        curses_toggle_color_attr(win, NONE, menu_item_ptr->attr, OFF);
        menu_item_ptr = menu_item_ptr->next_item;
    }

    if (menu->num_pages > 1) {
        footer_x = menu->width - strlen("<- (Page X of Y) ->");
        if (menu->num_pages > 9) {      /* Unlikely */
            footer_x -= 2;
        }
        mvwprintw(win, menu->height, footer_x + 3, "(Page %d of %d)",
                  page_num, menu->num_pages);
        if (page_num != 1) {
            curses_toggle_color_attr(win, HIGHLIGHT_COLOR, NONE, ON);
            mvwaddstr(win, menu->height, footer_x, "<=");
            curses_toggle_color_attr(win, HIGHLIGHT_COLOR, NONE, OFF);
        }
        if (page_num != menu->num_pages) {
            curses_toggle_color_attr(win, HIGHLIGHT_COLOR, NONE, ON);
            mvwaddstr(win, menu->height, menu->width - 2, "=>");
            curses_toggle_color_attr(win, HIGHLIGHT_COLOR, NONE, OFF);
        }
    }
    curses_toggle_color_attr(win, DIALOG_BORDER_COLOR, NONE, ON);
    box(win, 0, 0);
    curses_toggle_color_attr(win, DIALOG_BORDER_COLOR, NONE, OFF);
    wrefresh(win);
}


static int
menu_get_selections(WINDOW * win, nhmenu *menu, int how)
{
    int curletter;
    int count = -1;
    int count_letter = '\0';
    int curpage = 1;
    int num_selected = 0;
    boolean dismiss = FALSE;
    char search_key[BUFSZ];
    nhmenu_item *menu_item_ptr = menu->entries;

    menu_display_page(menu, win, 1);

    while (!dismiss) {
        curletter = wgetch(win);

        if (curletter == ERR) {
            num_selected = -1;
            dismiss = TRUE;
        }

        if (curletter == DOESCAPE) {
            curletter = curses_convert_keys(curletter);
        }

        switch (how) {
        case PICK_NONE:
            if (menu->num_pages == 1) {
                if (curletter == KEY_ESCAPE) {
                    num_selected = -1;
                } else {
                    num_selected = 0;

                }
                dismiss = TRUE;
                break;
            }
            break;
        case PICK_ANY:
            switch (curletter) {
            case MENU_SELECT_PAGE:
                (void) menu_operation(win, menu, SELECT, curpage);
                break;
            case MENU_SELECT_ALL:
                curpage = menu_operation(win, menu, SELECT, 0);
                break;
            case MENU_UNSELECT_PAGE:
                (void) menu_operation(win, menu, DESELECT, curpage);
                break;
            case MENU_UNSELECT_ALL:
                curpage = menu_operation(win, menu, DESELECT, 0);
                break;
            case MENU_INVERT_PAGE:
                (void) menu_operation(win, menu, INVERT, curpage);
                break;
            case MENU_INVERT_ALL:
                curpage = menu_operation(win, menu, INVERT, 0);
                break;
            }
        default:
            if (isdigit(curletter)) {
                count = curses_get_count(curletter - '0');
                wrefresh(win);
                curletter = wgetch(win);
                if (count > 0) {
                    count_letter = curletter;
                }
            }
        }

        switch (curletter) {
        case KEY_ESCAPE:
            num_selected = -1;
            dismiss = TRUE;
            break;
        case '\n':
        case '\r':
            dismiss = TRUE;
            break;
        case KEY_RIGHT:
        case KEY_NPAGE:
        case MENU_NEXT_PAGE:
        case ' ':
            if (curpage < menu->num_pages) {
                curpage++;
                menu_display_page(menu, win, curpage);
            } else if (curletter == ' ') {
                dismiss = TRUE;
                break;
            }
            break;
        case KEY_LEFT:
        case KEY_PPAGE:
        case MENU_PREVIOUS_PAGE:
            if (curpage > 1) {
                curpage--;
                menu_display_page(menu, win, curpage);
            }
            break;
        case KEY_END:
        case MENU_LAST_PAGE:
            if (curpage != menu->num_pages) {
                curpage = menu->num_pages;
                menu_display_page(menu, win, curpage);
            }
            break;
        case KEY_HOME:
        case MENU_FIRST_PAGE:
            if (curpage != 1) {
                curpage = 1;
                menu_display_page(menu, win, curpage);
            }
            break;
        case MENU_SEARCH:
            curses_line_input_dialog("Search for:", search_key, BUFSZ);

            wrefresh(win);

            if (strlen(search_key) == 0) {
                break;
            }

            menu_item_ptr = menu->entries;

            while (menu_item_ptr != NULL) {
                if ((menu_item_ptr->identifier.a_void != NULL) &&
                    (strstri(menu_item_ptr->str, search_key))) {
                    if (how == PICK_ONE) {
                        menu_clear_selections(menu);
                        menu_select_deselect(win, menu_item_ptr, SELECT);
                        num_selected = 1;
                        dismiss = TRUE;
                        break;
                    } else {
                        menu_select_deselect(win, menu_item_ptr, INVERT);
                    }
                }

                menu_item_ptr = menu_item_ptr->next_item;
            }

            menu_item_ptr = menu->entries;
            break;
        default:
            if (how == PICK_NONE) {
                num_selected = 0;
                dismiss = TRUE;
                break;
            }
        }

        menu_item_ptr = menu->entries;
        while (menu_item_ptr != NULL) {
            if (menu_item_ptr->identifier.a_void != NULL) {
                if (((curletter == menu_item_ptr->accelerator) &&
                     ((curpage == menu_item_ptr->page_num) ||
                      (!menu->reuse_accels))) || ((menu_item_ptr->group_accel)
                                                  && (curletter ==
                                                      menu_item_ptr->
                                                      group_accel))) {
                    if (curpage != menu_item_ptr->page_num) {
                        curpage = menu_item_ptr->page_num;
                        menu_display_page(menu, win, curpage);
                    }

                    if (how == PICK_ONE) {
                        menu_clear_selections(menu);
                        menu_select_deselect(win, menu_item_ptr, SELECT);
                        num_selected = 1;
                        dismiss = TRUE;
                        break;
                    } else if ((how == PICK_ANY) && (curletter == count_letter)) {
                        menu_select_deselect(win, menu_item_ptr, SELECT);
                        menu_item_ptr->count = count;
                        count = 0;
                        count_letter = '\0';
                    } else {
                        menu_select_deselect(win, menu_item_ptr, INVERT);
                    }
                }
            }
            menu_item_ptr = menu_item_ptr->next_item;
        }
    }

    if ((how == PICK_ANY) && (num_selected != -1)) {
        num_selected = 0;
        menu_item_ptr = menu->entries;

        while (menu_item_ptr != NULL) {
            if (menu_item_ptr->identifier.a_void != NULL) {
                if (menu_item_ptr->selected) {
                    num_selected++;
                }
            }
            menu_item_ptr = menu_item_ptr->next_item;
        }
    }

    return num_selected;
}


/* Select, deselect, or toggle selected for the given menu entry */

static void
menu_select_deselect(WINDOW * win, nhmenu_item *item, menu_op operation)
{
    int curletter = item->accelerator;

    if ((operation == DESELECT) || (item->selected && (operation == INVERT))) {
        item->selected = FALSE;
        mvwaddch(win, item->line_num + 1, 1, ' ');
        curses_toggle_color_attr(win, HIGHLIGHT_COLOR, NONE, ON);
        mvwaddch(win, item->line_num + 1, 2, curletter);
        curses_toggle_color_attr(win, HIGHLIGHT_COLOR, NONE, OFF);
        mvwaddch(win, item->line_num + 1, 3, ')');
    } else {
        item->selected = TRUE;
        curses_toggle_color_attr(win, HIGHLIGHT_COLOR, A_REVERSE, ON);
        mvwaddch(win, item->line_num + 1, 1, '<');
        mvwaddch(win, item->line_num + 1, 2, curletter);
        mvwaddch(win, item->line_num + 1, 3, '>');
        curses_toggle_color_attr(win, HIGHLIGHT_COLOR, A_REVERSE, OFF);
    }

    wrefresh(win);
}


/* Perform the selected operation (select, unselect, invert selection)
on the given menu page.  If menu_page is 0, then perform opetation on
all pages in menu.  Returns last page displayed.  */

static int
menu_operation(WINDOW * win, nhmenu *menu, menu_op
               operation, int page_num)
{
    int first_page, last_page, current_page;
    nhmenu_item *menu_item_ptr = menu->entries;

    if (page_num == 0) {        /* Operation to occur on all pages */
        first_page = 1;
        last_page = menu->num_pages;
    } else {
        first_page = page_num;
        last_page = page_num;
    }

    /* Cycle through entries until we are on the correct page */

    while (menu_item_ptr != NULL) {
        if (menu_item_ptr->page_num == first_page) {
            break;
        }
        menu_item_ptr = menu_item_ptr->next_item;
    }

    current_page = first_page;

    if (page_num == 0) {
        menu_display_page(menu, win, current_page);
    }

    if (menu_item_ptr == NULL) {        /* Page not found */
        panic("menu_display_page: attempt to display nonexistant page");
    }

    while (menu_item_ptr != NULL) {
        if (menu_item_ptr->page_num != current_page) {
            if (menu_item_ptr->page_num > last_page) {
                break;
            }

            current_page = menu_item_ptr->page_num;
            menu_display_page(menu, win, current_page);
        }

        if (menu_item_ptr->identifier.a_void != NULL) {
            menu_select_deselect(win, menu_item_ptr, operation);
        }

        menu_item_ptr = menu_item_ptr->next_item;
    }

    return current_page;
}


/* Set all menu items to unselected in menu */

static void
menu_clear_selections(nhmenu *menu)
{
    nhmenu_item *menu_item_ptr = menu->entries;

    while (menu_item_ptr != NULL) {
        menu_item_ptr->selected = FALSE;
        menu_item_ptr = menu_item_ptr->next_item;
    }
}


/* This is to get the color of a menu item if the menucolor patch is
 applied */

#ifdef MENU_COLOR
boolean
get_menu_coloring(char *str, int *color, int *attr)
{
    struct menucoloring *tmpmc;

    if (iflags.use_menu_color)
        for (tmpmc = menu_colorings; tmpmc; tmpmc = tmpmc->next)
# ifdef MENU_COLOR_REGEX
#  ifdef MENU_COLOR_REGEX_POSIX
            if (regexec(&tmpmc->match, str, 0, NULL, 0) == 0) {
#  else

            if (re_search(&tmpmc->match, str, strlen(str), 0, 9999, 0) >= 0) {
#  endif
# else
            if (pmatch(tmpmc->match, str)) {
# endif
                *color = tmpmc->color;
                *attr = curses_convert_attr(tmpmc->attr);
                return TRUE;
            }
    return FALSE;
}
#endif /* MENU_COLOR */


/* Get the maximum height for a menu */

static int
menu_max_height(void)
{
    return term_rows - 2;
}
