/*
 * Sylpheed -- a GTK+ based, lightweight, and fast e-mail client
 * Copyright (C) 1999-2007 Hiroyuki Yamamoto and the Claws Mail team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "defs.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtksignal.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkentry.h>
#include <gtk/gtktable.h>

#include "alertpanel.h"
#include "mgutils.h"
#include "addressbook.h"
#include "addressitem.h"
#include "addritem.h"
#include "addrbook.h"
#include "manage_window.h"
#include "gtkutils.h"
#include "filesel.h"
#include "codeconv.h"
#include "editaddress.h"
#include "editaddress_other_attributes_ldap.h"
#include "prefs_common.h"
#include "menu.h"

/* transient data */
static struct _PersonEdit_dlg personeditdlg;
static AddressBookFile *current_abf = NULL;
static ItemPerson *current_person = NULL;
static ItemFolder *current_parent_folder = NULL;
static EditAddressPostUpdateCallback edit_person_close_post_update_cb = NULL;

typedef enum {
	EMAIL_COL_EMAIL   = 0,
	EMAIL_COL_ALIAS   = 1,
	EMAIL_COL_REMARKS = 2
} PersonEditEMailColumnPos;

typedef enum {
	ATTRIB_COL_NAME    = 0,
	ATTRIB_COL_VALUE   = 1
} PersonEditAttribColumnPos;

#define EDITPERSON_WIDTH      520
#define EDITPERSON_HEIGHT     320

#ifndef MAEMO
# define EMAIL_N_COLS          3
# define EMAIL_COL_WIDTH_EMAIL 180
# define EMAIL_COL_WIDTH_ALIAS 80
#else
# define EMAIL_N_COLS          1
# define EMAIL_COL_WIDTH_EMAIL 130
# define EMAIL_COL_WIDTH_ALIAS 130
#endif

#ifndef MAEMO
# define ATTRIB_N_COLS          2
# define ATTRIB_COL_WIDTH_NAME  240
# define ATTRIB_COL_WIDTH_VALUE 0
#else
# define ATTRIB_N_COLS          2
# define ATTRIB_COL_WIDTH_NAME  120
# define ATTRIB_COL_WIDTH_VALUE 120
#endif

#define PAGE_BASIC             0
#define PAGE_EMAIL             1
#define PAGE_ATTRIBUTES        2

static gboolean addressbook_edit_person_close( gboolean cancelled );

static void edit_person_status_show( gchar *msg ) {
	if( personeditdlg.statusbar != NULL ) {
		gtk_statusbar_pop( GTK_STATUSBAR(personeditdlg.statusbar), personeditdlg.status_cid );
		if( msg ) {
			gtk_statusbar_push( GTK_STATUSBAR(personeditdlg.statusbar), personeditdlg.status_cid, msg );
		}
	}
}

static void edit_person_ok(GtkWidget *widget, gboolean *cancelled) {
	*cancelled = FALSE;
	if (prefs_common.addressbook_use_editaddress_dialog)
		gtk_main_quit();
	else
		addressbook_edit_person_close( *cancelled );
}

static void edit_person_cancel(GtkWidget *widget, gboolean *cancelled) {
	*cancelled = TRUE;
	if (prefs_common.addressbook_use_editaddress_dialog)
		gtk_main_quit();
	else
		addressbook_edit_person_close( *cancelled );
}

static gint edit_person_delete_event(GtkWidget *widget, GdkEventAny *event, gboolean *cancelled) {
	*cancelled = TRUE;
	if (prefs_common.addressbook_use_editaddress_dialog)
		gtk_main_quit();
	else
		addressbook_edit_person_close( *cancelled );
	return TRUE;
}

static gboolean edit_person_key_pressed(GtkWidget *widget, GdkEventKey *event, gboolean *cancelled) {
	if (prefs_common.addressbook_use_editaddress_dialog) {
	if (event && event->keyval == GDK_Escape) {
		*cancelled = TRUE;
		gtk_main_quit();
	}
	}
	return FALSE;
}

static gchar *_title_new_ = NULL;
static gchar *_title_edit_ = NULL;

static void edit_person_set_widgets_title( gchar *text )
{
	gchar *label = NULL;

	g_return_if_fail( text != NULL );

	gtk_label_set_text(GTK_LABEL(personeditdlg.title), "");
	label = g_markup_printf_escaped("<b>%s</b>", text);
	gtk_label_set_markup(GTK_LABEL(personeditdlg.title), label);
	g_free(label);
}

static void edit_person_set_window_title( gint pageNum ) {
	gchar *sTitle;

	if( _title_new_ == NULL ) {
		_title_new_ = g_strdup( _("Add New Person") );
		_title_edit_ = g_strdup( _("Edit Person Details") );
	}

	if( pageNum == PAGE_BASIC ) {
		if( personeditdlg.editNew ) {
			if (prefs_common.addressbook_use_editaddress_dialog)
				gtk_window_set_title( GTK_WINDOW(personeditdlg.container), _title_new_ );
			else
				edit_person_set_widgets_title( _title_new_ );
		}
		else {
			if (prefs_common.addressbook_use_editaddress_dialog)
				gtk_window_set_title( GTK_WINDOW(personeditdlg.container), _title_edit_ );
			else
				edit_person_set_widgets_title( _title_edit_ );
		}
	}
	else {
		if( personeditdlg.entry_name == NULL ) {
			sTitle = g_strdup( _title_edit_ );
		}
		else {
			gchar *name;
			name = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_name), 0, -1 );
			sTitle = g_strdup_printf( "%s - %s", _title_edit_, name );
			g_free( name );
		}
		if (prefs_common.addressbook_use_editaddress_dialog)
			gtk_window_set_title( GTK_WINDOW(personeditdlg.container), sTitle );
		else
			edit_person_set_widgets_title( sTitle );
		g_free( sTitle );
	}
}

static void edit_person_email_clear( gpointer data ) {
	gtk_entry_set_text( GTK_ENTRY(personeditdlg.entry_email), "" );
	gtk_entry_set_text( GTK_ENTRY(personeditdlg.entry_alias), "" );
	gtk_entry_set_text( GTK_ENTRY(personeditdlg.entry_remarks), "" );
}

static void edit_person_attrib_clear( gpointer data ) {
	if (!personeditdlg.ldap) {
		gtk_entry_set_text( GTK_ENTRY(personeditdlg.entry_atname), "" );
		gtk_entry_set_text( GTK_ENTRY(personeditdlg.entry_atvalue), "" );
	}
}

static void edit_person_switch_page( GtkNotebook *notebook, GtkNotebookPage *page,
					gint pageNum, gpointer user_data)
{
	edit_person_set_window_title( pageNum );
	edit_person_status_show( "" );
}

/*
* Load clist with a copy of person's email addresses.
*/
static void edit_person_load_email( ItemPerson *person ) {
	GList *node = person->listEMail;
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_email);
	gchar *text[ EMAIL_N_COLS ];
	while( node ) {
		ItemEMail *emorig = ( ItemEMail * ) node->data;
		ItemEMail *email = addritem_copyfull_item_email( emorig );
		gint row;
		text[ EMAIL_COL_EMAIL   ] = email->address;
#ifndef MAEMO
		text[ EMAIL_COL_ALIAS   ] = email->obj.name;
		text[ EMAIL_COL_REMARKS ] = email->remarks;
#endif
		row = gtk_clist_append( clist, text );
		gtk_clist_set_row_data( clist, row, email );
		node = g_list_next( node );
	}
}

