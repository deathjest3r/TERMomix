/*******************************************************************************
 *  Filename: termomix.c
 *  Description: VTE-based terminal emulator
 *               (Lots of code is taken from Sakura terminal application)
 *
 *           Copyright (C) 2012       Julian Vetter <death.jester@web.de>
 *           Copyright (C) 2006-2012  David Gómez <david@pleyades.net>
 *           Copyright (C) 2008       Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *****************************************************************************/

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <wchar.h>
#include <math.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <locale.h>
#include <libintl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "../include/termomix.h"


static gboolean termomix_key_press(GtkWidget *widget, GdkEventKey *event,
        gpointer user_data) {
    if (event->type!=GDK_KEY_PRESS) return FALSE;

    /* Check is Caps lock is enabled. If it is, change keyval to make
     * keybindings work with both lowercase and uppercase letters
     */
    if (gdk_keymap_get_caps_lock_state(gdk_keymap_get_default())) {
        event->keyval=gdk_keyval_to_upper(event->keyval);
    }

    /* copy_accelerator-[C/V] pressed */
    if ( (event->state & termomix.copy_accelerator) ==
            termomix.copy_accelerator ) {
        if (event->keyval==termomix.copy_key) {
            termomix_copy(NULL, NULL);
            return TRUE;
        } else if (event->keyval==termomix.paste_key) {
            termomix_paste(NULL, NULL);
            return TRUE;
        }
    }

    /* font_size_accelerator-[+] or [-] pressed */
    if ( (event->state & termomix.font_size_accelerator) ==
            termomix.font_size_accelerator ) {
        if (event->keyval==GDK_KEY_plus) {
            termomix_increase_font(NULL, NULL);
            return TRUE;
        } else if (event->keyval==GDK_KEY_minus) {
            termomix_decrease_font(NULL, NULL);
            return TRUE;
        }
    }
    return FALSE;
}


static gboolean termomix_button_press(GtkWidget *widget,
        GdkEventButton *button_event, gpointer user_data) {
    glong column, row;
    gint tag;

    if (button_event->type != GDK_BUTTON_PRESS)
        return FALSE;


    /* Get the column and row relative to pointer position */
    column = ((glong) (button_event->x) / vte_terminal_get_char_width(
            VTE_TERMINAL(termomix.term->vte)));
    row = ((glong) (button_event->y) / vte_terminal_get_char_height(
            VTE_TERMINAL(termomix.term->vte)));
    termomix.current_match = vte_terminal_match_check(
            VTE_TERMINAL(termomix.term->vte), column, row, &tag);

    /* Left button: open the URL if any */
    if (button_event->button == 1 &&
            ((button_event->state & termomix.open_url_accelerator) ==
            termomix.open_url_accelerator) && termomix.current_match) {
        termomix_open_url(NULL, NULL);
        return TRUE;
    }

    /* Right button: show the popup menu */
    if (button_event->button == 3) {
        GtkMenu *menu;
        menu = GTK_MENU (widget);

        if (termomix.current_match) {
            /* Show the extra options in the menu */
            gtk_widget_show(termomix.item_open_link);
            gtk_widget_show(termomix.item_copy_link);
            gtk_widget_show(termomix.open_link_separator);
        } else {
            /* Hide all the options */
            gtk_widget_hide(termomix.item_open_link);
            gtk_widget_hide(termomix.item_copy_link);
            gtk_widget_hide(termomix.open_link_separator);
        }

        gtk_menu_popup (menu, NULL, NULL, NULL, NULL, button_event->button,
                button_event->time);

        return TRUE;
    }
    return FALSE;
}


static void termomix_increase_font(GtkWidget *widget, void *data) {
    gint new_size;

    /* Increment font size one unit */
    new_size = pango_font_description_get_size(termomix.font) + PANGO_SCALE;

    pango_font_description_set_size(termomix.font, new_size);
    termomix_set_font();
    termomix_set_size(termomix.columns, termomix.rows);
    termomix_set_config_string("font",
            pango_font_description_to_string(termomix.font));
}


static void termomix_decrease_font(GtkWidget *widget, void *data) {
    gint new_size;

    /* Decrement font size one unit */
    new_size=pango_font_description_get_size(termomix.font)-PANGO_SCALE;
    
    /* Set a minimal size */
    if (new_size >= FONT_MINIMAL_SIZE ) {
        pango_font_description_set_size(termomix.font, new_size);
        termomix_set_font();
        termomix_set_size(termomix.columns, termomix.rows);    
        termomix_set_config_string("font", pango_font_description_to_string(termomix.font));
    }
}


static void termomix_child_exited(GtkWidget *widget, void *data) {
    gint status;

    termomix_config_done();

    if (option_hold==TRUE) {
        return;
    }

    waitpid(termomix.term->pid, &status, WNOHANG);
    termomix_destroy();
}


static void termomix_eof(GtkWidget *widget, void *data) {
    gint status;

    termomix_config_done();

    if (option_hold==TRUE) {
        return;
    }

    waitpid(termomix.term->pid, &status, WNOHANG);

    termomix_destroy();
}

/* Save configuration */
static void termomix_config_done() {
    GError *gerror = NULL;
    gsize len = 0;

    gchar *cfgdata = g_key_file_to_data(termomix.cfg, &len, &gerror);
    if (!cfgdata) {
        fprintf(stderr, "%s\n", gerror->message);
        exit(EXIT_FAILURE);
    }
    /* Write to file IF there's been changes */
    if (termomix.config_modified) {

        GIOChannel *cfgfile = g_io_channel_new_file(termomix.configfile, "w",
                &gerror);
        
        if (!cfgfile) {
            fprintf(stderr, "%s\n", gerror->message);
            exit(EXIT_FAILURE);
        }

        /* FIXME: if the number of chars written is not "len", something
         * happened. Check for errors appropriately...
         */
        GIOStatus status = g_io_channel_write_chars(cfgfile, cfgdata, len,
                NULL, &gerror);
        
        if (status != G_IO_STATUS_NORMAL) {
            // FIXME: we should deal with temporary failures (G_IO_STATUS_AGAIN)
            fprintf(stderr, "%s\n", gerror->message);
            exit(EXIT_FAILURE);
        }
        g_io_channel_shutdown(cfgfile, TRUE, &gerror);
        g_io_channel_unref(cfgfile);
    }
}


