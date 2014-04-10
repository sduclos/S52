// S52MP.c: Mariner Parameter
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2014 Sylvain Duclos sduclos@users.sourceforge.net

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



#include "S52MP.h"      // S52_MP_get/set()

#include <glib.h>       // TRUE, FALSE

///////////////////////////////////////////////////////////////////
//
//   GLOBAL MARINER PARAMETER (will move out)
//
// NOTE: value for Chart No 1 found in README
//
// Soundings      ON
// Text           ON
// Depth Shades    4
// Safety Contour 10 m
// Safety Depth    7 m
// Shallow         5 m
// Deep           30 m

/* A) value for Chart No 1 */
/*
static double SHOW_TEXT       = TRUE;    // view group 23
static double TWO_SHADES      = FALSE;   // flag indicating selection of two depth shades (on/off) [default ON]
static double SAFETY_CONTOUR  = 10.0;    // selected safety contour (meters) [IMO PS 3.6]
static double SAFETY_DEPTH    =  7.0;    // selected safety depth (meters) [IMO PS 3.7]
static double SHALLOW_CONTOUR =  5.0;    // selected shallow water contour (meters) (optional)
static double DEEP_CONTOUR    = 30.0;    // selected deepwatercontour (meters) (optional)
*/

/* B) value for testing */
/*
//gboolean TWO_SHADES      = TRUE;     // flag indicating selection of two depth shades (on/off) [default ON]
gboolean TWO_SHADES      = FALSE;    // flag indicating selection of two depth shades (on/off) [default ON]
gboolean SHOW_TEXT       = TRUE;     // view group 23
//double    SAFETY_DEPTH    = 30.0;    // selected safety depth (meters) [IMO PS 3.7]
//double    SHALLOW_CONTOUR = 2.0;     // selected shallow water contour (meters) (optional)
double    SAFETY_DEPTH    = 15.0;    // selected safety depth (meters) [IMO PS 3.7]
double    SHALLOW_CONTOUR = 5.0;     // selected shallow water contour (meters) (optional)
//double    SAFETY_CONTOUR  = 30.0;    // selected safety contour (meters) [IMO PS 3.6]
//double    DEEP_CONTOUR    = 30.0;    // selected deepwatercontour (meters) (optional)
//double    SAFETY_CONTOUR  = 5.0;     // selected safety contour (meters) [IMO PS 3.6]
//double    DEEP_CONTOUR    = 10.0;    // selected deepwatercontour (meters) (optional)
double    SAFETY_CONTOUR  = 10.0;    // selected safety contour (meters) [IMO PS 3.6]
double    DEEP_CONTOUR    = 15.0;    // selected deepwatercontour (meters) (optional)
*/

/* param needed for certain conditional symbology */
/*
gboolean SHALLOW_PATTERN = FALSE;    // flag indicating selection of shallow water highlight (on/off)(optional) [default OFF]
gboolean SHIPS_OUTLINE   = FALSE;    // flag indicating selection of ship scale symbol (on/off) [IMO PS 8.4]
double   DISTANCE_TAGS   = 0.0;      // selected spacing of "distance to run" tags at a route (nm)
double   TIME_TAGS       = 0.0;      // selected spacing of time tags at the pasttrack (min)
gboolean FULL_SECTORS    = TRUE;     // show full length light sector lines
gboolean SYMBOLIZED_BND  = TRUE;     // symbolized area boundaries
*/


