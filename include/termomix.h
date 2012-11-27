#ifndef __TERMOMIX_H__
#define __TERMOMIX_H__

#define VERSION "0.1"
#define DATADIR "/usr/local/share"
#define BUILDTYPE ""

#define GETTEXT_PACKAGE "TERMomix"

#define PALETTE_SIZE 16

const GdkColor xterm_palette[PALETTE_SIZE] = {
    {0, 0x0000, 0x0000, 0x0000 },
    {0, 0xcdcb, 0x0000, 0x0000 },
    {0, 0x0000, 0xcdcb, 0x0000 },
    {0, 0xcdcb, 0xcdcb, 0x0000 },
    {0, 0x1e1a, 0x908f, 0xffff },
    {0, 0xcdcb, 0x0000, 0xcdcb },
    {0, 0x0000, 0xcdcb, 0xcdcb },
    {0, 0xe5e2, 0xe5e2, 0xe5e2 },
    {0, 0x4ccc, 0x4ccc, 0x4ccc },
    {0, 0xffff, 0x0000, 0x0000 },
    {0, 0x0000, 0xffff, 0x0000 },
    {0, 0xffff, 0xffff, 0x0000 },
    {0, 0x4645, 0x8281, 0xb4ae },
    {0, 0xffff, 0x0000, 0xffff },
    {0, 0x0000, 0xffff, 0xffff },
    {0, 0xffff, 0xffff, 0xffff }
};

#define CLOSE_BUTTON_CSS "* {\n"\
        "-GtkButton-default-border : 0;\n"\
        "-GtkButton-default-outside-border : 0;\n"\
        "-GtkButton-inner-border: 0;\n"\
        "-GtkWidget-focus-line-width : 0;\n"\
        "-GtkWidget-focus-padding : 0;\n"\
        "padding: 0;\n"\
        "}"

#define HIG_DIALOG_CSS "* {\n"\
        "-GtkDialog-action-area-border : 12;\n"\
        "-GtkDialog-button-spacing : 12;\n"\
        "}"

static struct {
    GtkWidget *main_window;
    GtkWidget *menu;
    GtkWidget *im_menu;
    struct terminal* term;
    PangoFontDescription *font;
    GdkColor forecolor;
    GdkColor backcolor;
    const GdkColor *palette;
    bool has_rgba;
    char *current_match;
    guint width;
    guint height;
    glong columns;
    glong rows;
    gint char_width;
    gint char_height;
    guint opacity_level;
    VteTerminalCursorShape cursor_type;
    bool config_modified;
    bool externally_modified;
    bool resized;
    GtkWidget *item_clear_background;
    GtkWidget *item_copy_link;
    GtkWidget *item_open_link;
    GtkWidget *open_link_separator;
    GKeyFile *cfg;
    GtkCssProvider *provider;
    char *configfile;
    char *background;
    char *word_chars;
    gint copy_accelerator;
    gint scrollbar_accelerator;
    gint open_url_accelerator;
    gint font_size_accelerator;
    gint copy_key;
    gint paste_key;
    gint scrollbar_key;
    gint set_tab_name_key;
    GRegex *http_regexp;
    char *argv[3];
} termomix;

struct terminal {
    GtkWidget *hbox;
    GtkWidget *vte;
    GPid pid;
    GtkBorder *border;
};
                

#define ICON_FILE "terminal-tango.svg"
#define SCROLL_LINES 16384
#define HTTP_REGEXP "(ftp|http)s?://[-a-zA-Z0-9.?$%&/=_~#.,:;+]*"
#define DEFAULT_CONFIGFILE "termomix.conf"
#define DEFAULT_COLUMNS 80
#define DEFAULT_ROWS 24
#define DEFAULT_FONT "Monospace 8"
#define FONT_MINIMAL_SIZE (PANGO_SCALE*6)
#define DEFAULT_WORD_CHARS  "-A-Za-z0-9,./?%&#_~"
#define DEFAULT_PALETTE "xterm"
#define FORWARD 1
#define BACKWARDS 2
#define DEFAULT_COPY_ACCELERATOR  (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_SCROLLBAR_ACCELERATOR  (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_OPEN_URL_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_FONT_SIZE_ACCELERATOR (GDK_CONTROL_MASK)
#define DEFAULT_COPY_KEY  GDK_KEY_C
#define DEFAULT_PASTE_KEY  GDK_KEY_V
#define DEFAULT_SCROLLBAR_KEY  GDK_KEY_S
#define ERROR_BUFFER_LENGTH 256
const char cfg_group[] = "termomix";

static GQuark term_data_id = 0;