static void edit_person_email_list_selected( GtkCList *clist, gint row, gint column, GdkEvent *event, gpointer data ) {
	ItemEMail *email = gtk_clist_get_row_data( clist, row );
	if( email ) {
		if( email->address )
			gtk_entry_set_text( GTK_ENTRY(personeditdlg.entry_email), email->address );
		if( ADDRITEM_NAME(email) )
			gtk_entry_set_text( GTK_ENTRY(personeditdlg.entry_alias), ADDRITEM_NAME(email) );
		if( email->remarks )
			gtk_entry_set_text( GTK_ENTRY(personeditdlg.entry_remarks), email->remarks );
		if (!personeditdlg.read_only) {
			gtk_widget_set_sensitive(personeditdlg.email_del, TRUE);
			gtk_widget_set_sensitive(personeditdlg.email_up, row > 0);
			gtk_widget_set_sensitive(personeditdlg.email_down, gtk_clist_get_row_data(clist, row + 1) != NULL);
		}
	} else {
		gtk_widget_set_sensitive(personeditdlg.email_del, FALSE);
		gtk_widget_set_sensitive(personeditdlg.email_up, FALSE);
		gtk_widget_set_sensitive(personeditdlg.email_down, FALSE);
	}
	personeditdlg.rowIndEMail = row;
	edit_person_status_show( NULL );
}

static void edit_person_email_move( gint dir ) {
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_email);
	gint row = personeditdlg.rowIndEMail + dir;
	ItemEMail *email = gtk_clist_get_row_data( clist, row );
	if( email ) {
		gtk_clist_row_move( clist, personeditdlg.rowIndEMail, row );
		personeditdlg.rowIndEMail = row;
		if (!personeditdlg.read_only) {
			gtk_widget_set_sensitive(personeditdlg.email_up, row > 0);
			gtk_widget_set_sensitive(personeditdlg.email_down, gtk_clist_get_row_data(clist, row + 1) != NULL);
		}
	} else {
		gtk_widget_set_sensitive(personeditdlg.email_up, FALSE);
		gtk_widget_set_sensitive(personeditdlg.email_down, FALSE);
	}
	edit_person_email_clear( NULL );
	edit_person_status_show( NULL );
}

static void edit_person_email_move_up( gpointer data ) {
	edit_person_email_move( -1 );
}

static void edit_person_email_move_down( gpointer data ) {
	edit_person_email_move( +1 );
}

static void edit_person_email_delete( gpointer data ) {
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_email);
	gint row = personeditdlg.rowIndEMail;
	ItemEMail *email = gtk_clist_get_row_data( clist, row );
	edit_person_email_clear( NULL );
	if( email ) {
		/* Remove list entry */
		gtk_clist_remove( clist, row );
		addritem_free_item_email( email );
		email = NULL;
	}

	/* Position hilite bar */
	email = gtk_clist_get_row_data( clist, row );
	if( ! email ) {
		personeditdlg.rowIndEMail = -1 + row;
	}
	if (!personeditdlg.read_only) {
		gtk_widget_set_sensitive(personeditdlg.email_del, gtk_clist_get_row_data(clist, 0) != NULL);
		gtk_widget_set_sensitive(personeditdlg.email_up, gtk_clist_get_row_data(clist, personeditdlg.rowIndEMail + 1) != NULL);
		gtk_widget_set_sensitive(personeditdlg.email_down, gtk_clist_get_row_data(clist, personeditdlg.rowIndEMail - 1) != NULL);
	}
	edit_person_status_show( NULL );
}

static ItemEMail *edit_person_email_edit( gboolean *error, ItemEMail *email ) {
	ItemEMail *retVal = NULL;
	gchar *sEmail, *sAlias, *sRemarks, *sEmail_;

	*error = TRUE;
	sEmail_ = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_email), 0, -1 );
	sAlias = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_alias), 0, -1 );
	sRemarks = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_remarks), 0, -1 );
	sEmail = mgu_email_check_empty( sEmail_ );
	g_free( sEmail_ );

	if( sEmail ) {
		if( email == NULL ) {
			email = addritem_create_item_email();
		}
		addritem_email_set_address( email, sEmail );
		addritem_email_set_alias( email, sAlias );
		addritem_email_set_remarks( email, sRemarks );
		retVal = email;
		*error = FALSE;
	}
	else {
		edit_person_status_show( _( "An Email address must be supplied." ) );
	}

	g_free( sEmail );
	g_free( sAlias );
	g_free( sRemarks );

	return retVal;
}

static void edit_person_email_modify( gpointer data ) {
	gboolean errFlg = FALSE;
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_email);
	gint row = personeditdlg.rowIndEMail;
	ItemEMail *email = gtk_clist_get_row_data( clist, row );
	if( email ) {
		edit_person_email_edit( &errFlg, email );
		if( ! errFlg ) {
			gtk_clist_set_text( clist, row, EMAIL_COL_EMAIL, email->address );
			gtk_clist_set_text( clist, row, EMAIL_COL_ALIAS, email->obj.name );
			gtk_clist_set_text( clist, row, EMAIL_COL_REMARKS, email->remarks );
			edit_person_email_clear( NULL );
		}
	}
}

static void edit_person_email_add( gpointer data ) {
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_email);
	gboolean errFlg = FALSE;
	ItemEMail *email = NULL;
	gint row = personeditdlg.rowIndEMail;
	if( gtk_clist_get_row_data( clist, row ) == NULL ) row = 0;

	email = edit_person_email_edit( &errFlg, NULL );
	if( ! errFlg ) {
		gchar *text[ EMAIL_N_COLS ];
		text[ EMAIL_COL_EMAIL   ] = email->address;
#ifndef MAEMO
		text[ EMAIL_COL_ALIAS   ] = email->obj.name;
		text[ EMAIL_COL_REMARKS ] = email->remarks;
#endif
		row = gtk_clist_insert( clist, 1 + row, text );
		gtk_clist_set_row_data( clist, row, email );
		gtk_clist_select_row( clist, row, 0 );
		edit_person_email_clear( NULL );
	}
}

/*
* Comparison using cell contents (text in first column). Used for sort
* address index widget.
*/
static gint edit_person_attrib_compare_func(
	GtkCList *clist, gconstpointer ptr1, gconstpointer ptr2 )
{
	GtkCell *cell1 = ((GtkCListRow *)ptr1)->cell;
	GtkCell *cell2 = ((GtkCListRow *)ptr2)->cell;
	gchar *name1 = NULL, *name2 = NULL;

	if( cell1 ) name1 = cell1->u.text;
	if( cell2 ) name2 = cell2->u.text;
	if( ! name1 ) return ( name2 != NULL );
	if( ! name2 ) return -1;
	return g_utf8_collate( name1, name2 );
}

static gboolean list_find_attribute(const gchar *attr)
{
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_attrib);
	UserAttribute *attrib;
	gint row = 0;
	while( (attrib = gtk_clist_get_row_data( clist, row )) ) {
		if (!g_ascii_strcasecmp(attrib->name, attr)) {
			gtk_clist_select_row(clist, row, 0);
			return TRUE;
		}
		row++;
	}
	return FALSE;
}

static gboolean list_find_email(const gchar *addr)
{
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_email);
	ItemEMail *email;
	gint row = 0;
	while( (email = gtk_clist_get_row_data( clist, row )) ) {
		if (!g_ascii_strcasecmp(email->address, addr)) {
			gtk_clist_select_row(clist, row, 0);
			return TRUE;
		}
		row++;
	}
	return FALSE;
}

/*
* Load clist with a copy of person's email addresses.
*/
static void edit_person_load_attrib( ItemPerson *person ) {
	GList *node = person->listAttrib;
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_attrib);
	gchar *text[ ATTRIB_N_COLS ];
	while( node ) {
		UserAttribute *atorig = ( UserAttribute * ) node->data;
		UserAttribute *attrib = addritem_copy_attribute( atorig );
		gint row;
		debug_print("name: %s value: %s\n", attrib->name, attrib->value);
		text[ ATTRIB_COL_NAME  ] = attrib->name;
		text[ ATTRIB_COL_VALUE ] = attrib->value;

		row = gtk_clist_append( clist, text );
		gtk_clist_set_row_data( clist, row, attrib );
		node = g_list_next( node );
	}
}