// textual name of mariner's parameter
// WARNING: must be in sync with S52MarinerParameter
// WARNING: must be in sync with S52_MARparamName
static double _MARparamVal[] = {
    0.0,      // 0 - NONE

    //0.0,     // 1 - SHOW_TEXT  (0 - off)
    1.0,     // 1 - SHOW_TEXT (default)

    FALSE,    // 2 - TWO_SHADES (flase -> 4 shade)
    //TRUE,    // 2 - TWO_SHADES (SEABED01 default)

    15.0,     // 3 - SAFETY_CONTOUR
    //0.0,     // 3 - SAFETY_CONTOUR  --to test DEPCNT02 selection (GL) in CA49995A.000
    //0.5,     // 3 - SAFETY_CONTOUR  --to test DEPCNT02 selection (GL) in CA49995A.000
    //30.0,      // 3 - SAFETY_CONTOUR (SEABED01 default)

    7.0,      // 4 - SAFETY_DEPTH
    //5.0,      // 4 - SAFETY_DEPTH

    2.0,      // 5 - SHALLOW_CONTOUR (SEABED01 default)
    //5.0,      // 5 - SHALLOW_CONTOUR

    //30.0,     // 6 - DEEP_CONTOUR (SEABED01 defautl)
    15.0,     // 6 - DEEP_CONTOUR

    FALSE,    // 7 - SHALLOW_PATTERN (SEABED01 defautl)
    //TRUE,    // 7 - SHALLOW_PATTERN

    FALSE,    // 8 - SHIPS_OUTLINE

    0.0,      // 9 - S52_DISTANCE_TAGS (default OFF)

    //0.0,      // 10 - TIME_TAGS
    1.0,      // 10 - TIME_TAGS  (debug)

    TRUE,     // 11 - FULL_SECTORS

    TRUE,     // 12 - SYMBOLIZED_BND
    //FALSE,     // 12 - SYMBOLIZED_BND

    TRUE,     // 13 - SYMPLIFIED_PNT
    //FALSE,     // 13 - SYMPLIFIED_PNT

                 // 14 - S52_MAR_DISP_CATEGORY
//    S52_MAR_DISP_CATEGORY_BASE,     // 0,      0x000000  DISPLAY BASE
    S52_MAR_DISP_CATEGORY_STD,        // 1 << 0  0x000001  STANDARD (default)
//    S52_MAR_DISP_CATEGORY_OTHER,    // 1 << 1  0x000010  OTHER
//    S52_MAR_DISP_CATEGORY_SELECT,   // 1 << 2  0x000100  SELECT   (override)

      0,        // 15 - S52_MAR_COLOR_PALETTE --DAY_BRIGHT
//    1,        // 15 - S52_MAR_COLOR_PALETTE --DAY_BLACKBACK
//    2,        // 15 - S52_MAR_COLOR_PALETTE --DAY_WHITEBACK
//    3,        // 15 - S52_MAR_COLOR_PALETTE --DUSK
//    4,        // 15 - S52_MAR_COLOR_PALETTE --NIGHT

   12.0,      // 16 - S52_MAR_VECPER (min)
    1.0,      // 17 - S52_MAR_VECMRK 0/1/2 (0 - for none, 1 - 1&6 min, 2 - 6 min)
    1.0,      // 18 - S52_MAR_VECSTB 0/1/2 (0 - for none, 1 - ground, 2 - water)

    0.0,      // 19 - S52_MAR_HEADNG_LINE (default OFF)
    //1.0,      // 19 - S52_MAR_HEADNG_LINE (debug)
    0.0,      // 20 - S52_MAR_BEAM_BRG_NM - beam bearing NM (default OFF)
    //1.0,      // 20 - S52_MAR_BEAM_BRG_NM - beam bearing NM (debug)


    //---- experimantal ----
    0.0,      // 21 - S52_MAR_FONT_SOUNDG   --use font for souding (on/off)
    0.0,      // 22 - S52_MAR_DATUM_OFFSET  --value of chart datum offset (raster_sound must be ON)

    1.0,      // 23 - S52_MAR_SCAMIN        --flag for using SCAMIN filter (on/off)  (default ON)
    //0.0,      // 23 - S52_MAR_SCAMIN        --flag for using SCAMIN filter (on/off)

    0.0,      // 24 - S52_MAR_ANTIALIAS  (on/off)

    1.0,      // 25 - display QUAPNT01 (quality of position symbol) (on/off)

    0.0,      // 26 - display overlapping symbol (default to false, debug)

    //1.0,      // 27 - S52_MAR_DISP_LAYER_LAST to enable S52_drawLast()
    //S52_MAR_DISP_LAYER_LAST_NONE,   // 1 << 3  0x0001000 - MARINERS' NONE
    S52_MAR_DISP_LAYER_LAST_STD,      // 1 << 4  0x0010000 - MARINERS' STANDARD (default)
    //S52_MAR_DISP_LAYER_LAST_OTHER,  // 1 << 5  0x0100000 - MARINERS' OTHER
    //S52_MAR_DISP_LAYER_LAST_SELECT, // 1 << 6  0x1000000 - MARINERS' SELECT (override)

    0.0,      // 28 - S52_MAR_ROT_BUOY_LIGHT (deg)

    0.0,      // 29 - S52_MAR_DISP_CRSR_POS, display cursor position (default off)

    0.0,      // 30 - S52_MAR_DISP_GRATICULE  (default off)

    0.0,      // 31 - S52_MAR_DISP_WHOLIN (default off)

    0.0,      // 32 - S52_MAR_DISP_LEGEND (default off)

    0.0,      // 33 - S52_CMD_WRD_FILTER command word filter for profiling (default off)

    0.3,      // 34 - S52_MAR_DOTPITCH_MM_X dotpitch X (mm) - pixel size in X
    0.3,      // 35 - S52_MAR_DOTPITCH_MM_Y dotpitch Y (mm) - pixel size in Y

    0.0,      // 36 - S52_MAR_DISP_CALIB - display calibration symbol (on/off) (default off)

    1.0,      // 37 - S52_MAR_DISP_DRGARE_PATTERN - display DRGARE pattern (default on)

    1.0,      // 38 - S52_MAR_DISP_NODATA_LAYER -  display layer 0 (no data) (default on)

    600.0,    // 39 - S52_MAR_DEL_VESSEL_DELAY (sec)

    1.0,      // 40 - S52_MAR_DISP_AFTERGLOW (default on)

    //0.0,      // 41 - S52_MAR_DISP_CENTROIDS display all centered symb of one area (on/off) (default off)
    1.0,      // 41 - S52_MAR_DISP_CENTROIDS display all centered symb of one area (on/off) (default off)

    0.0,      // 42 - S52_MAR_DISP_WORLD display World - TM_WORLD_BORDERS_SIMPL-0.2.shp - (on/off) (default off)
    //1.0,      // 42 - S52_MAR_DISP_WORLD display World - TM_WORLD_BORDERS_SIMPL-0.2.shp - (on/off) (default off)

    1.0,      // 43 - S52_MAR_DISP_RND_LN_END - display rounded line ending (on/off)

    1.0,      // 44 - S52_MAR_DISP_VRMEBL_LABEL - display bearing / range label on VRMEBL (on/off)

    0.0,      // 45 - S52_MAR_DISP_RASTER - display Raster:RADAR,Bathy,... (on/off) (default off)

    46.0      // number of parameter type
};