static gboolean
termomix_delete_event (GtkWidget *widget, void *data)
{
    termomix_config_done();
    return FALSE;
}


static void
termomix_destroy_window (GtkWidget *widget, void *data)
{
    termomix_destroy();
}


static void
termomix_font_dialog (GtkWidget *widget, void *data)
{
    GtkWidget *font_dialog;
    gint response;

    font_dialog=gtk_font_chooser_dialog_new(gettext("Select font"), GTK_WINDOW(termomix.main_window));
    gtk_font_chooser_set_font_desc(GTK_FONT_CHOOSER(font_dialog), termomix.font);

    response=gtk_dialog_run(GTK_DIALOG(font_dialog));

    if (response==GTK_RESPONSE_OK) {
        pango_font_description_free(termomix.font);
        termomix.font=gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(font_dialog));
        termomix_set_font();
        termomix_set_size(termomix.columns, termomix.rows);
        termomix_set_config_string("font", pango_font_description_to_string(termomix.font));
    }

    gtk_widget_destroy(font_dialog);
}


static void
termomix_color_dialog (GtkWidget *widget, void *data)
{
    GtkWidget *color_dialog;
    GtkWidget *label1, *label2;
    GtkWidget *buttonfore, *buttonback;
    GtkWidget *hbox_fore, *hbox_back;
    gint response;
    guint16 backalpha;

    color_dialog=gtk_dialog_new_with_buttons(gettext("Select color"),
            GTK_WINDOW(termomix.main_window), GTK_DIALOG_MODAL, GTK_STOCK_CANCEL,
            GTK_RESPONSE_REJECT, GTK_STOCK_APPLY, GTK_RESPONSE_ACCEPT, NULL);

    gtk_dialog_set_default_response(GTK_DIALOG(color_dialog), GTK_RESPONSE_ACCEPT);
    gtk_window_set_modal(GTK_WINDOW(color_dialog), TRUE);
    /* Set style */
    gchar *css = g_strdup_printf (HIG_DIALOG_CSS);
    gtk_css_provider_load_from_data(termomix.provider, css, -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context (color_dialog);
    gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (termomix.provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_free(css);

    hbox_fore=gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    hbox_back=gtk_box_new(FALSE, 12);
    label1=gtk_label_new(gettext("Select foreground color:"));
    label2=gtk_label_new(gettext("Select background color:"));
    buttonfore=gtk_color_button_new_with_color(&termomix.forecolor);
    buttonback=gtk_color_button_new_with_color(&termomix.backcolor);
    /* When the times comes (gtk-3.4) */
    // buttonfore=gtk_color_button_new_with_rgba(&termomix.forecolor);
    // buttonback=gtk_color_button_new_with_rgba(&termomix.backcolor);*/

    /* This rounding sucks...*/
    backalpha = roundf((termomix.opacity_level*65535)/99);
    if (termomix.has_rgba) {
        gtk_color_button_set_use_alpha(GTK_COLOR_BUTTON(buttonback), TRUE);
        gtk_color_button_set_alpha(GTK_COLOR_BUTTON(buttonback), backalpha);
    }

    gtk_box_pack_start(GTK_BOX(hbox_fore), label1, FALSE, FALSE, 12);
    gtk_box_pack_end(GTK_BOX(hbox_fore), buttonfore, FALSE, FALSE, 12);
    gtk_box_pack_start(GTK_BOX(hbox_back), label2, FALSE, FALSE, 12);
    gtk_box_pack_end(GTK_BOX(hbox_back), buttonback, FALSE, FALSE, 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))), hbox_fore, FALSE, FALSE, 6);
    gtk_box_pack_end(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))), hbox_back, FALSE, FALSE, 6);

    gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog)));

    response=gtk_dialog_run(GTK_DIALOG(color_dialog));

    if (response==GTK_RESPONSE_ACCEPT) {
        /* TODO: Remove deprecated get_color */
        //gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(color_dialog), &termomix.forecolor);
        //gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(color_dialog), &termomix.backcolor);
        gtk_color_button_get_color(GTK_COLOR_BUTTON(buttonfore), &termomix.forecolor);
        gtk_color_button_get_color(GTK_COLOR_BUTTON(buttonback), &termomix.backcolor);

        if (termomix.has_rgba) {
            backalpha = gtk_color_button_get_alpha(GTK_COLOR_BUTTON(buttonback));
        }

        if (termomix.has_rgba) {
            vte_terminal_set_opacity(VTE_TERMINAL (termomix.term->vte), backalpha);
        }
        vte_terminal_set_colors(VTE_TERMINAL(termomix.term->vte), &termomix.forecolor,
                &termomix.backcolor, termomix.palette, PALETTE_SIZE);

        gchar *cfgtmp;
        cfgtmp = g_strdup_printf("#%02x%02x%02x", termomix.forecolor.red >>8,
                                 termomix.forecolor.green>>8, termomix.forecolor.blue>>8);
        termomix_set_config_string("forecolor", cfgtmp);
        g_free(cfgtmp);

        cfgtmp = g_strdup_printf("#%02x%02x%02x", termomix.backcolor.red >>8,
                                 termomix.backcolor.green>>8, termomix.backcolor.blue>>8);
        termomix_set_config_string("backcolor", cfgtmp);
        g_free(cfgtmp);

        termomix.opacity_level= roundf((backalpha*99)/65535);     /* Opacity value is between 0 and 99 */
        termomix_set_config_integer("opacity_level", termomix.opacity_level);  

    }

    gtk_widget_destroy(color_dialog);
}