static void edit_person_attrib_list_selected( GtkCList *clist, gint row, gint column, GdkEvent *event, gpointer data ) {
	UserAttribute *attrib = gtk_clist_get_row_data( clist, row );
	if( attrib && !personeditdlg.read_only && !personeditdlg.ldap ) {
		gtk_entry_set_text( GTK_ENTRY(personeditdlg.entry_atname), attrib->name );
		gtk_entry_set_text( GTK_ENTRY(personeditdlg.entry_atvalue), attrib->value );
		gtk_widget_set_sensitive(personeditdlg.attrib_del, TRUE);
	} else {
		gtk_widget_set_sensitive(personeditdlg.attrib_del, FALSE);
	}
	personeditdlg.rowIndAttrib = row;
	edit_person_status_show( NULL );
}

static void edit_person_attrib_delete( gpointer data ) {
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_attrib);
	gint row = personeditdlg.rowIndAttrib;
	UserAttribute *attrib = gtk_clist_get_row_data( clist, row );
	edit_person_attrib_clear( NULL );
	if( attrib ) {
		/* Remove list entry */
		gtk_clist_remove( clist, row );
		addritem_free_attribute( attrib );
		attrib = NULL;
	}

	/* Position hilite bar */
	attrib = gtk_clist_get_row_data( clist, row );
	if( ! attrib ) {
		personeditdlg.rowIndAttrib = -1 + row;
	} 
	
	if (!personeditdlg.read_only && !personeditdlg.ldap)
		gtk_widget_set_sensitive(personeditdlg.attrib_del, gtk_clist_get_row_data(clist, 0) != NULL);
	
	edit_person_status_show( NULL );
}

static UserAttribute *edit_person_attrib_edit( gboolean *error, UserAttribute *attrib ) {
	UserAttribute *retVal = NULL;
	gchar *sName, *sValue, *sName_, *sValue_;

	*error = TRUE;
	sName_ = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_atname), 0, -1 );
	sValue_ = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_atvalue), 0, -1 );
	sName = mgu_email_check_empty( sName_ );
	sValue = mgu_email_check_empty( sValue_ );
	g_free( sName_ );
	g_free( sValue_ );

	if( sName && sValue ) {
		if( attrib == NULL ) {
			attrib = addritem_create_attribute();
		}
		addritem_attrib_set_name( attrib, sName );
		addritem_attrib_set_value( attrib, sValue );
		retVal = attrib;
		*error = FALSE;
	}
	else {
		edit_person_status_show( _( "A Name and Value must be supplied." ) );
	}

	g_free( sName );
	g_free( sValue );

	return retVal;
}

static void edit_person_attrib_modify( gpointer data ) {
	gboolean errFlg = FALSE;
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_attrib);
	gint row = personeditdlg.rowIndAttrib;
	UserAttribute *attrib = gtk_clist_get_row_data( clist, row );
	if( attrib ) {
		edit_person_attrib_edit( &errFlg, attrib );
		if( ! errFlg ) {
			gtk_clist_set_text( clist, row, ATTRIB_COL_NAME, attrib->name );
			gtk_clist_set_text( clist, row, ATTRIB_COL_VALUE, attrib->value );
			edit_person_attrib_clear( NULL );
		}
	}
}

static void edit_person_attrib_add( gpointer data ) {
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_attrib);
	gboolean errFlg = FALSE;
	UserAttribute *attrib = NULL;
	gint row = personeditdlg.rowIndAttrib;
	if( gtk_clist_get_row_data( clist, row ) == NULL ) row = 0;

	attrib = edit_person_attrib_edit( &errFlg, NULL );
	if( ! errFlg ) {
		gchar *text[ ATTRIB_N_COLS ];
		text[ ATTRIB_COL_NAME  ] = attrib->name;
		text[ ATTRIB_COL_VALUE ] = attrib->value;

		row = gtk_clist_insert( clist, 1 + row, text );
		gtk_clist_set_row_data( clist, row, attrib );
		gtk_clist_select_row( clist, row, 0 );
		edit_person_attrib_clear( NULL );
	}
}

/*!
 *\brief	Save Gtk object size to prefs dataset
 */
static void edit_person_size_allocate_cb(GtkWidget *widget,
					 GtkAllocation *allocation)
{
	g_return_if_fail(allocation != NULL);

	prefs_common.addressbookeditpersonwin_width = allocation->width;
	prefs_common.addressbookeditpersonwin_height = allocation->height;
}

/* build edit person widgets, return a pointer to the main container of the widgetset (a vbox) */
static GtkWidget* addressbook_edit_person_widgets_create( GtkWidget* container, gboolean *cancelled )
{
	GtkWidget *vbox;
	GtkWidget *vnbox;
	GtkWidget *notebook;
	GtkWidget *hbbox;
	GtkWidget *ok_btn;
	GtkWidget *cancel_btn;

	vbox = gtk_vbox_new(FALSE, 4);
	 gtk_container_set_border_width(GTK_CONTAINER(vbox), BORDER_WIDTH); 
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(container), vbox);

	vnbox = gtk_vbox_new(FALSE, 4);
	gtk_container_set_border_width(GTK_CONTAINER(vnbox), 4);
	gtk_widget_show(vnbox);
	gtk_box_pack_start(GTK_BOX(vbox), vnbox, TRUE, TRUE, 0);

	/* Notebook */
	notebook = gtk_notebook_new();
	gtk_widget_show(notebook);
	gtk_box_pack_start(GTK_BOX(vnbox), notebook, TRUE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(notebook), 6);

	/* Button panel */
	if (prefs_common.addressbook_use_editaddress_dialog)
	gtkut_stock_button_set_create(&hbbox, &cancel_btn, GTK_STOCK_CANCEL,
				      &ok_btn, GTK_STOCK_OK,
				      NULL, NULL);
	else
		gtkut_stock_with_text_button_set_create(&hbbox,
					  &cancel_btn, GTK_STOCK_CANCEL, _("Discard"),
				      &ok_btn, GTK_STOCK_OK, _("Apply"),
				      NULL, NULL, NULL);
	gtk_box_pack_end(GTK_BOX(vnbox), hbbox, FALSE, FALSE, 0);
	gtk_widget_grab_default(ok_btn);

	g_signal_connect(G_OBJECT(ok_btn), "clicked",
			 G_CALLBACK(edit_person_ok), cancelled);
	g_signal_connect(G_OBJECT(cancel_btn), "clicked",
			 G_CALLBACK(edit_person_cancel), cancelled);
	g_signal_connect(G_OBJECT(notebook), "switch_page",
			 G_CALLBACK(edit_person_switch_page), NULL );

	gtk_widget_show_all(vbox);

	personeditdlg.notebook   = notebook;
	personeditdlg.ok_btn     = ok_btn;
	personeditdlg.cancel_btn = cancel_btn;

	return vbox;
}

static void addressbook_edit_person_dialog_create( gboolean *cancelled ) {
	GtkWidget *window;
	GtkWidget *hsbox;
	GtkWidget *vbox;
	GtkWidget *statusbar;
	static GdkGeometry geometry;

	window = gtkut_window_new(GTK_WINDOW_TOPLEVEL, "editaddress");
	/* gtk_container_set_border_width(GTK_CONTAINER(window), 0); */
	gtk_window_set_title(GTK_WINDOW(window), _("Edit Person Data"));
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_set_modal(GTK_WINDOW(window), TRUE);	
	g_signal_connect(G_OBJECT(window), "delete_event",
			 G_CALLBACK(edit_person_delete_event),
			 cancelled);
	g_signal_connect(G_OBJECT(window), "size_allocate",
			 G_CALLBACK(edit_person_size_allocate_cb),
			cancelled);
	g_signal_connect(G_OBJECT(window), "key_press_event",
			 G_CALLBACK(edit_person_key_pressed),
			 cancelled);

	vbox = addressbook_edit_person_widgets_create(window, cancelled);

	/* Status line */
	hsbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), hsbox, FALSE, FALSE, BORDER_WIDTH);
	statusbar = gtk_statusbar_new();
	gtk_box_pack_start(GTK_BOX(hsbox), statusbar, TRUE, TRUE, BORDER_WIDTH);

	if (!geometry.min_height) {
		geometry.min_width = EDITPERSON_WIDTH;
		geometry.min_height = EDITPERSON_HEIGHT;
	}

	gtk_window_set_geometry_hints(GTK_WINDOW(window), NULL, &geometry,
				      GDK_HINT_MIN_SIZE);
	gtk_widget_set_size_request(window, prefs_common.addressbookeditpersonwin_width,
				    prefs_common.addressbookeditpersonwin_height);

	personeditdlg.container  = window;
	personeditdlg.statusbar  = statusbar;
	personeditdlg.status_cid = gtk_statusbar_get_context_id( GTK_STATUSBAR(statusbar), "Edit Person Dialog" );

}