double S52_MP_get(S52MarinerParameter param)
// return Mariner parameter or S52_MAR_NONE if fail
// FIXME: check mariner param against groups selection
{
    if (S52_MAR_NONE<param && param<S52_MAR_NUM)
        return _MARparamVal[param];

    return _MARparamVal[S52_MAR_NONE];
}

int    S52_MP_set(S52MarinerParameter param, double val)
{
    _MARparamVal[param] = val;

    return TRUE;
}

/*
int    S52_MP_write(const char *filename)
// write back param to file
{
    const char *fn = NULL;

    PRINTF("FIXME: not implemented\n");

    if (NULL == filename)
        fn = CONF_NAME;
    else
        fn = filename;

    // TODO: write the stuff
    // ...

    return FALSE;
}
*/




// ------ Text Display Priority --------------------------------------------------
//
//

#define TEXT_IDX_MAX 100
static unsigned int _textDisp[TEXT_IDX_MAX]; // assume compiler init to 0 (C99!)

int    S52_MP_setTextDisp(unsigned int prioIdx, unsigned int count, unsigned int state)
{
    if (prioIdx+count > TEXT_IDX_MAX)
        return FALSE;

    for (guint i=0; i<count; ++i)
        _textDisp[prioIdx + i] = state;

    return TRUE;
}

int    S52_MP_getTextDisp(unsigned int prioIdx)
{
    if (prioIdx < TEXT_IDX_MAX)
        return _textDisp[prioIdx];
    else
        return FALSE;
}