static void
termomix_opacity_dialog (GtkWidget *widget, void *data)
{
    GtkWidget *opacity_dialog, *spin_control, *spin_label;//, *check;
    GtkAdjustment *spinner_adj;
    GtkWidget *dialog_hbox, *dialog_vbox, *dialog_spin_hbox;
    gint response;
    guint16 backalpha;

    opacity_dialog=gtk_dialog_new_with_buttons(gettext("Opacity"),
            GTK_WINDOW(termomix.main_window), GTK_DIALOG_MODAL, GTK_STOCK_CANCEL,
            GTK_RESPONSE_REJECT, GTK_STOCK_APPLY, GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(opacity_dialog), GTK_RESPONSE_ACCEPT);
    gtk_window_set_modal(GTK_WINDOW(opacity_dialog), TRUE);

    /* Set style */
    gchar *css = g_strdup_printf (HIG_DIALOG_CSS);
    gtk_css_provider_load_from_data(termomix.provider, css, -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context (opacity_dialog);
    gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (termomix.provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_free(css);

    spinner_adj = gtk_adjustment_new ((termomix.opacity_level), 0.0, 99.0, 1.0, 5.0, 0);
    spin_control = gtk_spin_button_new(GTK_ADJUSTMENT(spinner_adj), 1.0, 0);

    spin_label = gtk_label_new(gettext("Opacity level (%):"));
    dialog_hbox=gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    dialog_vbox=gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    dialog_spin_hbox=gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(opacity_dialog))), dialog_hbox, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(dialog_hbox), dialog_vbox, FALSE, FALSE, 12);
    gtk_box_pack_start(GTK_BOX(dialog_spin_hbox), spin_label, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(dialog_spin_hbox), spin_control, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(dialog_vbox), dialog_spin_hbox, TRUE, TRUE, 6);

    gtk_widget_show_all(dialog_hbox);

    response=gtk_dialog_run(GTK_DIALOG(opacity_dialog));

    if (response==GTK_RESPONSE_ACCEPT) {

        termomix.opacity_level = gtk_spin_button_get_value_as_int((GtkSpinButton *) spin_control);
        
        /* Map opacity level to alpha */
        backalpha = (termomix.opacity_level*65535)/100;

        /* Set transparency for all tabs */
        GdkColor white={0, 255, 255, 255};
        if (termomix.has_rgba) {
            /* This is needed for set_opacity to have effect */
            vte_terminal_set_color_background(VTE_TERMINAL (termomix.term->vte), &white);
            vte_terminal_set_opacity(VTE_TERMINAL (termomix.term->vte), backalpha);
            /* Reset colors again because we had set a white background.
             * TODO: Check if it's still needed with set_colors_rgba
             */
             vte_terminal_set_colors(VTE_TERMINAL(termomix.term->vte),
                    &termomix.forecolor, &termomix.backcolor, termomix.palette,
                    PALETTE_SIZE);
        }

        termomix_set_config_integer("opacity_level", termomix.opacity_level);
    }

    gtk_widget_destroy(opacity_dialog);
}


static void
termomix_set_title_dialog (GtkWidget *widget, void *data)
{
    GtkWidget *title_dialog;
    GtkWidget *entry, *label;
    GtkWidget *title_hbox;
    gint response;

    title_dialog=gtk_dialog_new_with_buttons(gettext("Set window title"), GTK_WINDOW(termomix.main_window), GTK_DIALOG_MODAL,
                                             GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                             GTK_STOCK_APPLY, GTK_RESPONSE_ACCEPT, NULL);

    gtk_dialog_set_default_response(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT);
    gtk_window_set_modal(GTK_WINDOW(title_dialog), TRUE);
    /* Set style */
    gchar *css = g_strdup_printf (HIG_DIALOG_CSS);
    gtk_css_provider_load_from_data(termomix.provider, css, -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context (title_dialog);
    gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (termomix.provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_free(css);

    entry=gtk_entry_new();
    label=gtk_label_new(gettext("New window title"));
    title_hbox=gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    /* Set window label as entry default text */
    gtk_entry_set_text(GTK_ENTRY(entry), gtk_window_get_title(GTK_WINDOW(termomix.main_window)));
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(title_hbox), label, TRUE, TRUE, 12);
    gtk_box_pack_start(GTK_BOX(title_hbox), entry, TRUE, TRUE, 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(title_dialog))), title_hbox, FALSE, FALSE, 12);
    /* Disable accept button until some text is entered */
    g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(termomix_setname_entry_changed), title_dialog);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT, FALSE);

    gtk_widget_show_all(title_hbox);

    response=gtk_dialog_run(GTK_DIALOG(title_dialog));
    if (response==GTK_RESPONSE_ACCEPT) {
        /* Bug #257391 shadow reachs here too... */
        gtk_window_set_title(GTK_WINDOW(termomix.main_window), gtk_entry_get_text(GTK_ENTRY(entry)));
    }
    gtk_widget_destroy(title_dialog);

}


static void
termomix_select_background_dialog (GtkWidget *widget, void *data)
{
    GtkWidget *dialog;
    gint response;
    gchar *filename;

    dialog = gtk_file_chooser_dialog_new (gettext("Select a background file"), GTK_WINDOW(termomix.main_window),
                                                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                                         GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                                                         NULL);


    response=gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
        termomix.background=g_strdup(filename);
        termomix_set_bgimage(termomix.background);
        gtk_widget_show(termomix.item_clear_background);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}


static void
termomix_copy_url (GtkWidget *widget, void *data)
{
    GtkClipboard* clip;

    clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clip, termomix.current_match, -1 );
    clip = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    gtk_clipboard_set_text(clip, termomix.current_match, -1 );

}


static void
termomix_open_url (GtkWidget *widget, void *data)
{
    GError *error=NULL;
    gchar *cmd;
    gchar *browser=NULL;

    browser=(gchar *)g_getenv("BROWSER");

    if (browser) {
        cmd=g_strdup_printf("%s %s", browser, termomix.current_match);
    } else {
        if ( (browser = g_find_program_in_path("xdg-open")) ) {
            cmd=g_strdup_printf("%s %s", browser, termomix.current_match);
            g_free(browser);
        } else
            cmd=g_strdup_printf("firefox %s", termomix.current_match);
    }

    if (!g_spawn_command_line_async(cmd, &error)) {
        termomix_error("Couldn't exec \"%s\": %s", cmd, error->message);
    }

    g_free(cmd);
}


static void
termomix_clear (GtkWidget *widget, void *data)
{
    gtk_widget_hide(termomix.item_clear_background);

    vte_terminal_set_background_image(VTE_TERMINAL(termomix.term->vte), NULL);

    termomix_set_config_string("background", "none");

    g_free(termomix.background);
    termomix.background=NULL;
}