#define  termomix_set_config_integer(key, value) do {\
        g_key_file_set_integer(termomix.cfg, cfg_group, key, value);\
        termomix.config_modified=TRUE;\
        } while(0);
            
#define  termomix_set_config_string(key, value) do {\
        g_key_file_set_value(termomix.cfg, cfg_group, key, value);\
        termomix.config_modified=TRUE;\
        } while(0);
                        
#define  termomix_set_config_boolean(key, value) do {\
        g_key_file_set_boolean(termomix.cfg, cfg_group, key, value);\
        termomix.config_modified=TRUE;\
        } while(0);
                                    

/* Callbacks */
static gboolean termomix_key_press (GtkWidget *, GdkEventKey *, gpointer);
static void     termomix_increase_font (GtkWidget *, void *);
static void     termomix_decrease_font (GtkWidget *, void *);
static void     termomix_child_exited (GtkWidget *, void *);
static void     termomix_eof (GtkWidget *, void *);
static gboolean termomix_delete_event (GtkWidget *, void *);
static void     termomix_destroy_window (GtkWidget *, void *);
static void     termomix_font_dialog (GtkWidget *, void *);
static void     termomix_color_dialog (GtkWidget *, void *);
static void     termomix_opacity_dialog (GtkWidget *, void *);
static void     termomix_set_title_dialog (GtkWidget *, void *);
static void     termomix_select_background_dialog (GtkWidget *, void *);
static void     termomix_open_url (GtkWidget *, void *);
static void     termomix_clear (GtkWidget *, void *);
static gboolean termomix_resized_window(GtkWidget *, GdkEventConfigure *, void *);
static void     termomix_setname_entry_changed(GtkWidget *, void *);
static void     termomix_copy(GtkWidget *, void *);
static void     termomix_paste(GtkWidget *, void *);
static void     termomix_conf_changed(GtkWidget *, void *);

/* Misc */
static void     termomix_error(const char *, ...);

/* Functions */
static void     termomix_init();
static void     termomix_init_popup();
static void     termomix_destroy();
static void     termomix_add_tab();
static void     termomix_set_font();
static void     termomix_set_size(gint, gint);
static void     termomix_set_bgimage();
static void     termomix_set_config_key(const gchar *, guint);
static guint    termomix_get_config_key(const gchar *);
static void     termomix_config_done();

static const char *option_font;
static const char *option_execute;
static gchar **option_xterm_args;
static gboolean option_xterm_execute=FALSE;
static gboolean option_version=FALSE;
static gint option_login = FALSE;
static const char *option_title;
static int option_rows, option_columns;
static gboolean option_hold=FALSE;
static const char *option_geometry;
static char *option_config_file;

static GOptionEntry entries[] = {
    { 
        "version",
        'v',
        0,
        G_OPTION_ARG_NONE,
        &option_version,
        "Print version number",
        NULL
    },
    {
        "font",
        'f',
        0,
        G_OPTION_ARG_STRING,
        &option_font,
        "Select initial terminal font",
        NULL
    },
    {
        "execute",
        'x',
        0,
        G_OPTION_ARG_STRING,
        &option_execute,
        "Execute command",
        NULL
    },
    {
        "xterm-execute",
        'e',
        0,
        G_OPTION_ARG_NONE,
        &option_xterm_execute,
        "Execute command (last option in the command line)",
        NULL
    },
    {
        G_OPTION_REMAINING,
        0,
        0,
        G_OPTION_ARG_STRING_ARRAY,
        &option_xterm_args,
        NULL,
        NULL
    },
    {
        "login",
        'l',
        0,
        G_OPTION_ARG_NONE,
        &option_login,
        "Login shell",
        NULL
    },
    {
        "title", 
        't',
        0,
        G_OPTION_ARG_STRING,
        &option_title,
        "Set window title",
        NULL
    },
    {
        "columns",
        'c',
        0,
        G_OPTION_ARG_INT,
        &option_columns,
        "Set columns number",
        NULL
    },
    {
        "rows",
        'r',
        0,
        G_OPTION_ARG_INT,
        &option_rows,
        "Set rows number",
        NULL
    },
    {
        "hold",
        'h',
        0,
        G_OPTION_ARG_NONE,
        &option_hold,
        "Hold window after execute command",
        NULL
    },
    {
        "geometry",
        0,
        0,
        G_OPTION_ARG_STRING,
        &option_geometry,
        "X geometry specification",
        NULL
    },
    {
        "config-file",
        0,
        0,
        G_OPTION_ARG_FILENAME,
        &option_config_file,
        "Use alternate configuration file",
        NULL
    },
    {
        NULL
    }
};

#endif /*__TERMOMIX_H__*/
