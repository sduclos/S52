// s52gv.c: test driver for libS52.so, libgv.so, libGDAL.so --no python
//          Derived from Frank Warmerdan openev/testmain.c rev 1.28
//
// Project:  OpENCview/OpenEV



#include "S52.h"                // S52_loadCell()

#include <gtk/gtk.h>            // gtk_init(), ...

// check this: GV use glib-1.0 but S52 use glib-2.0
#include <glib.h>               // GString

#include "gvviewarea.h"         // GV_VIEW_AREA()
#include "gvtoolbox.h"          // GvToolBox
#include "gvviewlink.h"         // GvViewLink
#include "gvundo.h"             // gv_undo_register_data()
#include "gvselecttool.h"       // gv_selection_tool_new()
#include "gvzoompantool.h"      // gv_zoompan_tool_new()
#include "gvpointtool.h"        // gv_point_tool_new()
#include "gvlinetool.h"         // gv_line_tool_new()
#include "gvareatool.h"         // gv_area_tool_new()
#include "gvnodetool.h"         // gv_node_tool_new()
#include "gvroitool.h"          // gv_roi_tool_new()

#include "gvshapeslayer.h"      // GvShapesLayerClass, GvShapesLayer, GvShapes


static GvToolbox  *toolbox      = NULL;
static GvViewLink *link         = NULL;

static GtkWidget  *VecView      = NULL;

// FIXME: get this programmaticaly
#define DRVNAME "S57"


static void _key_press_cb( GtkObject * object, GdkEventKey * event )

{
    GvViewArea     *view = GV_VIEW_AREA(object);

    if( event->keyval == 't' ) {
        GTimeVal      cur_time;
        //double    start_time = g_get_current_time_as_double();
        double    start_time = 0.0;
        double    end_time, spf;
        int       i, frame_count = 20;

        g_get_current_time( &cur_time );
        start_time = cur_time.tv_sec + cur_time.tv_usec / 1000000.0;

        for( i = 0; i < frame_count; i++ )
            gv_view_area_expose(GTK_WIDGET(view), NULL);

        g_get_current_time( &cur_time );
        end_time = cur_time.tv_sec + cur_time.tv_usec / 1000000.0;
        //end_time =  g_get_current_time_as_double();

        spf = (end_time - start_time) / frame_count;
        printf( "Speed is %.2ffps\n", 1.0 / spf );
    }
}


static void _toolbar_callback(GtkWidget *widget, gpointer data)
{
    gv_toolbox_activate_tool(toolbox, (gchar*)data);
}

/*
static void _link_callback(GtkWidget *widget, gpointer data)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
        gv_view_link_enable(link);
    else
        gv_view_link_disable(link);
}
*/

static GtkWidget *_create_toolbar()
{
    GtkWidget *win;
    GtkWidget *toolbar;
    GtkWidget *but;

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

#ifdef S52_USE_GTK2
    toolbar = gtk_toolbar_new();
#else
    toolbar = gtk_toolbar_new(GTK_ORIENTATION_VERTICAL, GTK_TOOLBAR_TEXT);
#endif

    gtk_container_add(GTK_CONTAINER(win), toolbar);

    but =
        gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                                   GTK_TOOLBAR_CHILD_RADIOBUTTON,
                                   NULL,
                                   "Zoom",
                                   "Zoom tool",
                                   NULL,
                                   NULL,
                                   GTK_SIGNAL_FUNC(_toolbar_callback),
                                   (void *) "zoompan");

    but =
        gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                                   GTK_TOOLBAR_CHILD_RADIOBUTTON,
                                   but,
                                   "Select",
                                   "Selection tool",
                                   NULL,
                                   NULL,
                                   GTK_SIGNAL_FUNC(_toolbar_callback),
                                   (void *) "select");

    but =
        gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                                   GTK_TOOLBAR_CHILD_RADIOBUTTON,
                                   but,
                                   "Draw Points",
                                   "Point drawing tool",
                                   NULL,
                                   NULL,
                                   GTK_SIGNAL_FUNC(_toolbar_callback),
                                   (void *) "point");
    but =
        gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                                   GTK_TOOLBAR_CHILD_RADIOBUTTON,
                                   but,
                                   "Draw Line",
                                   "Line drawing tool",
                                   NULL,
                                   NULL,
                                   GTK_SIGNAL_FUNC(_toolbar_callback),
                                   (void *) "line");
    but =
        gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                                   GTK_TOOLBAR_CHILD_RADIOBUTTON,
                                   but,
                                   "Draw Area",
                                   "Area drawing tool",
                                   NULL,
                                   NULL,
                                   GTK_SIGNAL_FUNC(_toolbar_callback),
                                   (void *) "area");

    but =
        gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                                   GTK_TOOLBAR_CHILD_RADIOBUTTON,
                                   but,
                                   "Edit Node",
                                   "Node edit tool",
                                   NULL,
                                   NULL,
                                   GTK_SIGNAL_FUNC(_toolbar_callback),
                                   (void *) "node");
    but =
        gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                                   GTK_TOOLBAR_CHILD_RADIOBUTTON,
                                   but,
                                   "Draw ROI",
                                   "ROI drawing tool",
                                   NULL,
                                   NULL,
                                   GTK_SIGNAL_FUNC(_toolbar_callback),
                                   (void *) "roi");
