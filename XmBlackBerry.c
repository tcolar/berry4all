/*
	XmBlackBerry, Copyright (C) 2006  Rick Scott <rwscott@users.sourceforge.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/**\mainpage XmBlackBerry
Backup and restore BlackBerry device data via USB serial or bluetooth.
Access the GPRS modem via the USB.
*/
/** \file
	\brief The user interface

	This is the main User Interface
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>

#include <Xm/MainW.h>
#include <Xm/RowColumn.h>
#include <Xm/Notebook.h>
#include <Xm/List.h>
#include <Xm/Form.h>
#include <Xm/Scale.h>
#include <Xm/Label.h>
#include <Xm/FileSB.h>
#include <Xm/ToggleB.h>
#include <Xm/Protocols.h>
#include <Xm/RepType.h>
#include <Xm/ScrolledW.h>
#include <Xm/TextF.h>
#include <Xm/XpmP.h>

#include <Xlt/Xlt.h>
#include <Xlt/Stroke.h>
#include <Xlt/Sound.h>
#include <Xlt/SelectionBox.h>

#ifdef HAVE_OPENSYNC
#include "sync.h"
#else
static void sync_configure(void *info, uint32_t pin){return;}
static void *sync_init(Widget parent){return(NULL);}
static void sync_finalize(void *info){return;}
static Boolean sync_can_sync(void *env, uint32_t pin){return(False);}
static void sync_start(void *env, uint32_t pin, void (*done_callback)(void *call_data, int num_changes), void *client_data){return;}
#endif

#include "XmBlackBerry.h"
#include "serial.h"
#include "bb_usb.h"
#include "util.h"
#include "bb_proto.h"
#include "text_list.h"
#include "verify.h"
#include "bypass.h"
#include "view.h"

/*
#include "8700r_50x50.xpm"
*/
#include "bbicon.xpm"
#include "bbarrows-both.xpm"
#include "bbarrows-down.xpm"
#include "bbarrows-up.xpm"
#include "bbarrows-none.xpm"

static const char copyright[] = "\
	XmBlackBerry, Copyright (C) 2006  Rick Scott <rwscott@users.sourceforge.net>\n\
\n\
    This program is free software; you can redistribute it and/or modify\n\
    it under the terms of the GNU General Public License as published by\n\
    the Free Software Foundation.\n\
\n\
    This program is distributed in the hope that it will be useful,\n\
    but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n\
    GNU General Public License for more details.\n\
\n\
    You should have received a copy of the GNU General Public License\n\
    along with this program; if not, write to the Free Software\n\
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\n\
";

/**
	Hold onto the X stuff related to a device.
*/
typedef struct bb_rec {
    struct bb_rec *next;
    Widget selectionbox;
    XtInputId input_id;
    struct resources_rec *resources;
    BlackBerry_t *device;
    XtIntervalId stats_timer;
    BlackBerryStats_t stats; /**< The stats that were last displayed */
} bb_t;

typedef struct signalControl_rec {
    struct signalControl_rec *next;
    Boolean connected;
    uint32_t pin;
    BlackBerry_t *blackberry;
} signalControlStruct;

typedef enum {
    ActionSave,
    ActionSync,
    ActionRestore
} autoActions;

typedef struct resources_rec {
    XtAppContext context;
    Widget shell;
    Widget main_window;
    Widget menu_bar;
    Widget file_pulldown;
    Widget help_pulldown;
    Widget option_pulldown;
    Widget sync_pulldown;
    Widget notebook;
    Widget message;
    Widget progress;
    Widget current_page;

    int debug;
    String device_file;
    Boolean help;
    Boolean fallback;
    Boolean redirect_errors;
    String prefix;
    String autoSelectList;
    XtPointer auto_save_list;

    bb_t *widget_list;
    struct backupControl_rec *backup_control;
    XtSignalId driver_signal_id;
    signalControlStruct *signal_list;
    Boolean initialized;

    ListStruct monitor_list; /**< A list of extra file descriptors to monitor */

    void *sync_env;
    String syncGroup;
    int num_changes;

    autoActions auto_actions;

    Atom command_atom;
    String remote_command;
} resources_t, *resourcesPtr;

/**
	Used to control the backup/restore of a device's databases
*/
typedef struct backupControl_rec {
    Widget selectionbox;
    resourcesPtr resources;
    String *databases;
    int *expected;
    int num_databases;
    int current_database;
    int current_record;
    int expected_records;
    BlackBerry_t *device;
    FILE *file;
    int len;
    char *data;
} backupControl_t;

typedef struct password_rec {
    BlackBerry_t *blackberry;
    String password;
} password_t;

static XtSignalId signal_id;

/** The UI options
*/
static XrmOptionDescRec opTable[] = {
    {"-device",  ".deviceFile", XrmoptionSepArg, NULL}, /**< The BlackBerry is attached to this device file */
    {"--device",  ".deviceFile", XrmoptionSepArg, NULL},
    {"-debug",  ".debug", XrmoptionSepArg, NULL}, /**< 0 is very quiet, 9 is very verbose */
    {"-help",  ".help", XrmoptionNoArg, "True"}, /**< provide a quick summary of available options and remote actions */
    {"--help", ".help", XrmoptionNoArg, "True"},
    {"-fallback",  ".fallback", XrmoptionNoArg, "True"}, /**< display the compiled in X resources */
    {"--fallback",  ".fallback", XrmoptionNoArg, "True"},
    {"-prefix",  ".prefix", XrmoptionSepArg, NULL}, /**< the directory in which to store the database files */
    {"--prefix",  ".prefix", XrmoptionSepArg, NULL},
    {"-autoSave",  "*BBMenu*Options.button_1.set", XrmoptionNoArg, "False"}, /**< disable automatic save after connect */
    {"+autoSave",  "*BBMenu*Options.button_1.set", XrmoptionNoArg, "True"}, /**< ensable automatic save after connect */
    {"-autoSelectList",  ".autoSelectList", XrmoptionSepArg, NULL}, /**< comma separated list of databases to automatically select */
    {"-autoSetTime",  "*BBMenu*Options.button_0.set", XrmoptionNoArg, "False"}, /**< do not set the device time after connecting */
    {"+autoSetTime",  "*BBMenu*Options.button_0.set", XrmoptionNoArg, "True"}, /**< set the device time after connecting */
    {"-redirectErrors",  ".redirectErrors", XrmoptionNoArg, "False"}, /**< display stderr on stderr */
    {"+redirectErrors",  ".redirectErrors", XrmoptionNoArg, "True"}, /**< display stderr in a window */
#ifdef HAVE_OPENSYNC
    {"-syncGroup",  ".syncGroup", XrmoptionSepArg, NULL}, /**< the opensync groupname to use, instead of the device PIN */
#endif
    {"+connectToDesktop",  "*BBMenu*Options.button_2.set", XrmoptionNoArg, "True"}, /**< connect to the device desktop automatically */
    {"-connectToDesktop",  "*BBMenu*Options.button_2.set", XrmoptionNoArg, "False"}, /**< do not connect to the device desktop automatically */
#ifdef HAVE_XLTREMOTE
    {"-remote", ".remote", XrmoptionSepArg, NULL}, /**< send a command to a running XmBlackBerry application */
#endif
};

static XtResource AppResources[] = {
    {"deviceFile", "DeviceFile", XtRString, sizeof(String), XtOffset(resourcesPtr, device_file), XtRImmediate, NULL},
    {"debug", "Debug", XtRInt, sizeof(int), XtOffset(resourcesPtr, debug), XtRImmediate, (XtPointer)3},
    {"help", "Help", XtRBoolean, sizeof(Boolean), XtOffset(resourcesPtr, help), XtRImmediate, (void *)False},
    {"fallback", "Fallback", XtRBoolean, sizeof(Boolean), XtOffset(resourcesPtr, fallback), XtRImmediate, (void *)False},
    {"redirectErrors", "RedirectErrors", XtRBoolean, sizeof(Boolean), XtOffset(resourcesPtr, redirect_errors), XtRImmediate, (void *)True},
    {"prefix", "Prefix", XtRString, sizeof(String), XtOffset(resourcesPtr, prefix), XtRImmediate, ""},
    {"autoSelectList", "AutoSelectList", XtRString, sizeof(String), XtOffset(resourcesPtr, autoSelectList), XtRImmediate, NULL},
    {"syncGroup", "SyncGroup", XtRString, sizeof(String), XtOffset(resourcesPtr, syncGroup), XtRImmediate, NULL},
    {"remote", "Remote", XtRString, sizeof(String), XtOffset(resourcesPtr, remote_command), XtRImmediate, NULL},
};

/** The default X resources
*/
static char *FallBackResources[] = {
    "*dragInitiatorProtocolStyle: DRAG_NONE",
    "*dragReceiverProtocolStyle: DRAG_NONE",
    "*allowShellResize: True",
    "*XltSelectionBox.resizePolicy: XmRESIZE_GROW",
    "*StdErrText.columns: 80",
    "*StdErrText.rows: 12",
"",
    "*BBMenu.button_0.labelString: File",
    "*BBMenu.button_0.mnemonic: F",
    "*BBMenu.button_1.labelString: Help",
    "*BBMenu.button_1.mnemonic: H",
    "*BBMenu.button_2.labelString: Options",
    "*BBMenu.button_2.mnemonic: O",
    "*BBMenu.button_3.labelString: Sync",
    "*BBMenu.button_3.mnemonic: S",
    "*BBMenu.?*tearOffModel: XmTEAR_OFF_ENABLED",
"",
    "*BBMenu*File.button_0.labelString: Quit",
    "*BBMenu*File.button_0.accelerator: Ctrl<Key>C",
    "*BBMenu*File.button_0.acceleratorText: ^C",
"",
    "*BBMenu*File.button_1.labelString: Raw",
    "*BBMenu*File.button_1.accelerator: Ctrl<Key>R",
    "*BBMenu*File.button_1.acceleratorText: ^R",
"",
    "*BBMenu*File.button_2.labelString: View",
    "*BBMenu*File.button_2.accelerator: Ctrl<Key>V",
    "*BBMenu*File.button_2.acceleratorText: ^V",
"",
    "*BBMenu*Options.label_0.labelString: On Connect ...",
"",
    "*BBMenu*Options.button_0.labelString: Set Time",
    "*BBMenu*Options.button_0.accelerator: Ctrl<Key>T",
    "*BBMenu*Options.button_0.acceleratorText: ^T",
"",
    "*BBMenu*Options.button_1.labelString: Save",
    "*BBMenu*Options.button_1.accelerator: Ctrl<Key>S",
    "*BBMenu*Options.button_1.acceleratorText: ^S",
"",
    "*BBMenu*Options.button_2.labelString: Connect to Desktop",
    "*BBMenu*Options.button_2.set: True",
"",
    "*BBMenu*Sync.button_0.labelString: Sync",
    "*BBMenu*Sync.button_0.accelerator: Alt<Key>S",
    "*BBMenu*Sync.button_0.acceleratorText: <Alt>S",
"",
    "*BBMenu*Sync.button_1.labelString: SaveSyncRestore",
    /*
    "*BBMenu*Sync.button_1.accelerator: Alt<Key>C",
    "*BBMenu*Sync.button_1.acceleratorText: <Alt>C",
    */
"",
    "*BBMenu*Sync.button_2.labelString: Configure ...",
    "*BBMenu*Sync.button_2.accelerator: Alt<Key>C",
    "*BBMenu*Sync.button_2.acceleratorText: <Alt>C",
"",
    "*BBMenu*Help.button_0.labelString: About",
    "*BBMenu*Help.button_0.accelerator: Ctrl<Key>A",
    "*BBMenu*Help.button_0.acceleratorText: ^A",
    "*BBMenu*Help.button_1.labelString: Version",
    "*BBMenu*Help.button_1.accelerator: Ctrl<Key>V",
    "*BBMenu*Help.button_1.acceleratorText: ^V",
"",
    "*XltSelectionBox.listLabelString: Databases on ",
    "*XltSelectionBox.okLabelString: Save",
    "*XltSelectionBox.OK.toolTipString: Get the selected\\nDatabases\\nfrom the BlackBerry",
    "*XltSelectionBox.cancelLabelString: Quit",
    "*XltSelectionBox.Cancel.toolTipString: Quit the\\nApplication",
    "*XltSelectionBox.applyLabelString: Restore",
    "*XltSelectionBox.Apply.toolTipString: Put the selected\\nDatabases\\nonto the BlackBerry",
    "*XltSelectionBox.Help.toolTipString:",
    "*XltSelectionBox*XmScrollBar*toolTipString:",
    "*XltSelectionBox*ItemsList.toolTipString: Select the\\nDatabases\\nto retreive",
    "*XltSelectionBox.Text.toolTipString: Type the name\\nof the Database\\nto select",
"",
    "*Notebook.PageScroller.NBTextField.toolTipString: Select a different\\nBlackBerry",
"",
    "*Status.Messages.labelString: Initializing ....",
    "*Status.Messages.alignment: XmALIGNMENT_BEGINNING",
"",
    "*Status.Progress.slidingMode: XmTHERMOMETER",
    "*Status.Progress.sliderVisual: XmSHADOWED_BACKGROUND",
    "*Status.Progress.sliderMark: XmNONE",
    "*Status.Progress.showValue: False",
"",
    "*Raw*autoUnmanage: False",
"",
    "*XmMainWindow*translations: #override " DEFAULT_STROKE_TRANSLATION,
    "*XmDialogShell*translations: #override " DEFAULT_STROKE_TRANSLATION,
    "*strokeSound:",
    "*strokes: 456 ParentActivate, \
		654 ParentCancel, \
		123658 ManagerGadgetHelp, \
		74123 DisplayCopyright",
"",
    "*XmScrolledWindow.ClipWindow*translations: #override <Btn4Down>: SWChildScroll(1)\\n<Btn5Down>: SWChildScroll(0)",
"",
    "*Password.SelectionLabelString: Device Password",
    "*Password.Hidden.sensitive: False",
"",
    "*ViewDatabase.Notebook*sw.width: 551",
    "*ViewDatabase.Notebook*sw.height: 298",
    "*ViewDatabase.Notebook*record.editMode: MULTI_LINE_EDIT",
    "*ViewDatabase.Notebook*record.columns: 68",
    "*ViewDatabase.Notebook.PageScroller.NBTextField.toolTipString: Select a different\\nrecord",
    /*
    "*ViewDatabase.Notebook*record*rows: 24",
    */
"",
    "*conflict_dialog.cancelLabelString: Ignore",
    NULL
};