/* parent must be a box */
static void addressbook_edit_person_widgetset_create( GtkWidget *parent, gboolean *cancelled )
{
	GtkWidget *vbox;
	GtkWidget *label;

	if ( parent == NULL )
		g_warning("addressbook_edit_person_widgetset_create: parent is NULL");

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_end(GTK_BOX(parent), vbox, TRUE, TRUE, 0);

	label = gtk_label_new(_("Edit Person Data"));
	gtk_label_set_justify( GTK_LABEL(label), GTK_JUSTIFY_CENTER);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	addressbook_edit_person_widgets_create(vbox, cancelled);

	gtk_widget_set_size_request(vbox, EDITPERSON_WIDTH, EDITPERSON_HEIGHT);

	personeditdlg.container = vbox;
	personeditdlg.title = label;
	personeditdlg.statusbar  = NULL;
	personeditdlg.status_cid = 0;
}

void addressbook_edit_person_widgetset_hide( void )
{
	if ( personeditdlg.container )
		gtk_widget_hide( personeditdlg.container );
}

static void addressbook_edit_person_unset_picture (void *obj, guint action, void *data)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 48, 48);
	gdk_pixbuf_fill(pixbuf, 0xffffff00);
	personeditdlg.picture_set = FALSE;
	gtk_image_set_from_pixbuf(GTK_IMAGE(personeditdlg.image), pixbuf);
	g_object_unref(pixbuf);
}

static GtkItemFactoryEntry editaddr_popup_entries[] =
{
	{N_("/_Unset picture"),		NULL, addressbook_edit_person_unset_picture, 0, NULL},
};

static void addressbook_edit_person_set_picture(GtkWidget *widget, 
		GdkEventButton *event, gpointer data)
{
	GError *error = NULL;
	gchar *filename;
	int width, height, scalewidth, scaleheight;
	
	if (event->button == 1) {
		if ( (filename = filesel_select_file_open(_("Choose a picture"), NULL)) ) {
			GdkPixbuf *pixbuf = NULL;
			gdk_pixbuf_get_file_info(filename, &width, &height);

			if ( width > 128 || height > 128 ) {
				if (width > height) {
					scaleheight = (height * 128) / width;
					scalewidth = 128;
				}
				else {
					scalewidth = (width * 128) / height;
					scaleheight = 128;
				}
				pixbuf = gdk_pixbuf_new_from_file_at_scale(filename, 
						scalewidth, scaleheight, TRUE, &error);
			} else {
				pixbuf = gdk_pixbuf_new_from_file(filename, &error);
			}
			if (error) {
				alertpanel_error(_("Failed to import image: \n%s"),
						error->message);
				g_error_free(error);
			}
			personeditdlg.picture_set = TRUE;
			g_free(filename);
			gtk_image_set_from_pixbuf(GTK_IMAGE(personeditdlg.image), pixbuf);
			g_object_unref(pixbuf);
		}
	} else {
		gtk_menu_popup(GTK_MENU(personeditdlg.editaddr_popupmenu), 
			       NULL, NULL, NULL, NULL, 
			       event->button, event->time);
	}
}

static void addressbook_edit_person_page_basic( gint pageNum, gchar *pageLbl ) {
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *table;
	GtkWidget *label;
	GtkWidget *ebox_picture;
	GtkWidget *frame_picture;
	GtkWidget *entry_name;
	GtkWidget *entry_fn;
	GtkWidget *entry_ln;
	GtkWidget *entry_nn;
	const gchar *locale;
	gint top = 0;
	gint n_entries;
	GdkPixbuf *pixbuf;
	vbox = gtk_vbox_new( FALSE, 20 );
	hbox = gtk_hbox_new( FALSE, 8 );

	gtk_widget_show( vbox );	
	
	/* User's picture */
	ebox_picture = gtk_event_box_new();
	frame_picture = gtk_frame_new("Photo");
	
	/* Room for a photo */
	pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 48, 48);
	gdk_pixbuf_fill(pixbuf, 0xffffff00);
	personeditdlg.picture_set = FALSE;
	personeditdlg.image = gtk_image_new_from_pixbuf(pixbuf);
	g_object_unref(pixbuf);

	gtk_container_add(GTK_CONTAINER(ebox_picture), personeditdlg.image);
	gtk_container_add(GTK_CONTAINER(frame_picture), ebox_picture);	
	gtk_container_add(GTK_CONTAINER( personeditdlg.notebook ), hbox );
	gtk_container_set_border_width( GTK_CONTAINER (vbox), BORDER_WIDTH );
	gtk_container_set_border_width( GTK_CONTAINER (hbox), BORDER_WIDTH );

	label = gtk_label_new_with_mnemonic( pageLbl );
	gtk_widget_show( label );
	
	gtk_box_pack_start(GTK_BOX(hbox), frame_picture, TRUE, TRUE, 0);
	
	gtk_notebook_set_tab_label(
		GTK_NOTEBOOK( personeditdlg.notebook ),
		gtk_notebook_get_nth_page( GTK_NOTEBOOK( personeditdlg.notebook ), pageNum ), label );
	
	g_signal_connect(G_OBJECT(ebox_picture), "button_press_event", 
			G_CALLBACK(addressbook_edit_person_set_picture), NULL);

	n_entries = sizeof(editaddr_popup_entries) /
		sizeof(editaddr_popup_entries[0]);
	personeditdlg.editaddr_popupmenu = menu_create_items(editaddr_popup_entries, n_entries,
				      "<EditAddrPopupMenu>", &personeditdlg.editaddr_popupfactory,
				      NULL);

	table = gtk_table_new( 3, 3, FALSE);

#define ATTACH_ROW(text, entry) \
{ \
	label = gtk_label_new(text); \
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, top, (top + 1), \
			 GTK_FILL, 0, 0, 0); \
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5); \
 \
	entry = gtk_entry_new(); \
	gtk_table_attach(GTK_TABLE(table), entry, 1, 2, top, (top + 1), \
			 GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0); \
	top++; \
}

#define ATTACH_HIDDEN_ROW(text, entry) \
{ \
	entry = gtk_entry_new(); \
}

#ifndef MAEMO
	ATTACH_ROW(_("Display Name"), entry_name);
#else
	ATTACH_HIDDEN_ROW(_("Display Name"), entry_name);
#endif
	locale = conv_get_current_locale();
	if (locale &&
	    (!g_ascii_strncasecmp(locale, "ja", 2) ||
	     !g_ascii_strncasecmp(locale, "ko", 2) ||
	     !g_ascii_strncasecmp(locale, "zh", 2))) {
		ATTACH_ROW(_("Last Name"), entry_ln);
		ATTACH_ROW(_("First Name"), entry_fn);
	} else {
		ATTACH_ROW(_("First Name"), entry_fn);
		ATTACH_ROW(_("Last Name"), entry_ln);
	}
#ifndef MAEMO
	ATTACH_ROW(_("Nickname"), entry_nn);
#else
	ATTACH_HIDDEN_ROW(_("Nickname"), entry_nn);
#endif