/*
    but =
        gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                                   GTK_TOOLBAR_CHILD_TOGGLEBUTTON,
                                   NULL,
                                   "Link",
                                   "Link views together",
                                   NULL,
                                   NULL,
                                   GTK_SIGNAL_FUNC(_link_callback),
                                   NULL);
*/

    gtk_signal_connect(GTK_OBJECT(win), "delete-event",
                       GTK_SIGNAL_FUNC(gtk_main_quit), NULL);

    gtk_widget_show(toolbar);
    gtk_widget_show(win);

    return win;
}

//static int _gv_loadLayer_cb(const char *filename, GvData *data)
static int  _gv_loadLayer_cb(const char *filename, void *data)
// callback to load this data into GV layer
// we need this all the way up here because of GvViewArea
{
    //GvShapesLayer *gvlayer    = (GvShapesLayer *)data;
    GvShapes      *shapes_data = GV_SHAPES(data);
    GvViewArea    *view        = GV_VIEW_AREA(VecView);
    GObject       *layer       = NULL;

    //gv_data_set_property(GV_DATA(shape_data), "_filename",        _filename);
    gv_data_set_property(GV_DATA(shapes_data), "_ogr_driver_name", DRVNAME);

    gv_undo_register_data(GV_DATA(shapes_data));

    layer = gv_shapes_layer_new(shapes_data);

    gv_view_area_add_layer       (view, layer);
    gv_view_area_set_active_layer(view, layer);

    return 1;
}

static void _loadCell(const char *filename)
{
    // S52_init() is done via gv_shapes_layer_new()
#ifdef S52_USE_DOTPITCH
    S52_init(10,10,30,30);
#else
    S52_init();
#endif


    S52_loadCell(filename, _gv_loadLayer_cb);

    return;
}

static void _destroy(GtkWidget *widget, gpointer data)
{
    S52_done();

    gtk_main_quit();
}

static void _usage(char *arg)
{
    printf("Usage: %s [-h] [-O] [-f] S57..\n", arg);
    printf("\t-h\t:this help\n");
    printf("\t-O\t:load S57 directly via OGR\n");
    printf("\t-f\t:S57 file to load --else search .conf\n");

    exit(0);
}

static char * _option(int argc, char **argv)
{
    char *prgnm = *argv;

    argc--;  argv++;
    while (argc--) {

        if (NULL == *argv)
            break;

        if (0 == strcmp(*argv, "-h"))
            _usage(prgnm);

        if (0 == strcmp(*argv, "-0"))
            setenv("S52_USE_OGR", "Yes", 1);

        if (0 == strcmp(*argv, "-f"))
            ;

        ++argv;
    }

    return *argv;
}