static void termomix_set_cursor(GtkWidget *widget, void *data) {

    char *cursor_string = (char *)data;

    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {

        if (strcmp(cursor_string, "block")==0) {
            termomix.cursor_type=VTE_CURSOR_SHAPE_BLOCK;
        } else if (strcmp(cursor_string, "underline")==0) {
            termomix.cursor_type=VTE_CURSOR_SHAPE_UNDERLINE;
        } else if (strcmp(cursor_string, "ibeam")==0) {
            termomix.cursor_type=VTE_CURSOR_SHAPE_IBEAM;
        } 

        vte_terminal_set_cursor_shape(VTE_TERMINAL(termomix.term->vte),
                termomix.cursor_type);
        termomix_set_config_integer("cursor_type", termomix.cursor_type);
    }
}

static gboolean termomix_resized_window (GtkWidget *widget,
        GdkEventConfigure *event, void *data) {
    if (event->width!=termomix.width || event->height!=termomix.height) {
        termomix.resized=TRUE;
    }
        
    return FALSE;
}

static void termomix_setname_entry_changed (GtkWidget *widget, void *data) {
    GtkDialog *title_dialog=(GtkDialog *)data;

    if (strcmp(gtk_entry_get_text(GTK_ENTRY(widget)), "")==0) {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(title_dialog),
                GTK_RESPONSE_ACCEPT, FALSE);
    } else {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(title_dialog),
                GTK_RESPONSE_ACCEPT, TRUE);
    }
}


/* Parameters are never used */
static void termomix_copy (GtkWidget *widget, void *data) {
    vte_terminal_copy_clipboard(VTE_TERMINAL(termomix.term->vte));
}


/* Parameters are never used */
static void termomix_paste (GtkWidget *widget, void *data) {
    vte_terminal_paste_clipboard(VTE_TERMINAL(termomix.term->vte));
}


/* Callback called when termomix configuration file is modified by an external process */
static void termomix_conf_changed (GtkWidget *widget, void *data) {
    termomix.externally_modified=true;
}

/******* Functions ********/