static void DisplayCopyright(Widget w, XEvent *event, String *params, Cardinal num_params);
static void SWChildScroll(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void quitAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void syncAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void saveSyncRestoreAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void backupAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void restoreAction(Widget w, XEvent *event, String *params, Cardinal *num_params);
static void selectDatabaseAction(Widget w, XEvent *event, String *params, Cardinal *num_params);

/** The remote command available
*/
static XtActionsRec actions[] = {
    {"DisplayCopyright", (XtActionProc)DisplayCopyright},
    {"SWChildScroll", (XtActionProc)SWChildScroll},
    {"Quit", (XtActionProc)quitAction},
    {"Sync", (XtActionProc)syncAction},
    {"SaveSyncRestore", (XtActionProc)saveSyncRestoreAction},
    {"Save", (XtActionProc)backupAction},
    {"Restore", (XtActionProc)restoreAction},
    {"SelectDatabase", (XtActionProc)selectDatabaseAction},
};

static void get_callback(int len, char *record, void *client_data);
static void restore_callback(int status, int *len, char **record, void *client_data);
static void AddDevice(BlackBerry_t *blackberry, resourcesPtr resources);
static void RemoveDevice(BlackBerry_t *blackberry, resourcesPtr resources);

/**
	For the generic file descriptor monitoring
*/
typedef struct GenericInputRec {
    int fd; /**< file descriptor to watch */
    XtInputId id; /**< the Xt input id */
    void *client_data; /**< pointer to data to use in the callback */
    int (*function)(void *data); /**< the callback for when there is data */
    resourcesPtr resources; /**< application resources */
} GenericInputStruct;

static void
DisplayCopyright(Widget w, XEvent *event, String *params, Cardinal num_params)
{
    debug(0, "%s", copyright);
}

/**
	Called when there is input on one of the monitored descriptors
*/
static void
GenericInput(XtPointer client_data, int *fd, XtInputId *id)
{
GenericInputStruct *cbs = (GenericInputStruct *)client_data;

    debug(9, "%s:%s(%d) - %i %p\n",
	__FILE__, __FUNCTION__, __LINE__,
	*fd, cbs->function);

    if (cbs->function)
    {
    int ret;

	ret = (*cbs->function)(cbs->client_data);
	if (ret < 0)
	{
	    XtRemoveInput(*id);
	    ListDequeue(&cbs->resources->monitor_list, cbs);
	    close(*fd);
	    XtFree(client_data);
	}
    }
    else
    {
    	XtRemoveInput(*id);
    	ListDequeue(&cbs->resources->monitor_list, cbs);
    	close(*fd);
    	XtFree(client_data);
    }
}

/**
	Used to initiate monitoring of a file descriptor
*/
static void
WatchFD(BlackBerry_t *blackberry, int fd, int (*function)(void *), void *client_data)
{
GenericInputStruct *cbs = NULL;
bb_t *bb = (bb_t *)blackberry->ui_private;

    debug(9, "%s:%s(%d) - %p %i %p %p\n",
	__FILE__, __FUNCTION__, __LINE__,
	&bb->resources->monitor_list,
	fd, function, client_data);

    if (function)
    {
	cbs = XtNew(GenericInputStruct);
	cbs->fd = fd;
	cbs->function = function;
	cbs->client_data = client_data;
	cbs->id = XtAppAddInput(bb->resources->context,
	    fd,
	    (XtPointer)(XtInputReadMask),
	    GenericInput,
	    cbs);
	cbs->resources = bb->resources;
	ListQueueTail(&bb->resources->monitor_list, cbs);
    }
    else
    {
    	while ((cbs = ListPeekNext(&bb->resources->monitor_list, cbs)))
    	{
	    if (cbs->fd == fd)
	    {
		XtRemoveInput(cbs->id);
		ListDequeue(&bb->resources->monitor_list, cbs);
		break;
	    }
    	}
    }
}

/**
	Used to get the application resources, given any widget.
	A pointer to the resource structure is attached to the
	userData of the MainWindow. This traverses up to the Shell,
	then locates the MainWindow, returning the pointer stored
	in userData of the MainWindow.
*/
static resourcesPtr
WidgetToResources(Widget w)
{
Widget shell = w;
Widget main_window;
resourcesPtr resources = NULL;

    while (XtParent(shell)) shell = XtParent(shell);
    main_window = XtNameToWidget(shell, ".BB");
    XtVaGetValues(main_window,
    	XmNuserData, &resources,
    	NULL);
    return(resources);
}

String
GetSyncGroup(Widget w)
{
String name = NULL;
resourcesPtr resources = WidgetToResources(w);

    if (resources)
    {
    	name = resources->syncGroup;
    }
    return(name);
}

/**
	Used to enable the scroll wheel in the list box. I should
	actually add this to XltSelectionBox.
*/
static void
SWChildScroll(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    if (*num_params == 1)
    {
    int dir;
    Widget sw = w;

    	while (sw && !XmIsScrolledWindow(sw)) sw = XtParent(sw);
    	if (sw)
    	{
	    dir = atoi(params[0]);
	    if (dir == 0)
	    {
		XtCallActionProc(sw, "SWDownPage", event, NULL, 0);
	    }
	    else
	    {
		XtCallActionProc(sw, "SWUpPage", event, NULL, 0);
	    }
    	}
    }
    else
    {
    }
}

/**
	Returns the state of a ToggleButton from the option menu
*/
static Boolean
IsOptionSet(Widget w, String button_name)
{
resourcesPtr resources;
Boolean set = False;

    resources = WidgetToResources(w);
    if (resources)
    {
    Widget button = XtNameToWidget(resources->option_pulldown, button_name);

	if (button)
	{
	    set = XmToggleButtonGetState(button);
	}
    }
    return(set);
}

static Boolean
IsConnectToDesktop(Widget w)
{
    return(IsOptionSet(w, ".button_2"));
}

static Boolean
IsAutoSetTime(Widget w)
{
    return(IsOptionSet(w, ".button_0"));
}

static Boolean
IsAutoSave(Widget w)
{
    return(IsOptionSet(w, ".button_1"));
}

/**
	Set the string that is shown in the MainWindow
	status area.
*/
static void
SetStatus(resourcesPtr resources, String msg)
{
XmString xm_msg;

    xm_msg = XmStringCreateSimple(msg);
    XtVaSetValues(resources->message,
    	XmNlabelString, xm_msg,
    	NULL);
    XmStringFree(xm_msg);
    XmUpdateDisplay(resources->message);
}

/**
	Set the string that is shown in the MainWindow
	status area with a printf like interface.
*/
static void
VaSetStatus(resourcesPtr resources, char *fmt, ...)
{
va_list args;
int len;
String buf;

    va_start(args, fmt);
    len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    buf = XtMalloc(len + 1);
    len = vsnprintf(buf, len + 1, fmt, args);
    SetStatus(resources, buf);
    XtFree(buf);
}

static String
databaseFilename(resourcesPtr resources, uint32_t pin, String database)
{
String dir;
String file;

    dir = XtMalloc(strlen(resources->prefix) + 1 + 8 + 1);
    sprintf(dir, "%s%s%08x", 
	resources->prefix,
	strlen(resources->prefix) > 0 ? "/" : "",
	pin);
    file = XtMalloc(strlen(dir) + strlen(database) + 2);
    sprintf(file, "%s/%s", dir, database);
    XtFree(dir);
    return(file);
}

static void
pageChange(Widget w, XtPointer client_data, XtPointer call_data)
{
XmNotebookCallbackStruct *cbs = (XmNotebookCallbackStruct *)call_data;
resourcesPtr resources = (resourcesPtr)client_data;
BlackBerry_t *device;
Widget list;
int selectedItemCount = 0;

    resources->current_page = cbs->page_widget;
    XtVaGetValues(resources->current_page,
    	XmNuserData, &device,
    	NULL);
    list = XtNameToWidget(resources->current_page, "*ItemsList");
    XtVaGetValues(list,
	XmNselectedItemCount, &selectedItemCount,
	NULL);
    debug(9, "%s:%s(%d) - %08x %s current_page = %i prev_page = %i\n",
	__FILE__, __FUNCTION__, __LINE__,
	device ? device->pin : (uint32_t)-1,
	XtName(w),
	cbs->page_number, cbs->prev_page_number);
    XtSetSensitive(XtNameToWidget(resources->file_pulldown, "*button_2"), (selectedItemCount == 1));
}

static void
RawCallback(Widget w, XtPointer client_data, XtPointer call_data)
{
XmSelectionBoxCallbackStruct *cbs = (XmSelectionBoxCallbackStruct *)call_data;
resourcesPtr resources = (resourcesPtr)client_data;
String item;
char *tok;
char *tmp;
unsigned char *packet = NULL;
int len = 0;
BlackBerry_t *device;

    XtVaGetValues(resources->current_page,
	XmNuserData, &device,
	NULL);
    item = XmStringUnparse(cbs->value, 
	NULL, 0, XmCHARSET_TEXT,
	NULL, 0, XmOUTPUT_ALL);

    tmp = item;
    while ((tok = strtok(tmp, " ,\t")))
    {
    unsigned int byte;

    	packet = (unsigned char *)XtRealloc((char *)packet, len + 1);
    	sscanf(tok, "%x", &byte);
    	packet[len] = byte & 0xff;
    	len++;
    	tmp = NULL;
    }
    /*
    dump_hex("=", packet, len);
    */
    if (device->desktop && device->desktop->send_packet)
    {
	(*device->desktop->send_packet)(device, device->desktop->socket, packet, len);
	//(*device->desktop->send_packet)(device, 0, packet, len);
    }
    XtFree(item);
    XtFree((char *)packet);
}

static void
Raw(resourcesPtr resources)
{
static Widget Dialog = NULL;

    if (Dialog == NULL)
    {
    	Dialog = XmCreatePromptDialog(resources->shell, "Raw", NULL, 0);
    	XtAddCallback(Dialog, XmNokCallback, RawCallback, resources);
    	XtAddCallback(Dialog, XmNcancelCallback, (XtCallbackProc)XtUnmanageChild, NULL);
    }
    XtManageChild(Dialog);
}

static void
FileMenuCallback(Widget w, XtPointer client_data, XtPointer call_data)
{
int button = (int)client_data;
resourcesPtr resources;

    resources = WidgetToResources(w);
    switch (button)
    {
    case 0: /* Quit */
    	{
    	bb_t *list = resources->widget_list;

	    debug(8, "%s:%s(%d)\n", __FILE__, __FUNCTION__, __LINE__);
	    while (list)
	    {
		VaSetStatus(resources, "Closing %08x", list->device->pin);
		debug(7, "%s:%s(%d) - %08x connected = %s\n",
			__FILE__, __FUNCTION__, __LINE__,
			list->device->pin,
			list->device->connected ? "True" : "False");
	    	if (list->device->close)
	    	{
		    (*list->device->close)(list->device);
	    	}
	    	list = list->next;
	    }
	    sync_finalize(resources->sync_env);
	    exit(EXIT_SUCCESS);
    	}
    	break;
    case 1: /* Raw */
    	Raw(resources);
    	break;
    case 2: /* View */
	if (resources->current_page)
	{
	Widget list;
	int selectedItemCount;
	XmStringTable selectedItems;

	    list = XtNameToWidget(resources->current_page, "*ItemsList");
	    XtVaGetValues(list,
		XmNselectedItemCount, &selectedItemCount,
		XmNselectedItems, &selectedItems,
		NULL);
	    if (selectedItemCount == 1)
	    {
	    String database;
	    String file;
	    BlackBerry_t *device;

		database = XmStringUnparse(selectedItems[0],
		    NULL, 0, XmCHARSET_TEXT,
		    NULL, 0, XmOUTPUT_ALL);
		XtVaGetValues(resources->current_page,
		    XmNuserData, &device,
		    NULL);
		file = databaseFilename(resources, device->pin, database);
		VaSetStatus(resources, "%08x Viewing \"%s\"",
		    device->pin,
		    database);
		ViewDatabase(resources->main_window, resources->progress, device, database);
		XtFree(database);
		XtFree(file);
	    }
	    else
	    {
	    }
	}
	else
	{
	}
    	break;
    default:
	debug(0, "%s:%s(%d) - %s(%i)\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    XtName(w), button);
    	break;
    }
}

static void
quitAction(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    FileMenuCallback(w, (XtPointer)0, NULL);
}

static int
NumDevices(bb_t *list)
{
int num = 0;

    while (list)
    {
    	list = list->next;
    	num++;
    }
    return(num);
}

static void
OptionMenuCallback(Widget w, XtPointer client_data, XtPointer call_data)
{
int button = (int)client_data;
resourcesPtr resources;
BlackBerry_t *device;

    resources = WidgetToResources(w);
    XtVaGetValues(resources->current_page,
	XmNuserData, &device,
	NULL);
    switch (button)
    {
    case 0: /* SetTime */
	{
	XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)call_data;

	    debug(5, "%s:%s(%d) - SetTime %08x %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		device ? device->pin : (uint32_t)-1,
		cbs->set ? "True" : "False");
	    if (cbs->set)
	    {
		if (SetTime(device) < 0)
		{
		    debug(0, "%s:%s(%d) - SetTime %08x %s\n",
			__FILE__, __FUNCTION__, __LINE__,
			device->pin,
			strerror(errno));
		}
	    }
    	}
    	break;
    case 1: /* Save */
    	break;
    case 2: /* ConnectToDesktop */
	{
	XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)call_data;

	    debug(5, "%s:%s(%d) - ConnectToDesktop %08x %s connected %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		device ? device->pin : (uint32_t)-1,
		cbs->set ? "True" : "False",
		device->desktop->connected ? "True" : "False");
	    if (cbs->set)
	    {
		if (device->desktop->connected)
		{
		    debug(0, "%s:%s(%d) - ConnectToDesktop %08x Already connected\n",
			__FILE__, __FUNCTION__, __LINE__);
		}
		else
		{
		    if (ConnectToDesktop(device, True) < 0)
		    {
			debug(0, "%s:%s(%d) - ConnectToDesktop %08x %s\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    device->pin,
			    strerror(errno));
		    }
		}
	    }
	    else
	    {
		if (device->desktop->connected)
		{
		    if (ConnectToDesktop(device, False) < 0)
		    {
			debug(0, "%s:%s(%d) - ConnectToDesktop %08x %s\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    device->pin,
			    strerror(errno));
		    }
		    else
		    {
			XtSetSensitive(XtNameToWidget(resources->current_page, ".OK"), False);
			XtSetSensitive(XtNameToWidget(resources->current_page, ".Text"), False);
			XtSetSensitive(XtNameToWidget(resources->current_page, ".Apply"), False);
			XtSetSensitive(XtNameToWidget(resources->current_page, "*ItemsList"), False);
			XtSetSensitive(XtNameToWidget(resources->main_window, "*File.button_0"), False);
			XtSetSensitive(XtNameToWidget(resources->main_window, "*Sync.button_0"), False);
		    }
		}
		else
		{
		    debug(0, "%s:%s(%d) - ConnectToDesktop %08x Already dis-connected\n",
			__FILE__, __FUNCTION__, __LINE__);
		}
	    }
    	}
    	break;
    default:
	debug(0, "%s:%s(%d) - %s(%i)\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    XtName(w), button);
    	break;
    }
}

static void
_sync_done(XtPointer client_data, XtIntervalId *id)
{
resourcesPtr resources = WidgetToResources(client_data);
BlackBerry_t *device;

    XtVaGetValues(resources->current_page,
	XmNuserData, &device,
	NULL);
    sync_finalize(NULL);
    VaSetStatus(resources, "%08x Sync done.",
	device->pin);
    resources->auto_actions &= ~(1 << ActionSync);
    if (resources->num_changes > 0 && (resources->auto_actions & (1 << ActionRestore)))
    {
	debug(0, "%s:%s(%d) - Restore after sync\n", __FILE__, __FUNCTION__, __LINE__);
	XtCallCallbacks(resources->current_page, XmNapplyCallback, NULL);
    }
    else
    {
	XtSetSensitive(XtNameToWidget(resources->current_page, ".OK"), True);
	XtSetSensitive(XtNameToWidget(resources->current_page, ".Cancel"), True);
	XtSetSensitive(XtNameToWidget(resources->current_page, ".Apply"), True);
	XtSetSensitive(XtNameToWidget(resources->main_window, "*File.button_0"), True);
	XtSetSensitive(XtNameToWidget(resources->main_window, "*Sync.button_0"), True);
	XtSetSensitive(XtNameToWidget(resources->notebook, ".PageScroller"), True);
    }
}

static void
sync_done(void *client_data, int num_changes)
{
resourcesPtr resources = (resourcesPtr)client_data;

    debug(0, "%s:%s(%d) - %i change%s\n",
	__FILE__, __FUNCTION__, __LINE__,
	num_changes,
	num_changes == 1 ? "" : "s");
    resources->num_changes = num_changes;
    /* rws 1 Dec 2006
       Do this with a timer callback since the sync stuff is probably
       coming from a different thread.
     */
    XtAppAddTimeOut(resources->context, 0, _sync_done, client_data);
}

static void
AutoSaveCallback(XtPointer client_data, XtIntervalId *id)
{
resourcesPtr resources = WidgetToResources(client_data);

    debug(9, "%s:%s(%d) - \n",
	__FILE__, __FUNCTION__, __LINE__);
    if (resources->backup_control)
    {
	/*
	    If a backup is in progress, try again later
	 */
	XtAppAddTimeOut(resources->context, 2000, AutoSaveCallback, client_data);
    }
    else
    {
	XtCallCallbacks(client_data, XmNokCallback, NULL);
    }
}

static void
FilePopupCallback(Widget w, XtPointer client_data, XtPointer call_data)
{
resources_t *resources = (resources_t *)client_data;
Widget list;
int selectedItemCount = 0;

    debug(9, "%s:%s(%d) - %s\n",
	__FILE__, __FUNCTION__, __LINE__,
	resources->current_page ? XtName(resources->current_page) : "NULL");

    if (resources->current_page)
    {
	list = XtNameToWidget(resources->current_page, "*ItemsList");
	XtVaGetValues(list,
	    XmNselectedItemCount, &selectedItemCount,
	    NULL);
    }
    else
    {
    }
    XtSetSensitive(XtNameToWidget(w, "*button_2"), (selectedItemCount == 1));
}

static void
OptionPopupCallback(Widget w, XtPointer client_data, XtPointer call_data)
{
resources_t *resources = (resources_t *)client_data;

    debug(9, "%s:%s(%d) - %s\n",
	__FILE__, __FUNCTION__, __LINE__,
	resources->current_page ? XtName(resources->current_page) : "NULL");

    if (resources->current_page)
    {
    BlackBerry_t *device;
    Widget button;

	button = XtNameToWidget(resources->option_pulldown, ".button_2");
	XtVaGetValues(resources->current_page,
	    XmNuserData, &device,
	    NULL);
	debug(0, "%s:%s(%d) - GPRS modem device %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    device && device->tty_name ? device->tty_name : "Not available");
	if (device && device->pin != (uint32_t)-1 && device->select_mode)
	{
	    XtSetSensitive(button, True);
	}
	else
	{
	    XtSetSensitive(button, False);
	}
    }
    else
    {
	debug(0, "%s:%s(%d) - No current_page\n",
	    __FILE__, __FUNCTION__, __LINE__);
    }
}

static void
SyncPopupCallback(Widget w, XtPointer client_data, XtPointer call_data)
{
resources_t *resources = (resources_t *)client_data;
Widget list;

    debug(9, "%s:%s(%d) - %s\n",
	__FILE__, __FUNCTION__, __LINE__,
	resources->current_page ? XtName(resources->current_page) : "NULL");

    if (resources->current_page)
    {
    BlackBerry_t *device;

	list = XtNameToWidget(resources->current_page, "*ItemsList");
	XtVaGetValues(resources->current_page,
	    XmNuserData, &device,
	    NULL);
	if (device && device->pin != (uint32_t)-1)
	{
	Boolean can_sync;

	    debug(8, "%s:%s(%d) - %08x\n",
		__FILE__, __FUNCTION__, __LINE__,
		device ? device->pin : (uint32_t)-1);
	    XtSetSensitive(resources->sync_pulldown, True);
	    if ((can_sync = sync_can_sync(resources->sync_env, device->pin)))
	    {
	    }
	    else
	    {
		debug(0, "%s:%s(%d) - %08x Can't sync\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    device ? device->pin : (uint32_t)-1);
	    }
	    XtSetSensitive(XtNameToWidget(w, ".button_0"), can_sync);
	    XtSetSensitive(XtNameToWidget(w, ".button_1"), can_sync);
	}
	else
	{
	    XtSetSensitive(resources->sync_pulldown, False);
	}
    }
    else
    {
	debug(0, "%s:%s(%d) - No current_page\n",
	    __FILE__, __FUNCTION__, __LINE__);
    }
}

static void
SyncMenuCallback(Widget w, XtPointer client_data, XtPointer call_data)
{
int button = (int)client_data;
resourcesPtr resources;

    resources = WidgetToResources(w);
    switch (button)
    {
    case 0: /* Sync */
	{
	BlackBerry_t *device;

	    XtVaGetValues(resources->current_page,
	    	XmNuserData, &device,
	    	NULL);
	    XtSetSensitive(XtNameToWidget(resources->current_page, ".OK"), False);
	    XtSetSensitive(XtNameToWidget(resources->current_page, ".Cancel"), False);
	    XtSetSensitive(XtNameToWidget(resources->current_page, ".Apply"), False);
	    XtSetSensitive(XtNameToWidget(resources->main_window, "*File.button_0"), False);
	    XtSetSensitive(XtNameToWidget(resources->main_window, "*Sync.button_0"), False);
	    XtSetSensitive(XtNameToWidget(resources->notebook, ".PageScroller"), False);
	    VaSetStatus(resources, "Syncing ...");
	    sync_start(resources->sync_env, device->pin, sync_done, resources);
    	}
    	break;
    case 1: /* SaveSyncRestore */
    	{
	    resources->auto_actions |= (1 << ActionSave);
	    resources->auto_actions |= (1 << ActionSync);
	    resources->auto_actions |= (1 << ActionRestore);
	    AutoSaveCallback(resources->current_page, NULL);
    	}
    	break;
    case 2: /* Configure ... */
	{
	BlackBerry_t *device;

	    XtVaGetValues(resources->current_page,
	    	XmNuserData, &device,
	    	NULL);
	    sync_configure(resources->sync_env, device->pin);
    	}
    	break;
    default:
	debug(0, "%s:%s(%d) - %s(%i)\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    XtName(w), button);
    	break;
    }
}

static void
syncAction(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
resourcesPtr resources;
Widget menu;

    resources = WidgetToResources(w);
    menu = XtNameToWidget(resources->shell, "*BBMenu*Sync");
    SyncPopupCallback(menu, resources, NULL);
    SyncMenuCallback(w, (XtPointer)0, NULL);
}

static void
saveSyncRestoreAction(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
resourcesPtr resources;
Widget menu;

    resources = WidgetToResources(w);
    menu = XtNameToWidget(resources->shell, "*BBMenu*Sync");
    SyncPopupCallback(menu, resources, NULL);
    SyncMenuCallback(w, (XtPointer)0, NULL);
}

static void
ListMakeItemVisible(Widget list, int pos)
{
int itemCount, topItemPosition, visibleItemCount;

    XtVaGetValues(list,
	XmNitemCount, &itemCount,
	XmNtopItemPosition, &topItemPosition,
	XmNvisibleItemCount, &visibleItemCount,
	NULL);
    //printf("%i %i %i %i\n", pos, itemCount, topItemPosition, visibleItemCount);
    if (pos >= topItemPosition && pos < topItemPosition + visibleItemCount)
    {
	/* Already visible */
    }
    else if (pos >= topItemPosition + visibleItemCount)
    {
	XmListSetPos(list, topItemPosition + pos - (topItemPosition + visibleItemCount) + 1);
    }
    else if (pos <= topItemPosition)
    {
	XmListSetPos(list, topItemPosition - (topItemPosition - pos));
    }
    else
    {
    }
}

static void
selectDatabaseAction(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
    debug(9, "%s:%s(%d) - num_params %i\n",
	__FILE__, __FUNCTION__, __LINE__,
	*num_params);

    if (*num_params > 0)
    {
    resourcesPtr resources;

	resources = WidgetToResources(w);
	if (resources->current_page)
	{
	Widget list;
	int i;

	    list = XtNameToWidget(resources->current_page, "*ItemsList");
	    if (list)
	    {
		for (i = 0; i < *num_params; i++)
		{
		XmString xm_item;
		int pos;

		    xm_item = XmStringCreateSimple(params[i]);
		    pos = XmListItemPos(list, xm_item);
		    if (pos > 0)
		    {
			ListMakeItemVisible(list, pos);
			XmListSelectPos(list, pos, True);
		    }
		    else
		    {
			XmListDeselectAllItems(list);
		    }
		    XmStringFree(xm_item);
		}
	    }
	    else
	    {
	    }
    	}
    }
    else
    {
	debug(0, "%s:%s(%d) - need at least 1 parameter. %i given.\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    *num_params);
    }
}

static void
HelpMenuCallback(Widget w, XtPointer client_data, XtPointer call_data)
{
int button = (int)client_data;

    switch (button)
    {
    case 0:
	debug(0, "%s:%s(%d) - %s-%s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    PACKAGE, VERSION);
	DisplayCopyright(w, NULL, NULL, 0);
    	break;
    case 1:
	debug(0, "%s:%s(%d) - %s-%s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    PACKAGE, VERSION);
    	break;
    default:
	debug(0, "%s:%s(%d) - %s(%i)\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    XtName(w), button);
    	break;
    }
}

static String
ResourcePath(Widget w, String name)
{
String ret = NULL;
String part;

    part = XtMalloc(strlen(XtName(w)) + strlen(name) + 3);
    if (XtParent(w) && ! XtIsShell(XtParent(w)))
    {
	sprintf(part, "%s.%s", XtName(w), name);
    	ret = ResourcePath(XtParent(w), part);
	XtFree(part);
    }
    else
    {
	sprintf(part, "*%s.%s", XtName(w), name);
    	ret = part;
    }
    return(ret);
}

static void
SaveToggle(Widget w, XtPointer client_data, XtPointer call_data)
{
XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)call_data;
String path;

    path = ResourcePath(w, "set");
    debug(9, "%s:%s(%d) - %s \"%s: %s\"\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	XtName(w),
	path,
	cbs->set ? "True" : "False");
    XltRdbPutString(w, path, cbs->set ? "True" : "False");
    XtFree(path);
}

static void
SetTimeCallback(XtPointer client_data, XtIntervalId *id)
{
BlackBerry_t *dev = (BlackBerry_t *)client_data;

    debug(9, "%s:%s(%d) - set time for %08x\n",
	__FILE__, __FUNCTION__, __LINE__,
	dev->pin);
    if (SetTime(dev) < 0)
    {
	debug(0, "%s:%s(%d) - SetTime %08x %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    dev->pin,
	    strerror(errno));
    }
}

static void
LoadPixmaps(Widget w)
{
char **pixmaps[] = {bbarrows_both, bbarrows_up, bbarrows_down, bbarrows_none};
char *pix_names[] = {"both", "up", "down", "none"};
int i;
XpmAttributes attrib;
GC gc;
Pixel background;
Pixel foreground, top_shadow, bottom_shadow, select;
Colormap colormap;

    /*
    fprintf(stderr, "%s:%s(%d)\n",
	__FILE__, __FUNCTION__, __LINE__);
	*/
    XtVaGetValues(w,
    	XmNbackground, &background,
    	XmNcolormap, &colormap,
    	NULL);
    XmGetColors(XtScreen(w),
    	colormap,
    	background,
	&foreground, &top_shadow, &bottom_shadow, &select);
    gc = XDefaultGCOfScreen(XtScreen(w));
    for (i = 0; i < XtNumber(pixmaps); i++)
    {
    Pixmap pixmap, mask;
    XImage *image;
    XpmColorSymbol colors[] = {
	{ NULL, "foreground", foreground},
	{ NULL, "topshadow", top_shadow},
	{ NULL, "bottomshadow", bottom_shadow},
	{ NULL, "select", select},
	{ NULL, "None", background}};

	/*
	fprintf(stderr, "%s:%s(%d) - %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    pix_names[i]);
	    */
	attrib.valuemask = XpmCloseness | XpmColorSymbols;
	attrib.closeness = 40000;
	attrib.colorsymbols = colors;
	attrib.numsymbols = XtNumber(colors);
	XpmCreatePixmapFromData(XtDisplay(w),
	    XRootWindowOfScreen(XtScreen(w)),
	    pixmaps[i],
	    &pixmap,
	    &mask,
	    &attrib);

	image = XGetImage(XtDisplay(w),
	    pixmap,
	    0, 0, attrib.width, attrib.height,
	    (unsigned long)-1, ZPixmap);
	XmInstallImage(image, pix_names[i]);

	XpmFreeAttributes(&attrib);
    }
}

static void
add_list_items(resourcesPtr resources, Widget selectionbox, BlackBerry_t *dev)
{
Widget list;
Widget label;
dbdb_t *database = NULL;
XmString xm_label;
XmString xm_db_name;
char db_name[11];
Boolean shellResize;

    if (dev->desktop && dev->desktop->database)
    {
	database = dev->desktop->database->database_table;
    }
    if (database)
    {
    int listItemCount;
    int selected_item_count = 0;

	VaSetStatus(resources, "Found %08x", dev->pin);
	XtVaGetValues(resources->shell,
	    XmNallowShellResize, &shellResize,
	    NULL);
	list = XtNameToWidget(selectionbox, "*ItemsList");
	XtVaGetValues(selectionbox,
	    XmNlistItemCount, &listItemCount,
	    NULL);
	if (listItemCount == 0)
	{
	    label = XtNameToWidget(selectionbox, ".Items");
	    XtVaGetValues(label,
		XmNlabelString, &xm_label,
		NULL);
	    snprintf(db_name, sizeof(db_name), "(%08x)", (dev->pin & 0xffffffff));
	    xm_db_name = XmStringCreateSimple(db_name);
	    xm_label = XmStringConcatAndFree(xm_label, xm_db_name);
	    XtVaSetValues(label,
		XmNlabelString, xm_label,
		NULL);
	}
	else
	{
	    debug(7, "%s:%s(%d) - %08x already has %i item%s.\n",
		__FILE__, __FUNCTION__, __LINE__,
		dev->pin,
		listItemCount,
		listItemCount > 1 ? "s" : "");
	    XmListDeleteAllItems(list);
	}
	XtVaSetValues(list,
	    XmNmustMatch, True,
	    XmNselectionPolicy, XmMULTIPLE_SELECT,
	    NULL);
	XtVaSetValues(resources->shell,
	    XmNallowShellResize, False,
	    NULL);
	while (database)
	{
	XmString xm_string;

	    xm_string = XmStringCreateSimple(database->name);
	    XmListAddItem(list, xm_string, 1);
	    if (IsOnTextList(resources->auto_save_list, database->name))
	    {
		XmListSelectPos(list, 1, False);
		selected_item_count++;
	    }
	    XmStringFree(xm_string);
	    database = database->next;
	}
	XtSetSensitive(XtNameToWidget(resources->file_pulldown, "*button_2"), (selected_item_count == 1));
	XtVaSetValues(list,
	    XmNmustMatch, True,
	    XmNselectionPolicy, XmEXTENDED_SELECT,
	    NULL);
	XtSetSensitive(XtNameToWidget(resources->current_page, ".OK"), True);
	XtSetSensitive(XtNameToWidget(resources->current_page, ".Text"), True);
	XtSetSensitive(XtNameToWidget(resources->current_page, ".Apply"), True);
	XtSetSensitive(XtNameToWidget(resources->current_page, "*ItemsList"), True);
	XtSetSensitive(XtNameToWidget(resources->main_window, "*File.button_0"), True);
	XtSetSensitive(XtNameToWidget(resources->main_window, "*Sync.button_0"), True);
	XtSetSensitive(XtNameToWidget(resources->notebook, ".PageScroller"), True);
	XtManageChild(selectionbox);
	XtVaSetValues(selectionbox,
	    XmNmappedWhenManaged, True,
	    NULL);
	XtVaSetValues(resources->shell,
	    XmNallowShellResize, shellResize,
	    NULL);
    	if (IsAutoSetTime(selectionbox))
    	{
	    XtAppAddTimeOut(resources->context, 0, SetTimeCallback, dev);
    	}
    	else if (IsAutoSave(selectionbox))
    	{
	    XtAppAddTimeOut(resources->context, 2000, AutoSaveCallback, selectionbox);
    	}
    }
    XtVaSetValues(resources->shell,
	XmNallowShellResize, True,
	NULL);
    label = XtNameToWidget(selectionbox, "*Description");
    if (label == NULL)
    {
    Widget form;
    Widget arrows;
    Pixmap pixmap;

	form = XmCreateForm(selectionbox, "D-Form", NULL, 0);
	LoadPixmaps(form);
	pixmap = XmGetPixmap(XtScreen(selectionbox),
	    "none", None, None);
	label = XmCreateLabel(form, "Description", NULL, 0);
	arrows = XmCreateLabel(form, "Arrows", NULL, 0);
	XtVaSetValues(arrows,
	    XmNlabelType, XmPIXMAP,
	    XmNlabelPixmap, pixmap,
	    XmNrightAttachment, XmATTACH_FORM,
	    XmNtopAttachment, XmATTACH_FORM,
	    XmNbottomAttachment, XmATTACH_FORM,
	    NULL);
	XtVaSetValues(label,
	    XmNrightAttachment, XmATTACH_WIDGET,
	    XmNrightWidget, arrows,
	    XmNleftAttachment, XmATTACH_FORM,
	    XmNtopAttachment, XmATTACH_FORM,
	    XmNbottomAttachment, XmATTACH_FORM,
	    NULL);
	XtManageChild(arrows);
	XtManageChild(form);
    }
    if (dev->desc)
    {
    XmString xm_desc;

	xm_desc = XmStringCreateSimple(dev->desc);
	XtVaSetValues(label,
	    XmNlabelString, xm_desc,
	    NULL);
	XmStringFree(xm_desc);
	XtManageChild(label);
    }
}

static void
serial_input(XtPointer client_data, int *fd, XtInputId *id)
{
bb_t *dev = (bb_t *)client_data;
dbdb_t *table = NULL;

    debug(9, "%s:%s(%d) - %i %p\n",
	__FILE__, __FUNCTION__, __LINE__,
	*fd, dev->device->read);
    if (dev->device->desktop  && dev->device->desktop->database)
    {
	table = dev->device->desktop->database->database_table;
    }
    if (dev->device->connected && dev->device->read)
    {
    ssize_t num_read;

	num_read = (*dev->device->read)(dev->device);
	if (num_read >= 0)
	{
	    if (dev->device->desktop && dev->device->desktop->database && table != dev->device->desktop->database->database_table)
	    {
		add_list_items(dev->resources, dev->selectionbox, dev->device);
	    }
	}
	else
	{
	    debug(0, "%s:%s(%d) - %08x read error. %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		dev->device->pin, strerror(errno));
	    XtRemoveInput(dev->input_id);
	    dev->input_id = 0;
	    close(*fd);
	}
    }
    else
    {
	debug(2, "%s:%s(%d) - %08x Serial input without a reader!! %sconnected %p\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    dev->device->pin,
	    dev->device->connected ? "" : "Not ", dev->device->read);
	XtRemoveInput(dev->input_id);
	dev->input_id = 0;
	close(*fd);
    }
}

static void
GetDatabaseTimerCallback(XtPointer client_data, XtIntervalId *id)
{
backupControl_t *control = (backupControl_t *)client_data;
String dir;
int ret;
struct stat stat_buf;

    debug(9, "%s:%s(%d) - %08x \"%s\" %i\n",
	__FILE__, __FUNCTION__, __LINE__,
	control->device->pin,
	control->resources->prefix,
	control->num_databases);

    if (control->num_databases > 0)
    {
	dir = XtMalloc(strlen(control->resources->prefix) + 1 + 8 + 1);
	sprintf(dir, "%s%s%08x", 
	    control->resources->prefix,
	    strlen(control->resources->prefix) > 0 ? "/" : "",
	    control->device->pin);
	ret = stat(dir, &stat_buf);
	if (ret == 0)
	{
	    if (S_ISDIR(stat_buf.st_mode))
	    {
	    }
	    else
	    {
		errno = ENOTDIR;
		debug(0, "%s:%s(%d) - %08x %s %s\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    control->device->pin,
		    dir,
		    strerror(errno));
		ret = -1;
	    }
	}
	else
	{
	    switch (errno)
	    {
	    case ENOENT:
		ret = mkdir(dir, 0777);
		if (ret == 0)
		{
		}
		else
		{
		    debug(0, "%s:%s(%d) - %08x %s %i %s\n",
			__FILE__, __FUNCTION__, __LINE__,
			control->device->pin,
			dir,
			errno,
			strerror(errno));
		}
		break;
	    default:
		debug(0, "%s:%s(%d) - %08x %s %i %s\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    control->device->pin,
		    dir,
		    errno,
		    strerror(errno));
	    break;
	    }
	}

	if (ret == 0)
	{
	String file = NULL;

	    file = databaseFilename(control->resources, control->device->pin, control->databases[control->current_database]);
	    control->expected_records = control->expected[control->current_database];
	    control->file = fopen(file, "wb");
	    if (control->file)
	    {
	    int ret;

		VaSetStatus(control->resources, "%08x \"%s\" to \"%s\"",
		    control->device->pin,
		    control->databases[control->current_database],
		    file);
		debug(5, "%s:%s(%d) - Saving \"%s\" to \"%s\".\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    control->databases[control->current_database],
		    file);
		if ((ret = GetDataBase(control->device, control->databases[control->current_database], get_callback, control->resources)) < 0)
		{
		    debug(0, "%s:%s(%d) - %s. %s.\n",
			__FILE__, __FUNCTION__, __LINE__,
			control->databases[control->current_database],
			strerror(errno));
		    get_callback(0, NULL, control->resources);
		}
	    }
	    else
	    {
		debug(0, "%s:%s(%d) - %s. %s.\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    control->databases[control->current_database],
		    strerror(errno));
		get_callback(0, NULL, control->resources);
	    }
	    XtFree(file);
	}
	XtFree(dir);
    }
    else
    {
	get_callback(0, NULL, control->resources);
    }
}

static void
RestoreDatabaseTimerCallback(XtPointer client_data, XtIntervalId *id)
{
backupControl_t *control = (backupControl_t *)client_data;

    debug(9, "%s:%s(%d) - %08x \"%s\" %i\n",
	__FILE__, __FUNCTION__, __LINE__,
	control->device->pin,
	control->resources->prefix,
	control->num_databases);

    if (control->num_databases > 0)
    {
    String file;
	file = databaseFilename(control->resources, control->device->pin, control->databases[control->current_database]);
	control->expected_records = get_database_nr(control->device->desktop->database->database_table, control->databases[control->current_database]);
	control->file = fopen(file, "rb");
	if (control->file)
	{
	int ret;

	    VaSetStatus(control->resources, "%08x \"%s\" from \"%s\"",
		control->device->pin,
		control->databases[control->current_database],
		file);
	    debug(5, "%s:%s(%d) - Restoring \"%s\" from \"%s\".\n",
		__FILE__, __FUNCTION__, __LINE__,
		control->databases[control->current_database],
		file);
	    if ((ret = PutDataBase(control->device, control->databases[control->current_database], restore_callback, control->resources)) < 0)
	    {
		debug(0, "%s:%s(%d) - %s. %s.\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    control->databases[control->current_database],
		    strerror(errno));
		restore_callback(errno, NULL, NULL, control->resources);
	    }
	}
	else
	{
	    debug(0, "%s:%s(%d) - %s. %s.\n",
		__FILE__, __FUNCTION__, __LINE__,
		control->databases[control->current_database],
		strerror(errno));
	    restore_callback(errno, NULL, NULL, control->resources);
	}
	XtFree(file);
    }
    else
    {
	restore_callback(EINVAL, NULL, NULL, control->resources);
    }
}

static Boolean
verify_file(backupControl_t *control)
{
int ret;

    control->current_record = 0;
    while ((ret = read_record(control->file, NULL, NULL)) > 0)
    {
    	control->current_record++;
    }
    return(ret == 0);
}

static Boolean
file_is_good(backupControl_t *control)
{
String file;
Boolean ret;

    file = databaseFilename(control->resources, control->device->pin, control->databases[control->current_database]);
    VaSetStatus(control->resources, "Verifying \"%s\"",
	control->databases[control->current_database]);
    control->file = fopen(file, "rb");
    if (control->file)
    {
    	ret = verify_file(control);
    	if (ret)
    	{
	    control->expected_records += control->current_record;
	    control->expected[control->current_database] = control->current_record;
	    debug(4, "%s:%s(%d) - file \"%s\" has %i record%s of %i total\n",
		__FILE__, __FUNCTION__, __LINE__,
		file,
		control->current_record,
		control->current_record == 1 ? "" : "s",
		control->expected_records);
    	}
    	control->current_record = 0;
    	fclose(control->file);
    	control->file = NULL;
    }
    else
    {
	debug(0, "%s:%s(%d) - bad file \"%s\" Restore aborted. %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    file, strerror(errno));
    	ret = False;
    }
    XtFree(file);
    return(ret);
}

static void
VerifyDatabaseTimerCallback(XtPointer client_data, XtIntervalId *id)
{
backupControl_t *control = (backupControl_t *)client_data;

    debug(9, "%s:%s(%d) - %08x \"%s\" %i %i\n",
	__FILE__, __FUNCTION__, __LINE__,
	control->device->pin,
	control->resources->prefix,
	control->current_database,
	control->num_databases);

    if (control->num_databases > 0)
    {
	if (control->current_database < control->num_databases)
	{
	    if (file_is_good(control))
	    {
		control->current_database++;
		XtVaSetValues(control->resources->progress,
		    XmNvalue, control->current_database,
		    NULL);
		XmUpdateDisplay(control->resources->progress);
		XtAppAddTimeOut(control->resources->context, 0, VerifyDatabaseTimerCallback, control);
	    }
	    else
	    {
		debug(0, "%s:%s(%d) - bad file \"%s\". Restore aborted.\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    control->databases[control->current_database]);
		control->current_database = control->num_databases - 1;
		restore_callback(EBADMSG, NULL, NULL, control->resources);
	    }
	}
	else
	{
	    debug(5, "%s:%s(%d) - All files good. %i\n",
		__FILE__, __FUNCTION__, __LINE__,
		control->expected_records);
	    XtVaSetValues(control->resources->progress,
		XmNvalue, 0,
		XmNminimum, control->expected_records > 0 ? 0 : -1,
		XmNmaximum, control->expected_records,
		NULL);
	    XmUpdateDisplay(control->resources->progress);
#if 1
	    control->current_database = 0;
	    control->current_record = 0;
	    /* Do this if they are all good */
	    SetStatus(control->resources, "Restoring ...");
	    //XtAppAddTimeOut(control->resources->context, 500, RestoreDatabaseTimerCallback, control);
	    XtAppAddTimeOut(control->resources->context, 0, RestoreDatabaseTimerCallback, control);
#else
	    control->current_database = control->num_databases - 1;
	    restore_callback(EBADMSG, NULL, NULL, control->resources);
#endif
	}
    }
    else
    {
	restore_callback(EINVAL, NULL, NULL, control->resources);
    }
}

static void
restore_callback(int status, int *len, char **record, void *client_data)
{
resourcesPtr resources = (resourcesPtr)client_data;
backupControl_t *control;
int value;
int maximum;

    debug(9, "%s:%s(%d) - status = %i %s len = %p record = %p\n",
	__FILE__, __FUNCTION__, __LINE__,
	status,
	strerror(status),
	len, record);
    if (resources)
    {
	control = resources->backup_control;
	XtVaGetValues(resources->progress,
	    XmNvalue, &value,
	    XmNmaximum, &maximum,
	    NULL);
	if (control->num_databases > 0)
	{
	    debug(len == NULL ? 4 : 5, "%s:%s(%d) - device %08x \"%s\" %i of %i total %i of %i. %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		control->device->pin,
		control->databases ? control->databases[control->current_database] : "",
		control->current_record,
		control->expected ? control->expected[control->current_database] : 0,
		value, control->num_databases,
		status != 0 ? strerror(status) : "");
	    VaSetStatus(control->resources, "%08x \"%s\" %i(%i) %i(%i)",
		control->device->pin,
		control->databases ? control->databases[control->current_database] : "",
		control->current_record,
		control->expected ? control->expected[control->current_database] : 0,
		value, maximum);
	}
	if (len == NULL)
	{
	    if (control->data)
	    {
		free(control->data);
		control->data = NULL;
	    }
	    if (control->file)
	    {
		fclose(control->file);
		control->file = NULL;
	    }
	    if (control->num_databases > 0 && (status != 0 || control->current_record != control->expected[control->current_database]))
	    {
		debug(0, "%s:%s(%d) - device %08x \"%s\" restored %i(%i) was %i record%s. %s\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    control->device->pin,
		    control->databases ? control->databases[control->current_database] : "",
		    control->current_record,
		    control->expected ? control->expected[control->current_database] : 0,
		    control->expected_records,
		    control->expected_records == 1 ? "" : "s",
		    status != 0 ? strerror(status) : "");
#if 1
		/* Not sure if this is a good idea */
		if (status == EACCES && control->current_record == 0)
		{
		    control->expected_records -= control->expected ? control->expected[control->current_database] : 0;
		    maximum -= control->expected ? control->expected[control->current_database] : 0;
		}
#endif
	    }
	    control->current_database++;
	    control->current_record = 0;
	    if (control->current_database < control->num_databases)
	    {
		//XtAppAddTimeOut(resources->context, 500, RestoreDatabaseTimerCallback, control);
		XtAppAddTimeOut(resources->context, 0, RestoreDatabaseTimerCallback, control);
	    }
	    else
	    {
		VaSetStatus(control->resources, "%08x Restored %i of %i record%s to %i database%s",
		    control->device->pin,
		    value, maximum,
		    value == 1 ? "" : "s",
		    control->num_databases,
		    control->num_databases == 1 ? "" : "s");
		for (; control->num_databases > 0; control->num_databases--)
		{
		    XtFree(control->databases[control->num_databases -1]);
		}
		XtFree((char *)control->databases);
		XtFree((char *)control->expected);
//		resources->auto_actions &= ~(1 << ActionRestore);
		/*
		if (resources->auto_actions & (1 << ActionRestore))
		{
		    debug(0, "%s:%s(%d) - Restore after sync\n", __FILE__, __FUNCTION__, __LINE__);
		    XtCallCallbacks(resources->current_page, XmNapplyCallback, NULL);
		}
		*/
		XtSetSensitive(XtNameToWidget(control->selectionbox, ".OK"), True);
		XtSetSensitive(XtNameToWidget(control->selectionbox, ".Cancel"), True);
		XtSetSensitive(XtNameToWidget(control->selectionbox, ".Apply"), True);
		XtSetSensitive(XtNameToWidget(resources->main_window, "*File.button_0"), True);
		XtSetSensitive(XtNameToWidget(resources->main_window, "*Sync.button_0"), True);
		XtSetSensitive(XtNameToWidget(resources->notebook, ".PageScroller"), True);
		XmProcessTraversal(XtNameToWidget(control->selectionbox, ".Apply"), XmTRAVERSE_CURRENT);
		XtFree((char *)control);
		resources->backup_control = NULL;
	    }
	}
	else
	{
#if 1
	    if (status != 0)
	    {
		debug(0, "%s:%s(%d) - device %08x \"%s\" record %i of %i %s. Record not restored. len = %i, %i packet%s\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    control->device->pin,
		    control->databases ? control->databases[control->current_database] : "",
		    control->current_record,
		    control->expected ? control->expected[control->current_database] : 0,
		    strerror(status),
		    control->len,
		    (control->len / control->device->max_packet) + 1,
		    (control->len / control->device->max_packet) + 1 == 1 ? "" : "s");
		value--;
		control->current_record--;
	    }
	    if (control->data)
	    {
		free(control->data);
		control->data = NULL;
	    }
	    read_record(control->file, &control->len, &control->data);
	    *len = control->len;
	    *record = control->data;
	    if (control->len > 0)
	    {
		value++;
		control->current_record++;
	    }
#else
	    *len = 0;
	    *record = NULL;
#endif
	}
	maximum = maximum >= value ? maximum : value,
	XtVaSetValues(resources->progress,
	    XmNminimum, maximum > 0 ? 0 : -1,
	    XmNvalue, value,
	    XmNmaximum, maximum,
	    NULL);
	XmUpdateDisplay(resources->progress);
    }
    else
    {
	debug(0, "%s:%s(%d) - What happened to my client data!!!\n",
	    __FILE__, __FUNCTION__, __LINE__);
    }
}

static void
restore(Widget w, XtPointer client_data, XtPointer call_data)
{
resourcesPtr resources = (resourcesPtr)client_data;

    debug(9, "%s:%s(%d) - Restore is starting\n",
	__FILE__, __FUNCTION__, __LINE__);
    if (resources->backup_control == NULL)
    {
    BlackBerry_t *device;
    Widget list;
    int selectedItemCount;
    XmStringTable selectedItems;
    int i;
    backupControl_t *control = XtNew(backupControl_t);

	resources->backup_control = control;
	XtVaGetValues(w,
	    XmNuserData, &device,
	    NULL);
	control->resources = resources;
	control->num_databases = 0;
	control->databases = NULL;
	control->expected = NULL;
	control->file = NULL;
	control->data = NULL;
	control->device = device;
	control->current_database = 0;
	control->current_record = 0;
	control->expected_records = 0;
	control->selectionbox = w;
	list = XtNameToWidget(w, "*ItemsList");
	XtVaGetValues(list,
	    XmNselectedItemCount, &selectedItemCount,
	    XmNselectedItems, &selectedItems,
	    NULL);
	debug(1, "%s:%s(%d) - %s %s %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    "item",
	    "index",
	    "database");
	for (i = 0; i < selectedItemCount; i++)
	{
	String item;

	    item = XmStringUnparse(selectedItems[i], 
		NULL, 0, XmCHARSET_TEXT,
		NULL, 0, XmOUTPUT_ALL);
	    debug(1, "%s:%s(%d) - %4i %5i \"%s\"\n",
		__FILE__, __FUNCTION__, __LINE__,
		i,
		get_database_index(device->desktop->database->database_table, item),
		item);
	    control->num_databases++;
	    control->databases = (String *)XtRealloc((char *)control->databases, control->num_databases * sizeof(String *));
	    control->databases[control->num_databases - 1] = XtNewString(item);
	    control->expected = (int *)XtRealloc((char *)control->expected, control->num_databases * sizeof(int *));
	    control->expected[control->num_databases - 1] = 0;
	    XtFree(item);
	}
	debug(1, "%s:%s(%d) - %i database%s.\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    control->num_databases,
	    control->num_databases > 1 ? "s" : "");
	XtSetSensitive(XtNameToWidget(w, ".OK"), False);
	XtSetSensitive(XtNameToWidget(w, ".Cancel"), False);
	XtSetSensitive(XtNameToWidget(w, ".Apply"), False);
	XtSetSensitive(XtNameToWidget(resources->main_window, "*File.button_0"), False);
	XtSetSensitive(XtNameToWidget(resources->main_window, "*Sync.button_0"), False);
	XtSetSensitive(XtNameToWidget(resources->notebook, ".PageScroller"), False);
	if (control->num_databases > 0)
	{
	    XtVaSetValues(resources->progress,
		XmNminimum, control->num_databases > 0 ? 0 : -1,
		XmNmaximum, control->num_databases,
		XmNvalue, 0,
		NULL);
	    XmUpdateDisplay(resources->progress);
	    SetStatus(resources, "Verifying ...");
	    //XtAppAddTimeOut(resources->context, 500, VerifyDatabaseTimerCallback, control);
	    XtAppAddTimeOut(resources->context, 0, VerifyDatabaseTimerCallback, control);
	}
	else
	{
	    restore_callback(EINVAL, NULL, NULL, resources);
	}
    }
    else
    {
	debug(0, "%s:%s(%d) - Backup already in progress.\n",
	    __FILE__, __FUNCTION__, __LINE__);
    }
}

static void
restoreAction(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
resourcesPtr resources;

    resources = WidgetToResources(w);
    restore(resources->current_page, resources, NULL);
}

static void
backup(Widget w, XtPointer client_data, XtPointer call_data)
{
resourcesPtr resources = (resourcesPtr)client_data;

    debug(9, "%s:%s(%d) - Backup is starting\n",
	__FILE__, __FUNCTION__, __LINE__);
    if (resources->backup_control == NULL)
    {
    int maximum = 0;
    backupControl_t *control = XtNew(backupControl_t);
    BlackBerry_t *device;
    Widget list;
    int selectedItemCount;
    XmStringTable selectedItems;
    int i;

	XtVaGetValues(w,
	    XmNuserData, &device,
	    NULL);
	debug(4, "%s:%s(%d) - device %08x\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    device->pin);

	XtSetSensitive(XtNameToWidget(w, ".OK"), False);
	XtSetSensitive(XtNameToWidget(w, ".Cancel"), False);
	XtSetSensitive(XtNameToWidget(w, ".Apply"), False);
	XtSetSensitive(XtNameToWidget(resources->main_window, "*File.button_0"), False);
	XtSetSensitive(XtNameToWidget(resources->main_window, "*Sync.button_0"), False);
	XtSetSensitive(XtNameToWidget(resources->notebook, ".PageScroller"), False);
	resources->backup_control = control;
	control->resources = resources;
	control->num_databases = 0;
	control->databases = NULL;
	control->expected = NULL;
	control->file = NULL;
	control->data = NULL;
	control->device = device;
	control->current_database = 0;
	control->current_record = 0;
	control->expected_records = 0;
	control->selectionbox = w;
	list = XtNameToWidget(w, "*ItemsList");
	XtVaGetValues(list,
	    XmNselectedItemCount, &selectedItemCount,
	    XmNselectedItems, &selectedItems,
	    NULL);
	debug(4, "%s:%s(%d) - %s %s %s %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    "item",
	    "index",
	    "records",
	    "database");
	for (i = 0; i < selectedItemCount; i++)
	{
	String item;
	int nr;

	    item = XmStringUnparse(selectedItems[i], 
		NULL, 0, XmCHARSET_TEXT,
		NULL, 0, XmOUTPUT_ALL);
	    nr = get_database_nr(device->desktop->database->database_table, item);
	    debug(4, "%s:%s(%d) - %4i %5i %7i \"%s\"\n",
		__FILE__, __FUNCTION__, __LINE__,
		i,
		get_database_index(device->desktop->database->database_table, item),
		nr,
		item);
	    control->num_databases++;
	    control->databases = (String *)XtRealloc((char *)control->databases, control->num_databases * sizeof(String *));
	    control->databases[control->num_databases - 1] = XtNewString(item);
	    control->expected = (int *)XtRealloc((char *)control->expected, control->num_databases * sizeof(int *));
	    control->expected[control->num_databases - 1] = nr;
	    maximum += nr;
	    XtFree(item);
	}
	debug(4, "%s:%s(%d) - %i record%s in %i database%s.\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    maximum,
	    ((maximum > 1) || (maximum == 0)) ? "s" : "",
	    control->num_databases,
	    control->num_databases > 1 ? "s" : "");
	if (control->num_databases > 0)
	{
	    XtVaSetValues(resources->progress,
		XmNminimum, maximum > 0 ? 0 : -1,
		XmNmaximum, maximum,
		XmNvalue, 0,
		NULL);
	    XmUpdateDisplay(resources->progress);
	    SetStatus(control->resources, "Retrieving ...");
	    //XtAppAddTimeOut(resources->context, 500, GetDatabaseTimerCallback, control);
	    XtAppAddTimeOut(resources->context, 0, GetDatabaseTimerCallback, control);
	}
	else
	{
	    resources->backup_control = control;
	    get_callback(0, NULL, resources);
	}
    }
    else
    {
	debug(0, "%s:%s(%d) - Backup already in progress.\n",
	    __FILE__, __FUNCTION__, __LINE__);
    }
}

static void
backupAction(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
resourcesPtr resources;

    resources = WidgetToResources(w);
    backup(resources->current_page, resources, NULL);
}

static void
DriverSignalCallback(XtPointer client_data, XtSignalId *id)
{
resourcesPtr resources = (resourcesPtr)client_data;
int numDevices = 0;

    debug(9, "%s:%s(%d)\n",
    	__FILE__, __FUNCTION__, __LINE__);
    while(resources->signal_list)
    {
    signalControlStruct *device = resources->signal_list;

	numDevices++;
	debug(7, "%s:%s(%d) - %08x connected = %s %i\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    device->pin,
	    device->connected ? "True" : "False",
	    numDevices);
	if (device->connected)
	{
	    AddDevice(device->blackberry, resources);
	}
	else
	{
	    RemoveDevice(device->blackberry, resources);
	}

	resources->signal_list = device->next;
	XtFree((char *)device);
    }
}

static void
device_signal(BlackBerry_t *device, XtPointer client_data, XtPointer call_data)
{
resourcesPtr resources = (resourcesPtr)client_data;
signalControlStruct *control = XtNew(signalControlStruct);

    debug(7, "%s:%s(%d) - %08x initialized = %s\n",
	__FILE__, __FUNCTION__, __LINE__,
	device->pin,
	resources ? (resources->initialized ? "True" : "False") : "What happened to my client_data!!!!");
    control->blackberry = device;
    control->connected = device->connected;
    control->pin = device->pin;
    control->next = resources->signal_list;
    resources->signal_list = control;
    device->monitor_fd = WatchFD;

    if (resources->initialized)
    {
	/* Don't do anything here!!! There is a good possibility that
	   this was called by a separate thread.
	 */
	 XtNoticeSignal(resources->driver_signal_id);
     }
     else
     {
	 DriverSignalCallback(resources, NULL);
     }
}

static void
FixPageNumbers(resourcesPtr resources)
{
bb_t *list = resources->widget_list;
int first = 1, current = 1, last = 1;
XmNotebookCallbackStruct call_data;
XmNotebookPageInfo current_page, new_page;

    XmNotebookGetPageInfo(resources->notebook, current, &current_page);
    if (NumDevices(resources->widget_list) > 0)
    {
	while (list)
	{
	    XtVaSetValues(list->selectionbox,
		XmNpageNumber, last,
		NULL);
	    last++;
	    list = list->next;
	}
	XtVaSetValues(resources->notebook,
	    XmNfirstPageNumber, first,
	    XmNcurrentPageNumber, current,
	    XmNlastPageNumber, NumDevices(resources->widget_list),
	    NULL);
	XmNotebookGetPageInfo(resources->notebook, current, &new_page);
	call_data.reason = XmCR_NONE;
	call_data.event = NULL;
	call_data.page_number = new_page.page_number;
	call_data.page_widget = new_page.page_widget;
	call_data.prev_page_number = current_page.page_number;
	call_data.prev_page_widget = current_page.page_widget;
	XtVaSetValues(resources->notebook,
	    XmNcurrentPageNumber, current,
	    NULL);
	XtCallCallbacks(resources->notebook, XmNpageChangedCallback, &call_data);
    }
    else
    {
    }
}

static void
SelectionCallback(Widget w, XtPointer client_data, XtPointer call_data)
{
resourcesPtr resources = (resourcesPtr)client_data;
XmListCallbackStruct *cbs = (XmListCallbackStruct *)call_data;

    debug(9, "%s:%s(%d) - selected_item_count = %i\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	cbs->selected_item_count);
    if (cbs->selected_item_count > 0)
    {
    }
    else
    {
    }
    XtSetSensitive(XtNameToWidget(resources->file_pulldown, "*button_2"), (cbs->selected_item_count == 1));
}

static void
_send_password(XtPointer client_data, XtIntervalId *tid)
{
password_t *blackberry_password = (password_t *)client_data;

    debug(9, "%s:%s(%d) - %08x\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	blackberry_password->blackberry->pin);
    (*blackberry_password->blackberry->send_password)(blackberry_password->blackberry, blackberry_password->password);
    XtFree(blackberry_password->password);
    XtFree((char *)blackberry_password);
}

static void
send_password(Widget w, XtPointer client_data, XtPointer call_data)
{
BlackBerry_t *blackberry = (BlackBerry_t *)client_data;

    debug(9, "%s:%s(%d) - %08x\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	blackberry->pin);

    if (blackberry->send_password)
    {
    Widget hidden;
    password_t *pwd = XtNew(password_t);

	hidden = XtNameToWidget(w, ".Hidden");

	pwd->password = XmTextFieldGetString(hidden);
	debug(4, "%s:%s(%d) - %08x password >%s<\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    blackberry->pin,
	    pwd->password);

	/* rws 17 Sep 2006
	   Can't call send_password directly because, in the case of a
	   failure get_password will be called before the last one has
	   finished. Probably something to do with the reader running
	   in a separate thread.
	 */
	pwd->blackberry = blackberry;
	XtAppAddTimeOut(XtWidgetToApplicationContext(w),
	    500, _send_password, pwd);
    }
}

static void
mask_password(Widget text, XtPointer client_data, XtPointer call_data)
{
XmTextVerifyCallbackStruct *cbs = (XmTextVerifyCallbackStruct *)call_data;
Widget hidden;

    /*
    debug(0, "%s:%s(%d) - currInsert %i newInsert %i\n\tstartPos %i endPos %i length %i\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	cbs->currInsert, cbs->newInsert,
    	cbs->startPos, cbs->endPos,
    	cbs->text->length);
    	*/

    hidden = XtNameToWidget(XtParent(text), ".Hidden");
    XmTextFieldReplace(hidden, cbs->startPos, cbs->endPos, cbs->text->ptr);
    if (cbs->text && cbs->text->ptr && cbs->text->length > 0)
    {
	memset(cbs->text->ptr, '*', cbs->text->length);
    }
}

static void
get_password(BlackBerry_t *blackberry)
{
static Widget dialog = NULL;
bb_t *bb = (bb_t *)blackberry->ui_private;
resourcesPtr resources = bb->resources;;

    debug(9, "%s:%s(%d) - %08x\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	blackberry->pin);

    if (dialog == NULL)
    {
    Widget text;
    Widget hidden;
    String initial;

    	dialog = XmCreatePromptDialog(resources->shell, "Password", NULL, 0);
	hidden = XmCreateTextField(dialog, "Hidden", NULL, 0);
    	text = XtNameToWidget(dialog, ".Text");

	XtVaSetValues(hidden,
	    XmNeditable, False,
	    NULL);
	initial = XmTextFieldGetString(text);
	XmTextFieldSetString(hidden, initial);
	if (initial && strlen(initial) > 0)
	{
	    memset(initial, '*', strlen(initial));
	    XmTextFieldSetString(text, initial);
	}
	XtFree(initial);
	XtAddCallback(text, XmNmodifyVerifyCallback, mask_password, NULL);
	if (XtIsSensitive(hidden))
	{
	    XtManageChild(hidden);
	}
    }
    XtRemoveAllCallbacks(dialog, XmNokCallback);
    XtAddCallback(dialog, XmNokCallback, send_password, blackberry);
    XtManageChild(dialog);
}

static void
DisplayStats(XtPointer client_data, XtIntervalId *id)
{
bb_t *new_device = (bb_t *)client_data;
unsigned int tx, rx;
Widget arrows;
Pixmap pixmap;

    debug(9, "%s:%s(%d) - %i %i %i %i\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	new_device->device->stats.tx,
    	new_device->device->stats.rx,
    	new_device->stats.tx,
    	new_device->stats.rx
    	);

    arrows = XtNameToWidget(new_device->selectionbox, "*Arrows");
    if (arrows)
    {
    String which;

	tx = new_device->stats.tx - new_device->device->stats.tx;
	rx = new_device->stats.rx - new_device->device->stats.rx;
	if (tx > 0 && rx > 0)
	{
	    if (tx * 10 > rx)
	    {
		which = "down";
	    }
	    else if (rx * 10 > tx)
	    {
		which = "up";
	    }
	    else
	    {
		which = "both";
	    }
	}
	else if (tx > 0 && tx > rx)
	{
	    which = "down";
	}
	else if (rx > 0 && rx > tx)
	{
	    which = "up";
	}
	else
	{
		which = "none";
	}
	pixmap = XmGetPixmap(XtScreen(new_device->selectionbox),
	    which, None, None);
	XtVaSetValues(arrows,
	    XmNlabelPixmap, pixmap,
	    NULL);
    }

    new_device->stats = new_device->device->stats;
    new_device->stats_timer = XtAppAddTimeOut(XtWidgetToApplicationContext(new_device->selectionbox),
	    125, DisplayStats, new_device);
}

static void
AddDevice(BlackBerry_t *blackberry, resourcesPtr resources)
{
Widget selectionbox = NULL;
int i;
WidgetList kids;
Cardinal numKids;

    debug(9, "%s:%s(%d) - %08x %i\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	blackberry->pin, blackberry->fd);

    XtVaGetValues(resources->notebook,
	XmNchildren, &kids,
	XmNnumChildren, &numKids,
	NULL);
    for (i = 0; i < numKids; i++)
    {
    Boolean mapFlag = True;

	XtVaGetValues(kids[i],
	    XmNmappedWhenManaged, &mapFlag,
	    NULL);
	if (mapFlag == False)
	{
	    debug(8, "%s:%s(%d) - %08x Found un-mapped widget\n",
		__FILE__, __FUNCTION__, __LINE__,
		blackberry->pin);
	    selectionbox = kids[i];
	    break;
	}
    }
    if (selectionbox == NULL)
    {
	debug(8, "%s:%s(%d) - %08x Creating new widget\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    blackberry->pin);
	selectionbox = XltCreateSelectionBox(resources->notebook, "selector", NULL, 0);
	XtAddCallback(selectionbox, XmNokCallback, backup, resources);
	XtAddCallback(selectionbox, XmNapplyCallback, restore, resources);
	XtAddCallback(selectionbox, XmNcancelCallback, FileMenuCallback, 0);
	XtAddCallback(XtNameToWidget(selectionbox, "*ItemsList"), XmNbrowseSelectionCallback, SelectionCallback, resources);
	XtAddCallback(XtNameToWidget(selectionbox, "*ItemsList"), XmNextendedSelectionCallback, SelectionCallback, resources);
	XtAddCallback(XtNameToWidget(selectionbox, "*ItemsList"), XmNmultipleSelectionCallback, SelectionCallback, resources);
	XtAddCallback(XtNameToWidget(selectionbox, "*ItemsList"), XmNsingleSelectionCallback, SelectionCallback, resources);
	XtManageChild(XtNameToWidget(selectionbox, ".Apply"));
	XtManageChild(selectionbox);
	XtManageChild(XtNameToWidget(resources->notebook, ".PageScroller"));
    }
    XtVaSetValues(selectionbox,
	XmNuserData, blackberry,
	XmNmappedWhenManaged, True,
	NULL);
    {
    bb_t *new_device;

	new_device = XtNew(bb_t);
	blackberry->ui_private = new_device;
	new_device->selectionbox = selectionbox;
	new_device->device = blackberry;
	new_device->resources = resources;
	new_device->stats_timer = XtAppAddTimeOut(XtWidgetToApplicationContext(selectionbox),
		125, DisplayStats, new_device);
	new_device->stats.tx = 0;
	new_device->stats.rx = 0;
	if (blackberry->fd >= 0)
	{
	    new_device->input_id = XtAppAddInput(resources->context, blackberry->fd, (XtPointer)(XtInputReadMask), serial_input, new_device);
	}
	else
	{
	    new_device->input_id = 0;
	}
	add_list_items(resources, selectionbox, blackberry);
	new_device->next = resources->widget_list;
	resources->widget_list = new_device;
    }
    blackberry->disconnect_callback = device_signal;
    FixPageNumbers(resources);
    blackberry->get_password = get_password;
    blackberry->connect_to_desktop = IsConnectToDesktop(selectionbox);
}

static void
RemoveDevice(BlackBerry_t *blackberry, resourcesPtr resources)
{
bb_t *list = resources->widget_list;
bb_t *previousListItem = NULL;
Widget box = NULL;

    /*  Be carefull here. The struct that blackberry points at
        may no longer exist.
     */
    debug(9, "%s:%s(%d) - numDevices = %i\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	NumDevices(resources->widget_list));
    while (list)
    {
    	if (list->device == blackberry)
    	{
	bb_t *tmp = list;

	    debug(8, "%s:%s(%d) - Found the one to delete %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		XtName(list->selectionbox));
	    if (list->input_id)
	    {
	    	XtRemoveInput(list->input_id);
	    	list->input_id = 0;
	    }
	    XtRemoveTimeOut(list->stats_timer);
	    if (NumDevices(resources->widget_list) > 1)
	    {
		XtUnmanageChild(list->selectionbox);
		box = list->selectionbox;
	    }
	    else
	    {
		XtVaSetValues(list->selectionbox,
		    XmNmappedWhenManaged, False,
		    NULL);
	    }
	    if (NumDevices(resources->widget_list) > 2)
	    {
		XtManageChild(XtNameToWidget(resources->notebook, ".PageScroller"));
	    }
	    else
	    {
		XtUnmanageChild(XtNameToWidget(resources->notebook, ".PageScroller"));
	    }
	    if (resources->widget_list == list)
	    {
	    	resources->widget_list = list->next;
	    	list = list->next;
	    }
	    else
	    {
		if (previousListItem)
		{
		    previousListItem->next = list->next;
		}
		list = list->next;
	    }
	    XtFree((char *)tmp);
    	}
    	else
    	{
	    previousListItem = list;
	    list = list->next;
    	}
    }
    FixPageNumbers(resources);
    if (box)
    {
    	XtDestroyWidget(box);
    }
}

static void
get_callback(int len, char *record, void *client_data)
{
resourcesPtr resources = (resourcesPtr)client_data;
backupControl_t *control;
int value;
int maximum;

    if (resources)
    {
	control = resources->backup_control;
	XtVaGetValues(resources->progress,
	    XmNvalue, &value,
	    XmNmaximum, &maximum,
	    NULL);
	debug(len == 0 ? 4 : 5, "%s:%s(%d) - device %08x \"%s\" total %i %i of %i len %i\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    control->device->pin,
	    control->databases ? control->databases[control->current_database] : "",
	    value,
	    control->current_record,
	    control->expected_records,
	    len);
	if (len == 0)
	{
	    if (control->file)
	    {
		fclose(control->file);
		control->file = NULL;
	    }
	    if (control->current_record != control->expected_records)
	    {
		debug(0, "%s:%s(%d) - device %08x \"%s\" retrieved %i of %i records!!!\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    control->device->pin,
		    control->databases[control->current_database],
		    control->current_record,
		    control->expected_records);
	    }
	    control->current_database++;
	    control->current_record = 0;
	    if (control->current_database < control->num_databases)
	    {
		XtAppAddTimeOut(resources->context, 0, GetDatabaseTimerCallback, control);
	    }
	    else
	    {
		VaSetStatus(control->resources, "%08x Retrieved %i of %i record%s from %i database%s",
		    control->device->pin,
		    value, maximum,
		    maximum == 1 ? "" : "s",
		    control->num_databases,
		    control->num_databases == 1 ? "" : "s");
		for (; control->num_databases > 0; control->num_databases--)
		{
		    XtFree(control->databases[control->num_databases -1]);
		}
		XtFree((char *)control->databases);
		XtFree((char *)control->expected);
#if 0
		if (IsAutoSave(control->selectionbox) && control->device->close)
		{
		    (*control->device->close)(control->device);
		    device_signal(control->device, resources);
		}
#endif
		resources->auto_actions &= ~(1 << ActionSave);
		if (resources->auto_actions & (1 << ActionSync))
		{
		    debug(0, "%s:%s(%d) - Sync after save\n", __FILE__, __FUNCTION__, __LINE__);
		    SyncMenuCallback(resources->current_page, (XtPointer)0, NULL);
		}
		else
		{
		    XtSetSensitive(XtNameToWidget(control->selectionbox, ".OK"), True);
		    XtSetSensitive(XtNameToWidget(control->selectionbox, ".Cancel"), True);
		    XtSetSensitive(XtNameToWidget(control->selectionbox, ".Apply"), True);
		    XtSetSensitive(XtNameToWidget(resources->main_window, "*File.button_0"), True);
		    XtSetSensitive(XtNameToWidget(resources->main_window, "*Sync.button_0"), True);
		    XtSetSensitive(XtNameToWidget(resources->notebook, ".PageScroller"), True);
		    XmProcessTraversal(XtNameToWidget(control->selectionbox, ".OK"), XmTRAVERSE_CURRENT);
		}
		XtFree((char *)control);
		resources->backup_control = NULL;
	    }
	}
	else
	{
	    VaSetStatus(control->resources, "%08x \"%s\" %i(%i)",
		control->device->pin,
		control->databases[control->current_database],
		control->current_record, control->expected_records);
	    if (control->file)
	    {
	    uint32_t n_len = htonl(len);

		fwrite(&n_len, sizeof(uint32_t), 1, control->file);
		fwrite(record, len, 1, control->file);
	    }
	    if (control->current_record > control->expected_records)
	    {
		maximum++;
	    }
	    value++;
	    control->current_record++;
	}
	maximum = maximum >= value ? maximum : value,
	XtVaSetValues(resources->progress,
	    XmNminimum, maximum > 0 ? 0 : -1,
	    XmNvalue, value,
	    XmNmaximum, maximum,
	    NULL);
	XmUpdateDisplay(resources->progress);
    }
    else
    {
	debug(0, "%s:%s(%d) - What happened to my client data!!!\n",
	    __FILE__, __FUNCTION__, __LINE__);
    }
}

static void
SignalCallback(XtPointer client_data, XtSignalId *id)
{
resourcesPtr resources = (resourcesPtr)client_data;

    debug(9, "%s:%s(%d) - closing\n", __FILE__, __FUNCTION__, __LINE__);
    FileMenuCallback(resources->shell, 0, NULL);
}

static void
signal_handler(int signum)
{
    debug(9, "%s:%s(%d) - closing\n", __FILE__, __FUNCTION__, __LINE__);
    XtNoticeSignal(signal_id);
}

static void
DoNothingTimer(XtPointer client_data, XtIntervalId *tid)
{
resourcesPtr resources = (resourcesPtr)client_data;

    /* This is here simply to get X to leave it's select
       so that we notice signals coming from the driver.
       Otherwise you have to wait till an XEvent happens.
     */
    XtAppAddTimeOut(resources->context, 250, DoNothingTimer, client_data);
}

static void
OpenTimerCallback(XtPointer client_data, XtIntervalId *tid)
{
resourcesPtr resources = (resourcesPtr)client_data;
BlackBerry_t *usb_devs = NULL;
BlackBerry_t *serial_dev = NULL;
BlackBerry_t *device_index = NULL;
int sig;

    resources->widget_list = NULL;
    resources->initialized = True;
    signal_id = XtAppAddSignal(resources->context, SignalCallback, resources);
    resources->driver_signal_id = XtAppAddSignal(resources->context, DriverSignalCallback, resources);
    SetStatus(resources, "Scanning USB ...");
    usb_devs = usb_bb_open(device_signal, resources);
    if (usb_devs)
    {
    }
    else
    {
	debug(9, "%s:%s(%d) - No usb devices found\n",
	    __FILE__, __FUNCTION__, __LINE__);
    }
    SetStatus(resources, "Scanning serial ...");
    if (resources->device_file)
    {
	serial_dev = serial_bb_open(resources->device_file, device_signal, resources);
	if (serial_dev)
	{
	    serial_dev->next = usb_devs;
	    usb_devs = serial_dev;
	}
	else
	{
	    /*
	    debug(0, "%s:%s(%d) - Couldn't open %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		resources->device_file);
		*/
	}
    }
    for (sig = 0; sig < 32; sig++)
    {
	switch (sig)
	{
	case SIGTERM:
	case SIGHUP:
	case SIGINT:
	    signal(sig, signal_handler);
	    break;
	default:
	    break;
	};
    }
    device_index = usb_devs;
    while (device_index)
    {
	VaSetStatus(resources, "Found %08x", device_index->pin);
	AddDevice(device_index, resources);
	device_index = device_index->next;
    }
    XtAppAddTimeOut(resources->context, 250, DoNothingTimer, resources);
    SetStatus(resources, "Done");
    resources->initialized = True;
}

int
main(int argc, char *argv[])
{
int exit_status = EXIT_SUCCESS;
Boolean info_only = False;
resources_t *resources;

    resources = XtNew(resources_t);
    ListInitialize(&resources->monitor_list);
    resources->widget_list = NULL;
    resources->signal_list = NULL;
    resources->backup_control = NULL;
    resources->sync_env = NULL;
    resources->auto_actions = 0;
    XtToolkitThreadInitialize();
    resources->shell = XtVaOpenApplication(&resources->context,
    	"XmBB",
	opTable, XtNumber(opTable),
    	&argc, argv,
    	FallBackResources,
	applicationShellWidgetClass,
	NULL);
    if (argc > 1)
    {
    int i;

	fprintf(stderr, "Unrecognized command argument(s)\n");
	for (i = 1; i < argc; i++)
	{
	    fprintf(stderr, "\t%s\n", argv[i]);
	}
	exit_status = EXIT_FAILURE;
    }
    else
    {
#ifdef HAVE_XLTREMOTE
	XltRemoteInitialize(resources->shell, "_XMBLACKBERRY_COMMAND", &resources->command_atom);
#endif
	XtGetApplicationResources(resources->shell,
	    resources,
	    AppResources, XtNumber(AppResources),
	    NULL, 0);
	if (resources->fallback)
	{
	    XltDisplayFallbackResources(FallBackResources);
	    info_only = True;
	}
	if (resources->help)
	{
	    XltDisplayOptions(opTable, XtNumber(opTable));
#ifdef HAVE_XLTREMOTE
	    XltDisplayActions(actions, XtNumber(actions));
#endif
	    info_only = True;
	}
	DebugLevel = resources->debug;
#ifdef HAVE_XLTREMOTE
	if (resources->remote_command)
	{
	    XltRemotePostAction(resources->shell, resources->command_atom, resources->remote_command);
	    info_only = True;
	}
#endif
	if (! info_only)
	{
	    XmRepTypeInstallTearOffModelConverter();
	    XtAppAddActions(resources->context, actions, XtNumber(actions));
	    XmAddWMProtocolCallback(resources->shell,
	    	XInternAtom(XtDisplay(resources->shell), "WM_DELETE_WINDOW", False),
	    	FileMenuCallback, (XtPointer)0);
	    /*
	    XltSetClientIcon(resources->shell, rim_8700r_50x50);
	    */
	    XltSetClientIcon(resources->shell, bbicon_xpm);
	    StrokeInitialize(resources->shell);
	    resources->sync_env = sync_init(resources->shell);
	    XltSoundInitialize(resources->shell);
	    resources->main_window = XmCreateMainWindow(resources->shell,
		"BB", NULL, 0);
	    XtVaSetValues(resources->main_window,
	    	XmNuserData, resources,
	    	NULL);
	    resources->menu_bar = XmVaCreateSimpleMenuBar(resources->main_window,
		"BBMenu",
		XmVaCASCADEBUTTON, NULL, NULL, /* File */
		XmVaCASCADEBUTTON, NULL, NULL, /* Help */
		XmVaCASCADEBUTTON, NULL, NULL, /* Options */
		XmVaCASCADEBUTTON, NULL, NULL, /* Sync */
		NULL);
	    if (!resources->sync_env)
	    {
		XtSetSensitive(XtNameToWidget(resources->menu_bar, ".button_3"), False);
	    }

	    resources->file_pulldown = XmVaCreateSimplePulldownMenu(resources->menu_bar,
	    	"File", 0, FileMenuCallback,
	    	XmVaPUSHBUTTON, NULL, NULL, NULL, NULL, /* Quit */
	    	XmVaPUSHBUTTON, NULL, NULL, NULL, NULL, /* Raw */
	    	XmVaPUSHBUTTON, NULL, NULL, NULL, NULL, /* View */
	    	NULL);
	    XtVaSetValues(XtNameToWidget(resources->file_pulldown, ".button_0"),
	    	XmNpositionIndex, XmLAST_POSITION,
	    	NULL);
	    XtAddCallback(resources->file_pulldown, XmNmapCallback, FilePopupCallback, resources);

	    resources->option_pulldown = XmVaCreateSimplePulldownMenu(resources->menu_bar,
	    	"Options", 2, OptionMenuCallback,
	    	XmVaTITLE, NULL,                         /* On Connect ... */
	    	XmVaCHECKBUTTON, NULL, NULL, NULL, NULL, /* SetTime */
	    	XmVaCHECKBUTTON, NULL, NULL, NULL, NULL, /* Save */
	    	XmVaCHECKBUTTON, NULL, NULL, NULL, NULL, /* Connect to Desktop */
	    	NULL);
	    XtAddCallback(XtNameToWidget(resources->option_pulldown, ".button_0"), XmNvalueChangedCallback, SaveToggle, resources);
	    XtAddCallback(XtNameToWidget(resources->option_pulldown, ".button_1"), XmNvalueChangedCallback, SaveToggle, resources);
	    XtAddCallback(XtNameToWidget(resources->option_pulldown, ".button_2"), XmNvalueChangedCallback, SaveToggle, resources);
	    XtAddCallback(resources->option_pulldown, XmNmapCallback, OptionPopupCallback, resources);

	    resources->sync_pulldown = XmVaCreateSimplePulldownMenu(resources->menu_bar,
	    	"Sync", 3, SyncMenuCallback,
	    	XmVaPUSHBUTTON, NULL, NULL, NULL, NULL, /* Sync */
	    	XmVaPUSHBUTTON, NULL, NULL, NULL, NULL, /* SaveSyncRestore */
	    	XmVaSEPARATOR,
	    	XmVaPUSHBUTTON, NULL, NULL, NULL, NULL, /* Configure */
	    	NULL);
	    XtAddCallback(resources->sync_pulldown, XmNmapCallback, SyncPopupCallback, resources);

	    XtVaSetValues(resources->menu_bar,
	    	XmNmenuHelpWidget, XtNameToWidget(resources->menu_bar, ".button_1"),
	    	NULL);
	    resources->help_pulldown = XmVaCreateSimplePulldownMenu(resources->menu_bar,
	    	"Help", 1, HelpMenuCallback,
	    	XmVaPUSHBUTTON, NULL, NULL, NULL, NULL, /* About */
	    	XmVaPUSHBUTTON, NULL, NULL, NULL, NULL, /* Version */
	    	NULL);

	    resources->notebook = XmCreateNotebook(resources->main_window, "Notebook", NULL, 0);
	    XtAddCallback(resources->notebook, XmNpageChangedCallback, pageChange, resources);
	    {
	    Widget selectionbox;

		selectionbox = XltCreateSelectionBox(resources->notebook, "selector", NULL, 0);
		XtAddCallback(selectionbox, XmNokCallback, backup, resources);
		XtAddCallback(selectionbox, XmNapplyCallback, restore, resources);
		XtAddCallback(selectionbox, XmNcancelCallback, FileMenuCallback, 0);
		XtAddCallback(XtNameToWidget(selectionbox, "*ItemsList"), XmNbrowseSelectionCallback, SelectionCallback, resources);
		XtAddCallback(XtNameToWidget(selectionbox, "*ItemsList"), XmNextendedSelectionCallback, SelectionCallback, resources);
		XtAddCallback(XtNameToWidget(selectionbox, "*ItemsList"), XmNmultipleSelectionCallback, SelectionCallback, resources);
		XtAddCallback(XtNameToWidget(selectionbox, "*ItemsList"), XmNsingleSelectionCallback, SelectionCallback, resources);
		XtManageChild(XtNameToWidget(selectionbox, ".Apply"));
		XtVaSetValues(selectionbox,
		    XmNmappedWhenManaged, False,
		    NULL);
		XtManageChild(selectionbox);
	    }
	    XtUnmanageChild(XtNameToWidget(resources->notebook, ".PageScroller"));

	    {
	    Widget form;

	    	form = XmCreateForm(resources->main_window, "Status", NULL, 0);
		XtVaSetValues(resources->main_window,
		    XmNmessageWindow, form,
		    NULL);
	    	resources->progress = XmCreateScale(form, "Progress", NULL, 0);
		XtVaSetValues(resources->progress,
		    XmNorientation, XmHORIZONTAL,
		    XmNtopAttachment, XmATTACH_FORM,
		    XmNbottomAttachment, XmATTACH_FORM,
		    XmNrightAttachment, XmATTACH_FORM,
		    XmNeditable, False,
		    NULL);
	    	resources->message = XmCreateLabel(form, "Messages", NULL, 0);
		XtVaSetValues(resources->message,
		    XmNtopAttachment, XmATTACH_FORM,
		    XmNbottomAttachment, XmATTACH_FORM,
		    XmNleftAttachment, XmATTACH_FORM,
		    XmNrightAttachment, XmATTACH_WIDGET,
		    XmNrightWidget, resources->progress,
		    XmNresizable, False,
		    NULL);
		XtManageChild(resources->progress);
		XtManageChild(resources->message);
		XtManageChild(form);
	    }
	    XtManageChild(resources->menu_bar);
	    XtManageChild(resources->notebook);
	    XtManageChild(resources->main_window);
	    XtRealizeWidget(resources->shell);
	    XtAppAddTimeOut(resources->context, 0, OpenTimerCallback, resources);
	    XltWaitTillMapped(resources->shell);
	    if (resources->redirect_errors && DebugLevel < 1)
	    {
		XltRedirectStdErr(resources->shell);
	    }
	    resources->auto_save_list = ParseTextList(resources->autoSelectList);
	    XtAppMainLoop(resources->context);
	}
	else
	{
	}
    }
    return(exit_status);
}