static int  _dumpSetUp()
{
    {
        //unsigned int value = HUGE_VAL;
        //int nan   = NaN;
        //PRINTF("int infinity value = -2147483648 =  HUGE_VAL = %i\n", value);
        //PRINTF("int infinity size = 8 = sizeof(HUGE_VAL) = %i\n", sizeof(value));
        //PRINTF("int          : sizeof(int)    = %i\n", sizeof(int));
        //PRINTF("float        : sizeof(float)  = %i\n", sizeof(float));
        //PRINTF("double       : sizeof(double) = %i\n", sizeof(double));
        //PRINTF("unsigned long: sizeof(unsigned long)= %i\n",sizeof(unsigned long));
        //exit(0);
    }

    return 1;
}

int main(int argc, char **argv)
{
    GtkWidget *win;
    GtkWidget *twin;   // toolbar window
    GtkWidget *swin;

    const char *filename = NULL;

    //g_thread_init(NULL);
    //gdk_threads_init();
    gdk_threads_enter();

    gtk_init(&argc, &argv);


    printf("%s\n", S52_version());


    //printf("float: %.02f \n", 3.1416);  // 3.14
    //printf("float: %.2f \n", 3.1416);   // 3.14
    //printf("float: %.02f \n", 3.1);     // 3.10
    //printf("float: %.2f \n", 3.1);      // 3.10
    //printf("float: %.2f \n", 3.665);      // 3.67
    //printf("float: %.2f \n", 3.664);      // 3.66
    //return 1;


    filename = _option(argc, argv);
    _dumpSetUp();

    VecView = gv_view_area_new();
    if (NULL == VecView){
        printf("main.c:ERROR: VecView == NULL!!  no OpenGL .. exit!\n");
        exit(0);
    }

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_signal_connect(GTK_OBJECT(win), "destroy", GTK_SIGNAL_FUNC(_destroy), NULL);

    toolbox = GV_TOOLBOX(gv_toolbox_new());
    gv_toolbox_add_tool(toolbox, "select",  gv_selection_tool_new());
    gv_toolbox_add_tool(toolbox, "zoompan", gv_zoompan_tool_new());
    gv_toolbox_add_tool(toolbox, "point",   gv_point_tool_new());
    gv_toolbox_add_tool(toolbox, "line",    gv_line_tool_new());
    gv_toolbox_add_tool(toolbox, "area",    gv_area_tool_new());
    gv_toolbox_add_tool(toolbox, "node",    gv_node_tool_new());
    gv_toolbox_add_tool(toolbox, "roi",     gv_roi_tool_new());

    link = GV_VIEW_LINK(gv_view_link_new());


    gtk_window_set_default_size( GTK_WINDOW(win), 800, 600 );

    // 2D
    gv_view_area_set_mode(GV_VIEW_AREA(VecView), 0);
    gtk_drawing_area_size(GTK_DRAWING_AREA(VecView), 800, 600);

    swin = gtk_scrolled_window_new(NULL, NULL);

    gtk_container_add(GTK_CONTAINER(win), swin);
    gtk_container_add(GTK_CONTAINER(swin), VecView);


    //gv_view_area_add_layer(GV_VIEW_AREA(view), gv_shapes_layer_new(shapes));

    gtk_signal_connect_object(GTK_OBJECT(VecView), "key-press-event",
                              GTK_SIGNAL_FUNC(_key_press_cb),
                              GTK_OBJECT(VecView));

    gtk_widget_show(VecView);
    gtk_widget_show(swin);
    gtk_widget_show(win);
    gtk_widget_grab_focus(VecView);

    gtk_signal_connect(GTK_OBJECT(win), "delete-event", GTK_SIGNAL_FUNC(gtk_main_quit), NULL);

    gtk_quit_add_destroy(1, GTK_OBJECT(win));

    gv_tool_activate(GV_TOOL(toolbox), GV_VIEW_AREA(VecView));
    gv_toolbox_activate_tool(toolbox, "zoompan" );


    twin = _create_toolbar();

    _loadCell(filename);

    gtk_main();

    gdk_threads_leave();

    // FIXME: do we ever get here!
    // put back env. var. as it was
    /*
    if (NULL != genv){
        setenv("OGR_S57_OPTIONS", genv->str, 1);
        g_string_free(genv, TRUE);
    }
    */
    printf("exit main()\n");


    return 0;
}