static void termomix_init() {
    GError *gerror=NULL;
    char* configdir = NULL;

    term_data_id = g_quark_from_static_string("termomix_term");

    g_setenv("TERM", "xterm", FALSE);

    /* Config file initialization*/
    termomix.cfg = g_key_file_new();
    termomix.config_modified=false;

    configdir = g_build_filename( g_get_user_config_dir(), "termomix", NULL );
    if( ! g_file_test( g_get_user_config_dir(), G_FILE_TEST_EXISTS) )
        g_mkdir( g_get_user_config_dir(), 0755 );
    if( ! g_file_test( configdir, G_FILE_TEST_EXISTS) )
        g_mkdir( configdir, 0755 );
    if (option_config_file) {
        termomix.configfile=g_build_filename(configdir, option_config_file, NULL);
    } else {
        /* Use more standard-conforming path for config files, if available. */
        termomix.configfile=g_build_filename(configdir, DEFAULT_CONFIGFILE, NULL);
    }
    g_free(configdir);

    /* Open config file */
    if (!g_key_file_load_from_file(termomix.cfg, termomix.configfile, 0, &gerror)) {
        /* If there's no file, ignore the error. A new one is created */
        if (gerror->code==G_KEY_FILE_ERROR_UNKNOWN_ENCODING ||
                gerror->code==G_KEY_FILE_ERROR_INVALID_VALUE) {
            fprintf(stderr, "Not valid config file format\n");
            exit(EXIT_FAILURE);
        }
    }
    
    /* Add GFile monitor to control file external changes */
    GFile *cfgfile = g_file_new_for_path(termomix.configfile);
    GFileMonitor *mon_cfgfile = g_file_monitor_file (cfgfile, 0, NULL, NULL);
    g_signal_connect(G_OBJECT(mon_cfgfile), "changed",
            G_CALLBACK(termomix_conf_changed), NULL);
    
    gchar *cfgtmp = NULL;

    /* We can safely ignore errors from g_key_file_get_value(), since if the
     * call to g_key_file_has_key() was successful, the key IS there. From the
     * glib docs I don't know if we can ignore errors from g_key_file_has_key,
     * too. I think we can: the only possible error is that the config file
     * doesn't exist, but we have just read it!
     */

    /* TODO: Use RGBA colors, with rgba_parse when gtk-3.4 deprecates some
     * functions using GdkColor. Maybe we can convert all termomix to GdkRGBA
     * colors
     */
    if (!g_key_file_has_key(termomix.cfg, cfg_group, "forecolor", NULL)) {
        termomix_set_config_string("forecolor", "#c0c0c0");
    }
    cfgtmp = g_key_file_get_value(termomix.cfg, cfg_group, "forecolor", NULL);
    gdk_color_parse(cfgtmp, &termomix.forecolor);
    g_free(cfgtmp);


    if (!g_key_file_has_key(termomix.cfg, cfg_group, "backcolor", NULL)) {
        termomix_set_config_string("backcolor", "#000000");
    }
    cfgtmp = g_key_file_get_value(termomix.cfg, cfg_group, "backcolor", NULL);
    gdk_color_parse(cfgtmp, &termomix.backcolor);
    g_free(cfgtmp);


    if (!g_key_file_has_key(termomix.cfg, cfg_group, "opacity_level", NULL)) {
        termomix_set_config_integer("opacity_level", 99);
    }
    termomix.opacity_level = g_key_file_get_integer(termomix.cfg, cfg_group, "opacity_level", NULL);


    if (!g_key_file_has_key(termomix.cfg, cfg_group, "background", NULL)) {
        termomix_set_config_string("background", "none");
    }
    cfgtmp = g_key_file_get_value(termomix.cfg, cfg_group, "background", NULL);
    if (strcmp(cfgtmp, "none")==0) {
        termomix.background=NULL;
    } else {
        termomix.background=g_strdup(cfgtmp);
    }
    g_free(cfgtmp);


    if (!g_key_file_has_key(termomix.cfg, cfg_group, "font", NULL)) {
        termomix_set_config_string("font", DEFAULT_FONT);
    }
    cfgtmp = g_key_file_get_value(termomix.cfg, cfg_group, "font", NULL);
    termomix.font = pango_font_description_from_string(cfgtmp);
    free(cfgtmp);

    if (!g_key_file_has_key(termomix.cfg, cfg_group, "cursor_type", NULL)) {
        termomix_set_config_string("cursor_type", "VTE_CURSOR_SHAPE_BLOCK");
    }
    termomix.cursor_type = g_key_file_get_integer(termomix.cfg, cfg_group,
            "cursor_type", NULL);

    if (!g_key_file_has_key(termomix.cfg, cfg_group, "word_chars", NULL)) {
        termomix_set_config_string("word_chars", DEFAULT_WORD_CHARS);
    }
    termomix.word_chars = g_key_file_get_value(termomix.cfg, cfg_group,
            "word_chars", NULL);

    termomix.palette=xterm_palette;
    
    if (!g_key_file_has_key(termomix.cfg, cfg_group, "copy_accelerator", NULL)) {
        termomix_set_config_integer("copy_accelerator", DEFAULT_COPY_ACCELERATOR);
    }
    termomix.copy_accelerator = g_key_file_get_integer(termomix.cfg, cfg_group,
            "copy_accelerator", NULL);

    if (!g_key_file_has_key(termomix.cfg, cfg_group, "open_url_accelerator", NULL)) {
        termomix_set_config_integer("open_url_accelerator",
                DEFAULT_OPEN_URL_ACCELERATOR);
    }
    termomix.open_url_accelerator = g_key_file_get_integer(termomix.cfg, cfg_group,
            "open_url_accelerator", NULL);

    if (!g_key_file_has_key(termomix.cfg, cfg_group,
            "font_size_accelerator", NULL)) {
        termomix_set_config_integer("font_size_accelerator",
                DEFAULT_FONT_SIZE_ACCELERATOR);
    }
    termomix.font_size_accelerator = g_key_file_get_integer(termomix.cfg,
            cfg_group, "font_size_accelerator", NULL);

    if (!g_key_file_has_key(termomix.cfg, cfg_group, "copy_key", NULL)) {
        termomix_set_config_key( "copy_key", DEFAULT_COPY_KEY);
    }
    termomix.copy_key = termomix_get_config_key("copy_key");

    if (!g_key_file_has_key(termomix.cfg, cfg_group, "paste_key", NULL)) {
        termomix_set_config_key("paste_key", DEFAULT_PASTE_KEY);
    }
    termomix.paste_key = termomix_get_config_key("paste_key");

    if (!g_key_file_has_key(termomix.cfg, cfg_group, "icon_file", NULL)) {
        termomix_set_config_string("icon_file", ICON_FILE);
    }
    /* We don't need a global because it's not configurable within termomix */

    termomix.provider = gtk_css_provider_new();

    termomix.main_window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(termomix.main_window), "termomix");
    gtk_window_set_has_resize_grip(GTK_WINDOW(termomix.main_window), false);

    /* Add datadir path to icon name */
    char *icon = g_key_file_get_value(termomix.cfg, cfg_group, "icon_file", NULL);
    char *icon_path = g_strdup_printf(DATADIR "/pixmaps/%s", icon);
    gtk_window_set_icon_from_file(GTK_WINDOW(termomix.main_window), icon_path,
            &gerror);
    g_free(icon); g_free(icon_path); icon=NULL; icon_path=NULL;

    /* Default terminal size*/
    termomix.columns = DEFAULT_COLUMNS;
    termomix.rows = DEFAULT_ROWS;

    /* Figure out if we have rgba capabilities. */
    GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (termomix.main_window));
    GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
    if (visual != NULL && gdk_screen_is_composited (screen)) {
        gtk_widget_set_visual (GTK_WIDGET (termomix.main_window), visual);
        termomix.has_rgba = true;
    } else {
        /* Probably not needed, as is likely the default initializer */
        termomix.has_rgba = false;
    }

    /* Command line options initialization */

    /* Set argv for forked childs. Real argv vector starts at argv[1] because we're
       using G_SPAWN_FILE_AND_ARGV_ZERO to be able to launch login shells */
    termomix.argv[0]=g_strdup(g_getenv("SHELL"));
    if (option_login) {
        termomix.argv[1]=g_strdup_printf("-%s", g_getenv("SHELL"));
    } else {
        termomix.argv[1]=g_strdup(g_getenv("SHELL"));
    }
    termomix.argv[2]=NULL;

    if (option_title) {
        gtk_window_set_title(GTK_WINDOW(termomix.main_window), option_title);
    }

    if (option_columns) {
        termomix.columns = option_columns;
    }

    if (option_rows) {
        termomix.rows = option_rows;
    }

    if (option_font) {
        termomix.font=pango_font_description_from_string(option_font);
    } 

    termomix.resized=FALSE;
    termomix.externally_modified=false;

    gerror=NULL;
    termomix.http_regexp=g_regex_new(HTTP_REGEXP, G_REGEX_CASELESS,
            G_REGEX_MATCH_NOTEMPTY, &gerror);

    termomix_init_popup();

    g_signal_connect(G_OBJECT(termomix.main_window), "delete_event",
            G_CALLBACK(termomix_delete_event), NULL);
    g_signal_connect(G_OBJECT(termomix.main_window), "destroy",
            G_CALLBACK(termomix_destroy_window), NULL);
    g_signal_connect(G_OBJECT(termomix.main_window), "key-press-event",
            G_CALLBACK(termomix_key_press), NULL);
    g_signal_connect(G_OBJECT(termomix.main_window), "configure-event",
            G_CALLBACK(termomix_resized_window), NULL);
}


