// gvS57layer.c: S57 layer classe for OpenEV
//
// Project:  OpENCview/OpenEV

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2017 Sylvain Duclos sduclos@users.sourceforge.net

    OpENCview is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpENCview is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public License
    along with OpENCview.  If not, see <http://www.gnu.org/licenses/>.
*/


// Note: openev-2 only because openev-1 depend on GLib-1
//       and libS52 use GLib-2 only now (2016)

#include "gvS57layer.h"

int S52_init();
int S52_drawLayer();
int S52_loadLayer(const char *layername, void *layer);
int S52_pickAt(double x, double y);
int S52_done();

#include "S52utils.h"       // PRINTF()

#include <glib.h>           // g_signal_connect()
#include <glib-object.h>    // G_TYPE_CHECK_INSTANCE()
#include <gmodule.h>        // g_module_init(), g_module_check_init(), g_module_unload()


static GModule *self = NULL;  // handel to libS52.so (this)

static void   _motion_handle_hint(GtkWidget *view, GdkEventMotion *event)
{
    if (event->type==GDK_BUTTON_RELEASE && event->state & GDK_SHIFT_MASK) {
        S52_pickAt(event->x, event->y);
    }
}

static void gv_S57_draw_layer(GvLayer *layer, GvViewArea *view)
{
    //GObject *layer = view->layers;
    //char *name = (char *)gv_data_get_name(GV_DATA(layer));
    char *name = (char *)gv_data_get_name(GV_DATA(layer));

    S52_drawLayer(name);
    //S52_drawLayer("DEPARE");
}

static void gv_S57_layer_setup(GvShapesLayer *layer, GvViewArea *view)
// load S57 --its in "wkt" OGC layer format
{
    //printf("start S52 gv_S57_layer_setup\n");

    if (NULL==layer || NULL==view) {
        PRINTF("ERROR: no GvShapesLayer or GvViewArea\n");
        return;
    }

    g_signal_connect(layer, "draw", gv_S57_draw_layer, view);
    g_signal_connect(view, "button-release-event", _motion_handle_hint, view);

    //printf("S52 name gv_S57_layer_setup\n");

    //char *name = (char *)gv_data_get_name(GV_DATA(layer));
    char *name = (char *)gv_data_get_name(layer);
    S52_loadLayer(name, (void *)layer);


    //printf("S52 fini gv_S57_layer_setup\n");

    return;
}

void          _layer_init(GvShapesLayer *layer)
{

    // debug
    //printf("from S52 _layer_init\n", sizeof(GvShapesLayer));
    //printf("from S52 _layer_init\n");
    //printf("from S52 sizeof(GvShapesLayer)%i\n", sizeof(GvShapesLayer));
    //printf("from S52 sizeof(GArray)       %i\n", sizeof(GArray));


    g_signal_connect((gpointer)layer, "setup", gv_S57_layer_setup, (gpointer)layer);

    //printf("fini _layer_init\n");

    return;
}

const gchar * _ogr_driver_name()
{
    // FIXME: get this programaticaly
    //OGRSFDriverH  hDriver =  ... load_S57_driver_ ...
    //return OGR_Dr_GetName(hDriver);

    // pulled from: gdal/ogr/ogrsf_frmts/s57/ogrs57driver.cpp:69
    // in  'const char *OGRS57Driver::GetName()'
    return "S57";
}

const gchar *g_module_check_init(GModule *module)
{
    PRINTF("loading libS52.so ...\n");

    if (NULL == self) {
        self = module;
        //PRINTF("self name = %s\n", g_basename(g_module_name(self)));
        // will print "self name = libS52.so"
    }

    S52_init();

    return NULL;
}

void         g_module_unload()
{
    PRINTF("unloading libS52.so ...\n");

    S52_done();

    return;
}