#undef ATTACH_ROW
#undef ATTACH_HIDDEN_ROW
	gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
	gtk_container_set_border_width( GTK_CONTAINER(table), 8 );
	gtk_table_set_row_spacings(GTK_TABLE(table), 15);
	gtk_table_set_col_spacings(GTK_TABLE(table), 8);

	gtk_widget_show_all(vbox);
	personeditdlg.entry_name  = entry_name;
	personeditdlg.entry_first = entry_fn;
	personeditdlg.entry_last  = entry_ln;
	personeditdlg.entry_nick  = entry_nn;
}

static gboolean email_adding = FALSE, email_saving = FALSE;

static void edit_person_entry_email_changed (GtkWidget *entry, gpointer data)
{
	gboolean non_empty = gtk_clist_get_row_data(GTK_CLIST(personeditdlg.clist_email), 0) != NULL;

	if (personeditdlg.read_only)
		return;

	if (gtk_entry_get_text(GTK_ENTRY(personeditdlg.entry_email)) == NULL
	||  strlen(gtk_entry_get_text(GTK_ENTRY(personeditdlg.entry_email))) == 0) {
		gtk_widget_set_sensitive(personeditdlg.email_add,FALSE);
		gtk_widget_set_sensitive(personeditdlg.email_mod,FALSE);
		email_adding = FALSE;
		email_saving = FALSE;
	} else if (list_find_email(gtk_entry_get_text(GTK_ENTRY(personeditdlg.entry_email)))) {
		gtk_widget_set_sensitive(personeditdlg.email_add,FALSE);
		gtk_widget_set_sensitive(personeditdlg.email_mod,non_empty);
		email_adding = FALSE;
		email_saving = non_empty;
	} else {
		gtk_widget_set_sensitive(personeditdlg.email_add,TRUE);
		gtk_widget_set_sensitive(personeditdlg.email_mod,non_empty);
		email_adding = TRUE;
		email_saving = non_empty;
	}
}

static gboolean edit_person_entry_email_pressed(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	if (event && event->keyval == GDK_Return) {
		if (email_saving)
			edit_person_email_modify(NULL);		
		else if (email_adding)
			edit_person_email_add(NULL);
	}
	return FALSE;
}


static void addressbook_edit_person_page_email( gint pageNum, gchar *pageLbl ) {
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *vboxl;
	GtkWidget *vboxb;
	GtkWidget *vbuttonbox;
	GtkWidget *buttonUp;
	GtkWidget *buttonDown;
	GtkWidget *buttonDel;
	GtkWidget *buttonMod;
	GtkWidget *buttonAdd;

	GtkWidget *table;
	GtkWidget *label;
	GtkWidget *clist_swin;
	GtkWidget *clist;
	GtkWidget *entry_email;
	GtkWidget *entry_alias;
	GtkWidget *entry_remarks;
	gint top;

	gchar *titles[ EMAIL_N_COLS ];
	gint i;

	titles[ EMAIL_COL_EMAIL   ] = _("Email Address");
#ifndef MAEMO
	titles[ EMAIL_COL_ALIAS   ] = _("Alias");
	titles[ EMAIL_COL_REMARKS ] = _("Remarks");
#endif
	vbox = gtk_vbox_new( FALSE, 8 );
	gtk_widget_show( vbox );
	gtk_container_add( GTK_CONTAINER( personeditdlg.notebook ), vbox );
	gtk_container_set_border_width( GTK_CONTAINER (vbox), BORDER_WIDTH );

	label = gtk_label_new_with_mnemonic( pageLbl );
	gtk_widget_show( label );
	gtk_notebook_set_tab_label(
		GTK_NOTEBOOK( personeditdlg.notebook ),
		gtk_notebook_get_nth_page( GTK_NOTEBOOK( personeditdlg.notebook ), pageNum ), label );

	/* Split into two areas */
	hbox = gtk_hbox_new( FALSE, 0 );
	gtk_container_add( GTK_CONTAINER( vbox ), hbox );

	/* Address list */
	vboxl = gtk_vbox_new( FALSE, 4 );
	gtk_container_add( GTK_CONTAINER( hbox ), vboxl );
	gtk_container_set_border_width( GTK_CONTAINER(vboxl), 4 );

	clist_swin = gtk_scrolled_window_new( NULL, NULL );
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(clist_swin),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);

	clist = gtk_clist_new_with_titles( EMAIL_N_COLS, titles );

	gtk_container_add( GTK_CONTAINER(clist_swin), clist );
	gtk_clist_set_selection_mode( GTK_CLIST(clist), GTK_SELECTION_BROWSE );
	gtk_clist_set_column_width( GTK_CLIST(clist), EMAIL_COL_EMAIL, EMAIL_COL_WIDTH_EMAIL );
	gtk_clist_set_column_width( GTK_CLIST(clist), EMAIL_COL_ALIAS, EMAIL_COL_WIDTH_ALIAS );

	for( i = 0; i < EMAIL_N_COLS; i++ )
		GTK_WIDGET_UNSET_FLAGS(GTK_CLIST(clist)->column[i].button, GTK_CAN_FOCUS);

	/* Data entry area */
	table = gtk_table_new( 4, 2, FALSE);

#ifndef MAEMO
	gtk_container_add( GTK_CONTAINER(vboxl), clist_swin );
	gtk_box_pack_start(GTK_BOX(vboxl), table, FALSE, FALSE, 0);
#else
	gtk_box_pack_start(GTK_BOX(vboxl), table, FALSE, FALSE, 0);
	gtk_container_add( GTK_CONTAINER(vboxl), clist_swin );
	gtk_clist_column_titles_hide(GTK_CLIST(clist));