static void
termomix_init_popup()
{
    GtkWidget *item_copy, *item_paste, *item_select_font, *item_select_colors,
            *item_select_background, *item_set_title, *item_options,
            *item_input_methods, *item_opacity_menu, *item_cursor,
            *item_cursor_block, *item_cursor_underline, *item_cursor_ibeam;
    GtkAction *action_open_link, *action_copy_link, *action_copy,
            *action_paste, *action_select_font, *action_select_colors,
            *action_select_background, *action_clear_background,
            *action_opacity, *action_set_title;
    GtkWidget *options_menu, *cursor_menu;

    /* Define actions */
    action_open_link=gtk_action_new("open_link", gettext("Open link..."), NULL, NULL);
    action_copy_link=gtk_action_new("copy_link", gettext("Copy link..."), NULL, NULL);
    action_copy=gtk_action_new("copy", gettext("Copy"), NULL, GTK_STOCK_COPY);
    action_paste=gtk_action_new("paste", gettext("Paste"), NULL, GTK_STOCK_PASTE);
    action_select_font=gtk_action_new("select_font", gettext("Select font..."),
            NULL, GTK_STOCK_SELECT_FONT);
    action_select_colors=gtk_action_new("select_colors", gettext("Select colors..."),
            NULL, GTK_STOCK_SELECT_COLOR);
    action_select_background=gtk_action_new("select_background",
            gettext("Select background..."), NULL, NULL);
    action_clear_background=gtk_action_new("clear_background",
            gettext("Clear background"), NULL, NULL);
    action_opacity=gtk_action_new("set_opacity", gettext("Set opacity level..."),
            NULL, NULL);
    action_set_title=gtk_action_new("set_title", gettext("Set window title..."),
            NULL, NULL);

    /* Create menuitems */
    termomix.item_open_link=gtk_action_create_menu_item(action_open_link);
    termomix.item_copy_link=gtk_action_create_menu_item(action_copy_link);
    item_copy=gtk_action_create_menu_item(action_copy);
    item_paste=gtk_action_create_menu_item(action_paste);
    item_select_font=gtk_action_create_menu_item(action_select_font);
    item_select_colors=gtk_action_create_menu_item(action_select_colors);
    item_select_background=gtk_action_create_menu_item(action_select_background);
    termomix.item_clear_background=gtk_action_create_menu_item(action_clear_background);
    item_opacity_menu=gtk_action_create_menu_item(action_opacity);
    item_set_title=gtk_action_create_menu_item(action_set_title);

    item_options=gtk_menu_item_new_with_label(gettext("Options"));

    /* FIXME: Use actions for all items, or no use'em at all */
    item_cursor = gtk_menu_item_new_with_label(gettext("Set cursor type"));
    item_cursor_block = gtk_radio_menu_item_new_with_label(NULL, gettext("Block"));
    item_cursor_underline = gtk_radio_menu_item_new_with_label_from_widget(
            GTK_RADIO_MENU_ITEM(item_cursor_block), gettext("Underline"));
    item_cursor_ibeam = gtk_radio_menu_item_new_with_label_from_widget(
            GTK_RADIO_MENU_ITEM(item_cursor_block), gettext("IBeam"));
    item_input_methods = gtk_menu_item_new_with_label(gettext("Input methods"));

    /* Show defaults in menu items */
    switch (termomix.cursor_type){
        case VTE_CURSOR_SHAPE_BLOCK:
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_block), TRUE);
            break;
        case VTE_CURSOR_SHAPE_UNDERLINE:
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_underline), TRUE);
            break;
        case VTE_CURSOR_SHAPE_IBEAM:
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_ibeam), TRUE);
    }

    termomix.open_link_separator=gtk_separator_menu_item_new();

    termomix.menu=gtk_menu_new();

    /* Add items to popup menu */
    gtk_menu_shell_append(GTK_MENU_SHELL(termomix.menu), termomix.item_open_link);
    gtk_menu_shell_append(GTK_MENU_SHELL(termomix.menu), termomix.item_copy_link);
    gtk_menu_shell_append(GTK_MENU_SHELL(termomix.menu), termomix.open_link_separator);
    gtk_menu_shell_append(GTK_MENU_SHELL(termomix.menu), item_copy);
    gtk_menu_shell_append(GTK_MENU_SHELL(termomix.menu), item_paste);
    gtk_menu_shell_append(GTK_MENU_SHELL(termomix.menu), termomix.item_clear_background);
    gtk_menu_shell_append(GTK_MENU_SHELL(termomix.menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(termomix.menu), item_options);

    termomix.im_menu=gtk_menu_new();
    options_menu=gtk_menu_new();
    cursor_menu=gtk_menu_new();

    gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_opacity_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_set_title);
    gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_select_colors);
    gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_select_font);
    gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_select_background);
    gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_cursor);
    gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_input_methods);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_block);
    gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_underline);
    gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_ibeam);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_input_methods), termomix.im_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_options), options_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_cursor), cursor_menu);

    /* ... and finally assign callbacks to menuitems */
    g_signal_connect(G_OBJECT(action_select_font), "activate",
            G_CALLBACK(termomix_font_dialog), NULL);
    g_signal_connect(G_OBJECT(action_select_background), "activate",
            G_CALLBACK(termomix_select_background_dialog), NULL);
    g_signal_connect(G_OBJECT(action_copy), "activate",
            G_CALLBACK(termomix_copy), NULL);
    g_signal_connect(G_OBJECT(action_paste), "activate",
            G_CALLBACK(termomix_paste), NULL);
    g_signal_connect(G_OBJECT(action_select_colors), "activate",
            G_CALLBACK(termomix_color_dialog), NULL);
    g_signal_connect(G_OBJECT(action_opacity), "activate",
            G_CALLBACK(termomix_opacity_dialog), NULL);
    g_signal_connect(G_OBJECT(action_set_title), "activate",
            G_CALLBACK(termomix_set_title_dialog), NULL);
    g_signal_connect(G_OBJECT(item_cursor_block), "activate",
            G_CALLBACK(termomix_set_cursor), "block");
    g_signal_connect(G_OBJECT(item_cursor_underline), "activate",
            G_CALLBACK(termomix_set_cursor), "underline");
    g_signal_connect(G_OBJECT(item_cursor_ibeam), "activate",
            G_CALLBACK(termomix_set_cursor), "ibeam");
    g_signal_connect(G_OBJECT(action_open_link), "activate",
            G_CALLBACK(termomix_open_url), NULL);
    g_signal_connect(G_OBJECT(action_copy_link), "activate",
            G_CALLBACK(termomix_copy_url), NULL);
    g_signal_connect(G_OBJECT(action_clear_background), "activate",
            G_CALLBACK(termomix_clear), NULL);

    gtk_widget_show_all(termomix.menu);

    /* We don't want to see this if there's no background image */
    if (!termomix.background) {
        gtk_widget_hide(termomix.item_clear_background);
    }
}


static void
termomix_destroy()
{
    g_key_file_free(termomix.cfg);

    pango_font_description_free(termomix.font);

    if (termomix.background)
        free(termomix.background);

    free(termomix.configfile);

    gtk_main_quit();
}


static void
termomix_set_size(gint columns, gint rows)
{
    gint pad_x, pad_y;
    gint char_width, char_height;

    /* Mayhaps an user resize happened. Check if row and columns have changed */
    if (termomix.resized) {
        termomix.columns=vte_terminal_get_column_count(VTE_TERMINAL(termomix.term->vte));
        termomix.rows=vte_terminal_get_row_count(VTE_TERMINAL(termomix.term->vte));
        termomix.resized=FALSE;
    }

    gtk_widget_style_get(termomix.term->vte, "inner-border", &termomix.term->border, NULL);
    pad_x = termomix.term->border->left + termomix.term->border->right;
    pad_y = termomix.term->border->top + termomix.term->border->bottom;
    char_width = vte_terminal_get_char_width(VTE_TERMINAL(termomix.term->vte));
    char_height = vte_terminal_get_char_height(VTE_TERMINAL(termomix.term->vte));

    termomix.width = pad_x + (char_width * termomix.columns);
    termomix.height = pad_y + (char_height * termomix.rows);

    /* GTK ignores resizes for maximized windows, so we don't need no check if
     * it's maximized or not
     */
    gtk_window_resize(GTK_WINDOW(termomix.main_window), termomix.width, termomix.height);
}


static void
termomix_set_font()
{
    vte_terminal_set_font(VTE_TERMINAL(termomix.term->vte), termomix.font);
}


static void
termomix_add_tab()
{
    gchar *cwd = NULL;

    termomix.term = g_new0( struct terminal, 1 );
    termomix.term->hbox=gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    termomix.term->vte=vte_terminal_new();

    /* Init vte */
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(termomix.term->vte), SCROLL_LINES);
    vte_terminal_match_add_gregex(VTE_TERMINAL(termomix.term->vte), termomix.http_regexp, 0);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(termomix.term->vte), TRUE);
    
    gtk_box_pack_start(GTK_BOX(termomix.term->hbox), termomix.term->vte, TRUE, TRUE, 0);

    cwd = g_get_current_dir();
   
    gtk_container_add(GTK_CONTAINER(termomix.main_window), termomix.term->hbox); 


    /* vte signals */
    g_signal_connect(G_OBJECT(termomix.term->vte), "increase-font-size",
            G_CALLBACK(termomix_increase_font), NULL);
    g_signal_connect(G_OBJECT(termomix.term->vte), "decrease-font-size",
            G_CALLBACK(termomix_decrease_font), NULL);
    g_signal_connect(G_OBJECT(termomix.term->vte), "child-exited",
            G_CALLBACK(termomix_child_exited), NULL);
    g_signal_connect(G_OBJECT(termomix.term->vte), "eof",
            G_CALLBACK(termomix_eof), NULL);
    g_signal_connect_swapped(G_OBJECT(termomix.term->vte), "button-press-event",
            G_CALLBACK(termomix_button_press), termomix.menu);

    termomix_set_font();
    /* Set size before showing the widgets but after setting the font */
    termomix_set_size(termomix.columns, termomix.rows);

    gtk_widget_show_all(termomix.term->hbox);

    if (option_geometry) {
        if (!gtk_window_parse_geometry(GTK_WINDOW(termomix.main_window), option_geometry)) {
            fprintf(stderr, "Invalid geometry.\n");
            gtk_widget_show(termomix.main_window);
        } else {
            gtk_widget_show(termomix.main_window);
            termomix.columns = VTE_TERMINAL(termomix.term->vte)->column_count;
            termomix.rows = VTE_TERMINAL(termomix.term->vte)->row_count;
        }
    } else {
        gtk_widget_show(termomix.main_window);
    }

    if (option_execute||option_xterm_execute) {
        int command_argc; char **command_argv;
        GError *gerror = NULL;
        gchar *path;

        if(option_execute) {
                /* -x option */
                if (!g_shell_parse_argv(option_execute, &command_argc, &command_argv, &gerror)) {
                    switch (gerror->code) {
                        case G_SHELL_ERROR_EMPTY_STRING:
                            termomix_error("Empty exec string");
                            exit(1);
                            break;
                        case G_SHELL_ERROR_BAD_QUOTING: 
                            termomix_error("Cannot parse command line arguments: mangled quoting");
                            exit(1);
                            break;
                        case G_SHELL_ERROR_FAILED:
                            termomix_error("Error in exec option command line arguments");
                            exit(1);
                    }
                } else {
                }
            } else {
                /* -e option - last in the command line */
                gchar *command_joined;
                /* the xterm -e command takes all extra arguments */
                command_joined = g_strjoinv(" ", option_xterm_args);
                if (!g_shell_parse_argv(command_joined, &command_argc, &command_argv, &gerror)) {
                    switch (gerror->code) {
                        case G_SHELL_ERROR_EMPTY_STRING:
                            termomix_error("Empty exec string");
                            exit(1);
                            break;
                        case G_SHELL_ERROR_BAD_QUOTING: 
                            termomix_error("Cannot parse command line arguments: mangled quoting");
                            exit(1);
                        case G_SHELL_ERROR_FAILED:
                            termomix_error("Error in exec option command line arguments");
                            exit(1);
                    }
                }
                g_free(command_joined);
            }

            /* Check if the command is valid */
            path=g_find_program_in_path(command_argv[0]);
            if (path) {
                vte_terminal_fork_command_full(VTE_TERMINAL(termomix.term->vte),
                        VTE_PTY_DEFAULT, NULL, command_argv, NULL,
                        G_SPAWN_SEARCH_PATH, NULL, NULL, &termomix.term->pid,
                        NULL);
            } else {
                termomix_error("%s binary not found", command_argv[0]);
                exit(1);
            }
            free(path);
            g_strfreev(command_argv); g_strfreev(option_xterm_args);
        } else { /* No execute option */
            if (option_hold==TRUE) {
                termomix_error("Hold option given without any command");
                option_hold=FALSE;
            }
            vte_terminal_fork_command_full(VTE_TERMINAL(termomix.term->vte),
                    VTE_PTY_DEFAULT, cwd, termomix.argv, NULL,
                    G_SPAWN_SEARCH_PATH|G_SPAWN_FILE_AND_ARGV_ZERO, NULL, NULL,
                    &termomix.term->pid, NULL);
        }
    /* Not the first tab */

    free(cwd);

    /* Configuration for the newly created terminal */
    GdkColor white={0, 255, 255, 255};
    vte_terminal_set_color_background(VTE_TERMINAL (termomix.term->vte), &white);
    vte_terminal_set_backspace_binding(VTE_TERMINAL(termomix.term->vte),
            VTE_ERASE_ASCII_DELETE);
    vte_terminal_set_colors(VTE_TERMINAL(termomix.term->vte), &termomix.forecolor,
            &termomix.backcolor, termomix.palette, PALETTE_SIZE);
    if (termomix.has_rgba) {
        vte_terminal_set_opacity(VTE_TERMINAL (termomix.term->vte),
                (termomix.opacity_level*65535)/99); /* 0-99 value */
    }

    if (termomix.background) {
        termomix_set_bgimage(termomix.background);
    }

    if (termomix.word_chars) {
        vte_terminal_set_word_chars( VTE_TERMINAL (termomix.term->vte),
                termomix.word_chars );
    }

    /* Change cursor */    
    vte_terminal_set_cursor_shape (VTE_TERMINAL(termomix.term->vte),
            termomix.cursor_type);

    gtk_widget_grab_focus(termomix.term->vte);
}