#endif
	gtk_container_set_border_width( GTK_CONTAINER(table), 4 );
	gtk_table_set_row_spacings(GTK_TABLE(table), 4);
	gtk_table_set_col_spacings(GTK_TABLE(table), 4);

	entry_email = gtk_entry_new();
	entry_alias = gtk_entry_new();
	entry_remarks = gtk_entry_new();

	/* First row */
	top = 0;
	label = gtk_label_new(_("Email Address"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, top, (top + 1), GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

	gtk_table_attach(GTK_TABLE(table), entry_email, 1, 2, top, (top + 1), GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);

#ifndef MAEMO
	/* Next row */
	++top;
	label = gtk_label_new(_("Alias"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, top, (top + 1), GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

	gtk_table_attach(GTK_TABLE(table), entry_alias, 1, 2, top, (top + 1), GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);

	/* Next row */
	++top;
	label = gtk_label_new(_("Remarks"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, top, (top + 1), GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

	gtk_table_attach(GTK_TABLE(table), entry_remarks, 1, 2, top, (top + 1), GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);
#endif

	/* Button box */
	vboxb = gtk_vbox_new( FALSE, 4 );
	gtk_box_pack_start(GTK_BOX(hbox), vboxb, FALSE, FALSE, 2);

	vbuttonbox = gtk_vbutton_box_new();
	gtk_button_box_set_layout( GTK_BUTTON_BOX(vbuttonbox), GTK_BUTTONBOX_START );
	gtk_box_set_spacing( GTK_BOX(vbuttonbox), 8 );
	gtk_container_set_border_width( GTK_CONTAINER(vbuttonbox), 4 );
	gtk_container_add( GTK_CONTAINER(vboxb), vbuttonbox );

	/* Buttons */
	buttonUp = gtk_button_new_from_stock(GTK_STOCK_GO_UP);
	buttonDown = gtk_button_new_from_stock(GTK_STOCK_GO_DOWN);
	buttonDel = gtk_button_new_from_stock(GTK_STOCK_DELETE);
	buttonMod = gtk_button_new_from_stock(GTK_STOCK_SAVE);
	buttonAdd = gtk_button_new_from_stock(GTK_STOCK_ADD);
	

#ifndef MAEMO
	gtk_container_add( GTK_CONTAINER(vbuttonbox), buttonUp );

	gtk_container_add( GTK_CONTAINER(vbuttonbox), buttonDown );
#endif
	gtk_container_add( GTK_CONTAINER(vbuttonbox), buttonDel );

	gtk_container_add( GTK_CONTAINER(vbuttonbox), buttonMod );

	gtk_container_add( GTK_CONTAINER(vbuttonbox), buttonAdd );

	gtk_widget_show_all(vbox);

	/* Event handlers */
	g_signal_connect( G_OBJECT(clist), "select_row",
			  G_CALLBACK( edit_person_email_list_selected), NULL );
	g_signal_connect( G_OBJECT(buttonUp), "clicked",
			  G_CALLBACK( edit_person_email_move_up ), NULL );
	g_signal_connect( G_OBJECT(buttonDown), "clicked",
			  G_CALLBACK( edit_person_email_move_down ), NULL );
	g_signal_connect( G_OBJECT(buttonDel), "clicked",
			  G_CALLBACK( edit_person_email_delete ), NULL );
	g_signal_connect( G_OBJECT(buttonMod), "clicked",
			  G_CALLBACK( edit_person_email_modify ), NULL );
	g_signal_connect( G_OBJECT(buttonAdd), "clicked",
			  G_CALLBACK( edit_person_email_add ), NULL );
	g_signal_connect(G_OBJECT(entry_email), "changed",
			 G_CALLBACK(edit_person_entry_email_changed), NULL);
	g_signal_connect(G_OBJECT(entry_email), "key_press_event",
			 G_CALLBACK(edit_person_entry_email_pressed), NULL);
	g_signal_connect(G_OBJECT(entry_alias), "key_press_event",
			 G_CALLBACK(edit_person_entry_email_pressed), NULL);
	g_signal_connect(G_OBJECT(entry_remarks), "key_press_event",
			 G_CALLBACK(edit_person_entry_email_pressed), NULL);

	personeditdlg.clist_email   = clist;
	personeditdlg.entry_email   = entry_email;
	personeditdlg.entry_alias   = entry_alias;
	personeditdlg.entry_remarks = entry_remarks;
	personeditdlg.email_up = buttonUp;
	personeditdlg.email_down = buttonDown;
	personeditdlg.email_del = buttonDel;
	personeditdlg.email_mod = buttonMod;
	personeditdlg.email_add = buttonAdd;
}

static gboolean attrib_adding = FALSE, attrib_saving = FALSE;

static void edit_person_entry_att_changed (GtkWidget *entry, gpointer data)
{
	gboolean non_empty = gtk_clist_get_row_data(GTK_CLIST(personeditdlg.clist_attrib), 0) != NULL;

	if (personeditdlg.read_only || personeditdlg.ldap)
		return;

	if (gtk_entry_get_text(GTK_ENTRY(personeditdlg.entry_atname)) == NULL
	||  strlen(gtk_entry_get_text(GTK_ENTRY(personeditdlg.entry_atname))) == 0) {
		gtk_widget_set_sensitive(personeditdlg.attrib_add,FALSE);
		gtk_widget_set_sensitive(personeditdlg.attrib_mod,FALSE);
		attrib_adding = FALSE;
		attrib_saving = FALSE;
	} else if (list_find_attribute(gtk_entry_get_text(GTK_ENTRY(personeditdlg.entry_atname)))) {
		gtk_widget_set_sensitive(personeditdlg.attrib_add,FALSE);
		gtk_widget_set_sensitive(personeditdlg.attrib_mod,non_empty);
		attrib_adding = FALSE;
		attrib_saving = non_empty;
	} else {
		gtk_widget_set_sensitive(personeditdlg.attrib_add,TRUE);
		gtk_widget_set_sensitive(personeditdlg.attrib_mod,non_empty);
		attrib_adding = TRUE;
		attrib_saving = non_empty;
	}
}

static gboolean edit_person_entry_att_pressed(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	if (event && event->keyval == GDK_Return) {
		if (attrib_saving)
			edit_person_attrib_modify(NULL);
		else if (attrib_adding)
			edit_person_attrib_add(NULL);
	}
	return FALSE;
}

static void addressbook_edit_person_page_attrib( gint pageNum, gchar *pageLbl ) {
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *vboxl;
	GtkWidget *vboxb;
	GtkWidget *vbuttonbox;
	GtkWidget *buttonDel;
	GtkWidget *buttonMod;
	GtkWidget *buttonAdd;

	GtkWidget *table;
	GtkWidget *label;
	GtkWidget *clist_swin;
	GtkWidget *clist;
	GtkWidget *entry_name;
	GtkWidget *entry_value;
	gint top;

	gchar *titles[ ATTRIB_N_COLS ];
	gint i;

	titles[ ATTRIB_COL_NAME  ] = _("Name");
	titles[ ATTRIB_COL_VALUE ] = _("Value");

	vbox = gtk_vbox_new( FALSE, 8 );
	gtk_widget_show( vbox );
	gtk_container_add( GTK_CONTAINER( personeditdlg.notebook ), vbox );
	gtk_container_set_border_width( GTK_CONTAINER (vbox), BORDER_WIDTH );

	label = gtk_label_new_with_mnemonic( pageLbl );
	gtk_widget_show( label );
	gtk_notebook_set_tab_label(
		GTK_NOTEBOOK( personeditdlg.notebook ),
		gtk_notebook_get_nth_page( GTK_NOTEBOOK( personeditdlg.notebook ), pageNum ), label );

	/* Split into two areas */
	hbox = gtk_hbox_new( FALSE, 0 );
	gtk_container_add( GTK_CONTAINER( vbox ), hbox );

	/* Attribute list */
	vboxl = gtk_vbox_new( FALSE, 4 );
	gtk_container_add( GTK_CONTAINER( hbox ), vboxl );
	gtk_container_set_border_width( GTK_CONTAINER(vboxl), 4 );

	clist_swin = gtk_scrolled_window_new( NULL, NULL );
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(clist_swin),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);

	clist = gtk_clist_new_with_titles( ATTRIB_N_COLS, titles );
	gtk_container_add( GTK_CONTAINER(clist_swin), clist );
	gtk_clist_set_selection_mode( GTK_CLIST(clist), GTK_SELECTION_BROWSE );
	gtk_clist_set_compare_func( GTK_CLIST(clist), edit_person_attrib_compare_func );
	gtk_clist_set_auto_sort( GTK_CLIST(clist), TRUE );
	gtk_clist_set_column_width( GTK_CLIST(clist), ATTRIB_COL_NAME, ATTRIB_COL_WIDTH_NAME );
	gtk_clist_set_column_width( GTK_CLIST(clist), ATTRIB_COL_VALUE, ATTRIB_COL_WIDTH_VALUE );

	for( i = 0; i < ATTRIB_N_COLS; i++ )
		GTK_WIDGET_UNSET_FLAGS(GTK_CLIST(clist)->column[i].button, GTK_CAN_FOCUS);

	/* Data entry area */
#ifndef MAEMO
	table = gtk_table_new( 4, 2, FALSE);
	gtk_container_add( GTK_CONTAINER(vboxl), clist_swin );
	gtk_box_pack_start(GTK_BOX(vboxl), table, FALSE, FALSE, 0);
#else
	table = gtk_table_new( 2, 4, FALSE);
	gtk_box_pack_start(GTK_BOX(vboxl), table, FALSE, FALSE, 0);
	gtk_container_add( GTK_CONTAINER(vboxl), clist_swin );
	gtk_clist_column_titles_hide(GTK_CLIST(clist));
#endif
	gtk_container_set_border_width( GTK_CONTAINER(table), 4 );
	gtk_table_set_row_spacings(GTK_TABLE(table), 4);
	gtk_table_set_col_spacings(GTK_TABLE(table), 4);

	/* First row */
	top = 0;
#ifndef MAEMO
	label = gtk_label_new(_("Name"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, top, (top + 1), GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

	entry_name = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table), entry_name, 1, 2, top, (top + 1), GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);

	/* Next row */
	++top;
	label = gtk_label_new(_("Value"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, top, (top + 1), GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

	entry_value = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table), entry_value, 1, 2, top, (top + 1), GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);
#else
	label = gtk_label_new(_("Name"));
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

	entry_name = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table), entry_name, 1, 2, 0, 1, GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);

	/* Next row */
	++top;
	label = gtk_label_new(_("Value"));
	gtk_table_attach(GTK_TABLE(table), label, 2, 3, 0, 1, GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

	entry_value = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table), entry_value, 3, 4, 0, 1, GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);
#endif
	/* Button box */
	vboxb = gtk_vbox_new( FALSE, 4 );
	gtk_box_pack_start(GTK_BOX(hbox), vboxb, FALSE, FALSE, 2);

	vbuttonbox = gtk_vbutton_box_new();
	gtk_button_box_set_layout( GTK_BUTTON_BOX(vbuttonbox), GTK_BUTTONBOX_START );
	gtk_box_set_spacing( GTK_BOX(vbuttonbox), 8 );
	gtk_container_set_border_width( GTK_CONTAINER(vbuttonbox), 4 );
	gtk_container_add( GTK_CONTAINER(vboxb), vbuttonbox );

	/* Buttons */
	buttonDel = gtk_button_new_from_stock(GTK_STOCK_DELETE);
	gtk_container_add( GTK_CONTAINER(vbuttonbox), buttonDel );

	buttonMod = gtk_button_new_from_stock(GTK_STOCK_SAVE);
	gtk_container_add( GTK_CONTAINER(vbuttonbox), buttonMod );

	buttonAdd = gtk_button_new_from_stock(GTK_STOCK_ADD);
	gtk_container_add( GTK_CONTAINER(vbuttonbox), buttonAdd );
	
	gtk_widget_set_sensitive(buttonDel,FALSE);
	gtk_widget_set_sensitive(buttonMod,FALSE);
	gtk_widget_set_sensitive(buttonAdd,FALSE);

	gtk_widget_show_all(vbox);

	/* Event handlers */
	g_signal_connect( G_OBJECT(clist), "select_row",
			  G_CALLBACK( edit_person_attrib_list_selected), NULL );
	g_signal_connect( G_OBJECT(buttonDel), "clicked",
			  G_CALLBACK( edit_person_attrib_delete ), NULL );
	g_signal_connect( G_OBJECT(buttonMod), "clicked",
			  G_CALLBACK( edit_person_attrib_modify ), NULL );
	g_signal_connect( G_OBJECT(buttonAdd), "clicked",
			  G_CALLBACK( edit_person_attrib_add ), NULL );
	g_signal_connect(G_OBJECT(entry_name), "changed",
			 G_CALLBACK(edit_person_entry_att_changed), NULL);
	g_signal_connect(G_OBJECT(entry_name), "key_press_event",
			 G_CALLBACK(edit_person_entry_att_pressed), NULL);
	g_signal_connect(G_OBJECT(entry_value), "key_press_event",
			 G_CALLBACK(edit_person_entry_att_pressed), NULL);

	personeditdlg.clist_attrib  = clist;
	personeditdlg.entry_atname  = entry_name;
	personeditdlg.entry_atvalue = entry_value;
	personeditdlg.attrib_add = buttonAdd;
	personeditdlg.attrib_del = buttonDel;
	personeditdlg.attrib_mod = buttonMod;
}

static void addressbook_edit_person_create( GtkWidget *parent, gboolean *cancelled ) {
	if (prefs_common.addressbook_use_editaddress_dialog)
		addressbook_edit_person_dialog_create( cancelled );
	else
		addressbook_edit_person_widgetset_create( parent, cancelled );
	addressbook_edit_person_page_basic( PAGE_BASIC, _( "_User Data" ) );
	addressbook_edit_person_page_email( PAGE_EMAIL, _( "_Email Addresses" ) );
#ifdef USE_LDAP
	if (personeditdlg.ldap)
		addressbook_edit_person_page_attrib_ldap(&personeditdlg, PAGE_ATTRIBUTES, _("O_ther Attributes"));
	else
#endif
		addressbook_edit_person_page_attrib( PAGE_ATTRIBUTES, _( "O_ther Attributes" ) );
	gtk_widget_show_all( personeditdlg.container );
}

/*
* Return list of email items.
*/
static GList *edit_person_build_email_list() {
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_email);
	GList *listEMail = NULL;
	ItemEMail *email;
	gint row = 0;
	while( (email = gtk_clist_get_row_data( clist, row )) ) {
		listEMail = g_list_append( listEMail, email );
		row++;
	}
	return listEMail;
}

/*
* Return list of attributes.
*/
static GList *edit_person_build_attrib_list() {
	GtkCList *clist = GTK_CLIST(personeditdlg.clist_attrib);
	GList *listAttrib = NULL;
	UserAttribute *attrib;
	gint row = 0;
	while( (attrib = gtk_clist_get_row_data( clist, row )) ) {
		listAttrib = g_list_append( listAttrib, attrib );
		row++;
	}
	return listAttrib;
}

static void update_sensitivity(void)
{
	gtk_widget_set_sensitive(personeditdlg.entry_name,    !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.entry_first,   !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.entry_last,    !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.entry_nick,    !personeditdlg.read_only && !personeditdlg.ldap);
	gtk_widget_set_sensitive(personeditdlg.entry_email,   !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.entry_alias,   !personeditdlg.read_only && !personeditdlg.ldap);
	gtk_widget_set_sensitive(personeditdlg.entry_remarks, !personeditdlg.read_only && !personeditdlg.ldap);
	gtk_widget_set_sensitive(personeditdlg.email_up,      !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.email_down,    !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.email_del,     !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.email_mod,     !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.email_add,     !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.entry_atname,  !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.entry_atvalue, !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.attrib_add,    !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.attrib_del,    !personeditdlg.read_only);
	gtk_widget_set_sensitive(personeditdlg.attrib_mod,    !personeditdlg.read_only);
}

static void addressbook_edit_person_flush_transient( void )
{
	ItemPerson *person = current_person;
	EditAddressPostUpdateCallback callback = edit_person_close_post_update_cb;

	/* reset transient data */
	current_abf = NULL;
	current_person = NULL;
	current_parent_folder = NULL;
	edit_person_close_post_update_cb = NULL;

	/* post action to perform on addressbook side */
	if (callback)
		callback( person );
}

void addressbook_edit_person_invalidate( AddressBookFile *abf, ItemFolder *parent_folder,
										 ItemPerson *person )
{
	if (current_abf == NULL &&
		current_person == NULL &&
		current_parent_folder == NULL)
		/* edit address form is already hidden */
		return;

	/* unconditional invalidation or invalidating the currently edited item */
	if ( ( abf == NULL && person == NULL && parent_folder == NULL )
		|| (current_abf == abf ||
			current_person == person ||
			current_parent_folder == parent_folder))
		addressbook_edit_person_close( TRUE );
}

static gboolean addressbook_edit_person_close( gboolean cancelled )
{
	GList *listEMail = NULL;
	GList *listAttrib = NULL;
	GError *error;
	gchar *cn = NULL;

	listEMail = edit_person_build_email_list();
	listAttrib = edit_person_build_attrib_list();
	if( cancelled ) {
		addritem_free_list_email( listEMail );
		addritem_free_list_attribute( listAttrib );
		gtk_clist_clear( GTK_CLIST(personeditdlg.clist_email) );
		gtk_clist_clear( GTK_CLIST(personeditdlg.clist_attrib) );

		if (!prefs_common.addressbook_use_editaddress_dialog)
			gtk_widget_hide( personeditdlg.container );

		/* no callback, as we're discarding the form */
		edit_person_close_post_update_cb = NULL;
		addressbook_edit_person_flush_transient();

		/* set focus to the address list (this is done by the post_update_cb usually) */
		addressbook_address_list_set_focus();
		return FALSE;
	}

#ifndef MAEMO
	cn = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_name), 0, -1 );
#else
	{
		gchar *first = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_first), 0, -1 );
		gchar *last = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_last), 0, -1 );
		cn = g_strdup_printf("%s%s%s", first, (first && last && *first && *last)?" ":"", last);
		g_free(first);
		g_free(last);
	}