static void termomix_set_bgimage(char *infile) {
    GError *gerror=NULL;
    GdkPixbuf *pixbuf=NULL;

    /* Check file existence and type */
    if (g_file_test(infile, G_FILE_TEST_IS_REGULAR)) {

        pixbuf = gdk_pixbuf_new_from_file (infile, &gerror);
        if (!pixbuf) {
            termomix_error("Error loading image file: %s\n", gerror->message);
        } else {
            vte_terminal_set_background_image(VTE_TERMINAL(termomix.term->vte), pixbuf);
            vte_terminal_set_background_saturation(VTE_TERMINAL(termomix.term->vte), TRUE);
            vte_terminal_set_background_transparent(VTE_TERMINAL(termomix.term->vte),FALSE);

            termomix_set_config_string("background", infile);
        }
    }
}


static void
termomix_set_config_key(const gchar *key, guint value)
{
    char *valname;

    valname=gdk_keyval_name(value);
    g_key_file_set_string(termomix.cfg, cfg_group, key, valname);
    termomix.config_modified=TRUE;
    //FIXME: free() valname?
} 


static guint
termomix_get_config_key(const gchar *key)
{
    gchar *value;
    guint retval=GDK_KEY_VoidSymbol;

    value=g_key_file_get_string(termomix.cfg, cfg_group, key, NULL);
    if (value!=NULL){
        retval=gdk_keyval_from_name(value);
        g_free(value);
    }

    /* For backwards compatibility with integer values */
    /* If gdk_keyval_from_name fail, it seems to be integer value*/
    if ((retval==GDK_KEY_VoidSymbol)||(retval==0)) {
        retval=g_key_file_get_integer(termomix.cfg, cfg_group, key, NULL);
    }

    return retval;
}


static void
termomix_error(const char *format, ...)
{
    GtkWidget *dialog;
    va_list args;
    char* buff;

    va_start(args, format);
    buff = malloc(sizeof(char)*ERROR_BUFFER_LENGTH);
    vsnprintf(buff, sizeof(char)*ERROR_BUFFER_LENGTH, format, args);
    va_end(args);

    dialog = gtk_message_dialog_new(GTK_WINDOW(termomix.main_window),
            GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE, "%s", buff);
    gtk_window_set_title(GTK_WINDOW(dialog), gettext("Error message"));
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    free(buff);
}


int
main(int argc, char **argv)
{
    gchar *localedir;
    GError *error=NULL;
    GOptionContext *context;
    int i;
    int n;
    char **nargv;
    int nargc;

    /* Localization */
    setlocale(LC_ALL, "");
    localedir=g_strdup_printf("%s/locale", DATADIR);
    textdomain(GETTEXT_PACKAGE);
    bindtextdomain(GETTEXT_PACKAGE, localedir);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    g_free(localedir);

    /* Rewrites argv to include a -- after the -e argument this is required to make
     * sure GOption doesn't grab any arguments meant for the command being called */

    /* Initialize nargv */
    nargv = (char**)calloc((argc+1), sizeof(char*));
       n=0; nargc=argc;

    for(i=0; i<argc; i++) {
        if(g_strcmp0(argv[i],"-e") == 0)
        {
            nargv[n]="-e";
            n++;
            nargv[n]="--";
            nargc = argc+1;
        } else {
            nargv[n]=g_strdup(argv[i]);
        }
        n++;
    }

    /* Options parsing */
    context = g_option_context_new (gettext("- vte-based terminal emulator"));
    g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
    g_option_group_set_translation_domain(gtk_get_option_group(TRUE), GETTEXT_PACKAGE);
    g_option_context_add_group (context, gtk_get_option_group(TRUE));
    g_option_context_parse (context, &nargc, &nargv, &error);

    if (option_version) {
        fprintf(stderr, gettext("termomix version is %s\n"), VERSION);
        exit(1);
    }

    g_option_context_free(context);

    gtk_init(&nargc, &nargv);

    g_strfreev(nargv);

    /* Init stuff */
    termomix_init();
    
    /* Add first tab */
    termomix_add_tab();
    
    /* Fill Input Methods menu */
    vte_terminal_im_append_menuitems(VTE_TERMINAL(termomix.term->vte), GTK_MENU_SHELL(termomix.im_menu));

    gtk_main();

    return 0;
}