#endif
	if( current_person && current_abf ) {
		/* Update email/attribute list for existing current_person */
		addrbook_update_address_list( current_abf, current_person, listEMail );
		addrbook_update_attrib_list( current_abf, current_person, listAttrib );
	}
	else {
		/* Create new current_person and email/attribute list */
		if( cn == NULL || *cn == '\0' ) {
			/* Wasting our time */
			if( listEMail == NULL && listAttrib == NULL ) cancelled = TRUE;
		}
		if( ! cancelled && current_abf ) {
			current_person = addrbook_add_address_list( current_abf, current_parent_folder, listEMail );
			addrbook_add_attrib_list( current_abf, current_person, listAttrib );
		}
	}
	listEMail = NULL;
	listAttrib = NULL;

	if( ! cancelled ) {
		/* Set current_person stuff */		

		gchar *name;
		addritem_person_set_common_name( current_person, cn );
		if (personeditdlg.picture_set) {
			GdkPixbuf * pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(personeditdlg.image));
			name = g_strconcat( get_rc_dir(), G_DIR_SEPARATOR_S, ADDRBOOK_DIR, G_DIR_SEPARATOR_S, 
							ADDRITEM_ID(current_person), ".png", NULL );
			gdk_pixbuf_save(pixbuf, name, "png", &error, NULL);
			addritem_person_set_picture( current_person, ADDRITEM_ID(current_person) ) ;
			g_free( name );
		} else {
			name = g_strconcat( get_rc_dir(), G_DIR_SEPARATOR_S, ADDRBOOK_DIR, G_DIR_SEPARATOR_S, 
							ADDRITEM_ID(current_person), ".png", NULL );
			g_unlink(name);
			g_free(name);
		}
		name = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_first), 0, -1 );
		addritem_person_set_first_name( current_person, name );
		g_free( name );
		name = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_last), 0, -1 );
		addritem_person_set_last_name( current_person, name );
		g_free( name );
		name = gtk_editable_get_chars( GTK_EDITABLE(personeditdlg.entry_nick), 0, -1 );
		addritem_person_set_nick_name( current_person, name );
		g_free( name );
	}
	g_free( cn );

	gtk_clist_clear( GTK_CLIST(personeditdlg.clist_email) );
	gtk_clist_clear( GTK_CLIST(personeditdlg.clist_attrib) );

	if (!prefs_common.addressbook_use_editaddress_dialog)
		gtk_widget_hide( personeditdlg.container );

	addressbook_edit_person_flush_transient();

	return TRUE;
}
 
/*
* Edit person.
* Enter: abf    Address book.
*        parent Parent folder for person (or NULL if adding to root folder). Argument is
*               only required for new objects).
*        person Person to edit, or NULL for a new person object.
*        pgMail If TRUE, E-Mail page will be activated.
* Return: Edited object, or NULL if cancelled.*/
ItemPerson *addressbook_edit_person( AddressBookFile *abf, ItemFolder *parent_folder, ItemPerson *person,
									 gboolean pgMail, GtkWidget *parent_container,
									 void (*post_update_cb) (ItemPerson *person),
									 gboolean get_focus) {
	static gboolean cancelled;
	GError *error = NULL;
	GdkPixbuf *pixbuf;
	/* set transient data */
	current_abf = abf;
	current_person = person;
	current_parent_folder = parent_folder;
	edit_person_close_post_update_cb = post_update_cb;
	personeditdlg.ldap = (abf && abf->type == ADBOOKTYPE_LDAP)? TRUE : FALSE;

	if( personeditdlg.container ) {
		gtk_widget_destroy(personeditdlg.container);
		personeditdlg.container = NULL;
	}
	addressbook_edit_person_create(parent_container, &cancelled);

	/* typically, get focus when dialog mode is enabled, or when editing a new address */
	if( get_focus ) {
		gtk_widget_grab_focus(personeditdlg.ok_btn);
		gtk_widget_grab_focus(personeditdlg.entry_name);
	}
	
	personeditdlg.read_only = (current_abf == NULL);
	update_sensitivity();

	gtk_widget_show(personeditdlg.container);
	if (prefs_common.addressbook_use_editaddress_dialog)
		manage_window_set_transient(GTK_WINDOW(personeditdlg.container));
	else
		if (get_focus)
			addressbook_address_list_disable_some_actions();

	/* Clear all fields */
	personeditdlg.rowIndEMail = -1;
	personeditdlg.rowIndAttrib = -1;
	edit_person_status_show( "" );
	gtk_clist_clear( GTK_CLIST(personeditdlg.clist_email) );
	gtk_clist_clear( GTK_CLIST(personeditdlg.clist_attrib) );
	gtk_entry_set_text(GTK_ENTRY(personeditdlg.entry_name), "" );
	gtk_entry_set_text(GTK_ENTRY(personeditdlg.entry_first), "" );
	gtk_entry_set_text(GTK_ENTRY(personeditdlg.entry_last), "" );
	gtk_entry_set_text(GTK_ENTRY(personeditdlg.entry_nick), "" );

	personeditdlg.editNew = FALSE;
	if( current_person ) {
		if( ADDRITEM_NAME(current_person) )
			gtk_entry_set_text(GTK_ENTRY(personeditdlg.entry_name), ADDRITEM_NAME(person) );

		if( current_person->picture ) {	
			gchar *filename = g_strconcat( get_rc_dir(), G_DIR_SEPARATOR_S, ADDRBOOK_DIR, G_DIR_SEPARATOR_S, 
							current_person->picture, ".png", NULL );
			if (is_file_exist(filename)) {
				pixbuf = gdk_pixbuf_new_from_file(filename, &error);
				if (error) {
					debug_print("Failed to import image: \n%s",
							error->message);
					g_error_free(error);
					goto no_img;
				}
				personeditdlg.picture_set = TRUE;
			} else {
				goto no_img;
			}
			g_free(filename);
		} else {
no_img:
			personeditdlg.picture_set = FALSE;
			pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 48, 48);
			gdk_pixbuf_fill(pixbuf, 0xffffff00);
		}
		gtk_image_set_from_pixbuf(GTK_IMAGE(personeditdlg.image), pixbuf);
		g_object_unref(pixbuf);
		pixbuf = NULL;

		if( current_person->firstName )
			gtk_entry_set_text(GTK_ENTRY(personeditdlg.entry_first), current_person->firstName );
		if( current_person->lastName )
			gtk_entry_set_text(GTK_ENTRY(personeditdlg.entry_last), current_person->lastName );
		if( current_person->nickName )
			gtk_entry_set_text(GTK_ENTRY(personeditdlg.entry_nick), current_person->nickName );
		edit_person_load_email( current_person );
		edit_person_load_attrib( current_person );
		gtk_entry_set_text(GTK_ENTRY(personeditdlg.entry_atvalue), "");
	}
	else {
		personeditdlg.editNew = TRUE;
	}

	/* Select appropriate start page */
	if( pgMail ) {
		gtk_notebook_set_current_page( GTK_NOTEBOOK(personeditdlg.notebook), PAGE_EMAIL );
	}
	else {
		gtk_notebook_set_current_page( GTK_NOTEBOOK(personeditdlg.notebook), PAGE_BASIC );
	}

	gtk_clist_select_row( GTK_CLIST(personeditdlg.clist_email), 0, 0 );
	gtk_clist_select_row( GTK_CLIST(personeditdlg.clist_attrib), 0, 0 );
	edit_person_email_clear( NULL );
	if (current_person)
		edit_person_email_list_selected(GTK_CLIST(personeditdlg.clist_email), 0, 0, NULL, NULL);

	edit_person_attrib_clear( NULL );

	if (prefs_common.addressbook_use_editaddress_dialog) {

	gtk_main();
		gtk_widget_hide( personeditdlg.container );

		if (!addressbook_edit_person_close( cancelled ))
			return NULL;
	}

	return current_person;
}

/*
* End of Source.
*/

