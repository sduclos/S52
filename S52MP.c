// S52MP.c: Mariner Parameter
//
// Project:  OpENCview

/*
    This file is part of the OpENCview project, a viewer of ENC.
    Copyright (C) 2000-2016 Sylvain Duclos sduclos@users.sourceforge.net

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
#include "S52utils.h"   // PRINTF()

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


// FIXME: add textual name of mariner's parameter
// FIXME: use the X macro to sync: http://www.drdobbs.com/cpp/the-x-macro/228700289
/*
#define COLORS        \
    X(Cred,   "red")  \
    X(Cblue,  "blue") \
    X(Cgreen, "green")

// Put this in color.h. Then, in the place where the enum is declared:

#define X(a, b) a,
enum Color { COLORS };
#undef X

// You can see where this is going. In the source file color.c:

#define X(a, b) b,
static char *ColorStrings[] = { COLORS };
#undef X
*/

// WARNING: must be in sync with S52MarinerParameter (see X macro above)
static double _MARparamVal[] = {
    0.0,      // 0 - ERROR: 0 - no error,

    //0.0,     // 1 - SHOW_TEXT  (0 - off)
    1.0,     // 1 - SHOW_TEXT (default)

    FALSE,    // 2 - TWO_SHADES (false -> 4 shade)
    //TRUE,    // 2 - TWO_SHADES (SEABED01 default)

    15.0,     // 3 - SAFETY_CONTOUR
    //0.0,     // 3 - SAFETY_CONTOUR  --to test DEPCNT02 selection (GL) in CA49995A.000
    //0.5,     // 3 - SAFETY_CONTOUR  --to test DEPCNT02 selection (GL) in CA49995A.000
    //30.0,      // 3 - SAFETY_CONTOUR (SEABED01 default)

    7.0,      // 4 - SAFETY_DEPTH
    //5.0,      // 4 - SAFETY_DEPTH

    2.0,      // 5 - SHALLOW_CONTOUR (SEABED01 default)
    //5.0,      // 5 - SHALLOW_CONTOUR

    //30.0,     // 6 - DEEP_CONTOUR (SEABED01 default)
    15.0,     // 6 - DEEP_CONTOUR

    FALSE,    // 7 - SHALLOW_PATTERN (SEABED01 default)
    //TRUE,    // 7 - SHALLOW_PATTERN

    FALSE,    // 8 - SHIPS_OUTLINE

    0.0,      // 9 - S52_DISTANCE_TAGS (default OFF)

    0.0,      // 10 - TIME_TAGS (default OFF)

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
    0.0,      // 21 - S52_MAR_FONT_SOUNDG   --NOT IMPLEMENTED: use font for souding (on/off)
    0.0,      // 22 - S52_MAR_DATUM_OFFSET  --value of chart datum offset (raster_sound must be ON)

    1.0,      // 23 - S52_MAR_SCAMIN        --flag for using SCAMIN filter (on/off)  (default ON)
    //0.0,      // 23 - S52_MAR_SCAMIN        --flag for using SCAMIN filter (on/off)

    0.0,      // 24 - S52_MAR_ANTIALIAS  (on/off)

    1.0,      // 25 - display QUAPNT01 (quality of position symbol) (on/off)

    0.0,      // 26 - display overlapping symbol (default to false, debug)

              // 27 - S52_MAR_DISP_LAYER_LAST to enable S52_drawLast()
    //S52_MAR_DISP_LAYER_LAST_NONE,   // 1 << 3  0x0001000 - MARINERS' NONE
    S52_MAR_DISP_LAYER_LAST_STD,      // 1 << 4  0x0010000 - MARINERS' STANDARD (default)
    //S52_MAR_DISP_LAYER_LAST_OTHER,  // 1 << 5  0x0100000 - MARINERS' OTHER
    //S52_MAR_DISP_LAYER_LAST_SELECT, // 1 << 6  0x1000000 - MARINERS' SELECT (override)

    0.0,      // 28 - S52_MAR_ROT_BUOY_LIGHT (deg)

    1.0,      // 29 - S52_MAR_DISP_CRSR_PICK, 0 - off, 1 - pick/highlight top object, 2 - pick stack/highlight top,
              //                              3 - pick stack+ASSOC/highlight ASSOC (compiled with -DS52_USE_C_AGGR_C_ASSO)

    0.0,      // 30 - S52_MAR_DISP_GRATICULE  (default off)

    0.0,      // 31 - S52_MAR_DISP_WHOLIN (default off)

    0.0,      // 32 - S52_MAR_DISP_LEGEND (default off)

    0.0,      // 33 - S52_CMD_WRD_FILTER command word filter for profiling (default off)

    0.3,      // 34 - S52_MAR_DOTPITCH_MM_X dotpitch X (mm) - pixel size in X
    0.3,      // 35 - S52_MAR_DOTPITCH_MM_Y dotpitch Y (mm) - pixel size in Y

    0.0,      // 36 - S52_MAR_DISP_CALIB - display calibration symbol (on/off) (default off)

    1.0,      // 37 - S52_MAR_DISP_DRGARE_PATTERN - display DRGARE pattern (default on)

    1.0,      // 38 - S52_MAR_DISP_NODATA_LAYER -  display layer 0 (no data) (default on)

    //600.0,    // 39 - S52_MAR_DISP_VESSEL_DELAY (sec)
    0.0,      // 39 - S52_MAR_DISP_VESSEL_DELAY (sec) (default OFF)

    0.0,      // 40 - S52_MAR_DISP_AFTERGLOW (default off)
    //1.0,      // 40 - S52_MAR_DISP_AFTERGLOW (on)

    //0.0,      // 41 - S52_MAR_DISP_CENTROIDS display all centered symb of one area (on/off) (default off)
    1.0,      // 41 - S52_MAR_DISP_CENTROIDS display all centered symb of one area (on/off) (default off)

    0.0,      // 42 - S52_MAR_DISP_WORLD display World - TM_WORLD_BORDERS_SIMPL-0.2.shp - (on/off) (default off)
    //1.0,      // 42 - S52_MAR_DISP_WORLD display World - TM_WORLD_BORDERS_SIMPL-0.2.shp - (on/off) (default off)

    1.0,      // 43 - S52_MAR_DISP_RND_LN_END - display rounded line ending (on/off)

    1.0,      // 44 - S52_MAR_DISP_VRMEBL_LABEL - display bearing / range label on VRMEBL (on/off)

    0.0,      // 45 - S52_MAR_DISP_RADAR_LAYER - display Raster: RADAR, Bathy, ... (on/off) (default off)

    1852.0,   // 46 - S52_MAR_GUARDZONE_BEAM - Danger/Indication Highlight used by LEGLIN & Position (meters)
    1852.0*6, // 47 - S52_MAR_GUARDZONE_LENGTH - Danger/Indication Highlight used by Position
              //      (meters, user computed from speed/time or distance) [default 6 NM, 30 min. @ 12kt]
    0.0,      // 48 - S52_MAR_GUARDZONE_ALARM  // FIXME: put MAR_ERROR code here
              //      FIXME: 1&2 ON at the same time. 0 - no error, 1 - alarm, 2 - indication

    0.0,      // 49 - S52_MAR_DISP_HODATA, 0 - union HO data limit "m_covr" (default), 1 - all HO data limit (M_COVR+m_covr)

    50.0      // number of parameter type
};

double S52_MP_get(S52MarinerParameter param)
// return Mariner parameter or S52_MAR_ERROR if fail
// FIXME: check mariner param against groups selection
{
    //if (param<S52_MAR_ERROR || S52_MAR_NUM<=param) {
    if (S52_MAR_ERROR<=param && param<S52_MAR_NUM) {
        return _MARparamVal[param];
    } else {
        PRINTF("WARNING: param invalid(%f)\n", param);
        g_assert(0);

        return _MARparamVal[S52_MAR_ERROR];
    }

}

int    S52_MP_set(S52MarinerParameter param, double val)
{
    //if (param<S52_MAR_ERROR || S52_MAR_NUM<=param) {
    if (S52_MAR_ERROR<=param && param<S52_MAR_NUM) {
        _MARparamVal[param] = val;

        return TRUE;
    } else {
        PRINTF("WARNING: param invalid(%f)\n", param);
        g_assert(0);

        return FALSE;
    }
}


// ------ Text Display Priority --------------------------------------------------
//
//

#define TEXT_IDX_MAX 100
//static unsigned int _textDisp[TEXT_IDX_MAX] = {[0 ... 99] = 1}; // not ISO C (gcc specific)
static unsigned int _textDisp[TEXT_IDX_MAX] = {
    1,1,1,1,1,1,1,1,1,1,   // 00 - 09 reserved for future assignment by IHO
    1,1,1,1,1,1,1,1,1,1,   // 10 - 19 Important Text
    1,1,1,1,1,1,1,1,1,1,   // 20 - 29 Other text
    1,1,1,1,1,1,1,1,1,1,   // 30 - 39 30 - na, 31 - national language text (NOBJNM, NINFOM, NTXTDS)
    1,1,1,1,1,1,1,1,1,1,   // 40 - 49 32 - 49 reserved for IHO
    1,1,1,1,1,1,1,1,1,1,   // 50 - 59 mariners text, including planned speed etc.
    1,1,1,1,1,1,1,1,1,1,   // 60 - 69 manufacturer's text
    1,1,1,1,1,1,1,1,1,1,   // 70 - 79 future requirements (AIS etc.)
    1,1,1,1,1,1,1,1,1,1,   // 80 - 89 future requirements (AIS etc.)
    1,1,1,1,1,1,1,1,1,1    // 90 - 99 future requirements (AIS etc.)
};

int    S52_MP_setTextDisp(unsigned int prioIdx, unsigned int count, unsigned int state)
{
    if (TEXT_IDX_MAX <= prioIdx) {
        PRINTF("WARNING: prioIdx out of bound (%i)\n", prioIdx);
        return FALSE;
    }

    if (TEXT_IDX_MAX < count) {
        PRINTF("WARNING: count out of bound (%i)\n", count);
        return FALSE;
    }

    if (TEXT_IDX_MAX < prioIdx+count) {
        PRINTF("WARNING: prioIdx + count out of bound (%i)\n", prioIdx + count);
        return FALSE;
    }

    for (guint i=0; i<count; ++i)
        _textDisp[prioIdx + i] = state;

    return TRUE;
}

int    S52_MP_getTextDisp(unsigned int prioIdx)
{
    if (prioIdx < TEXT_IDX_MAX)
        return _textDisp[prioIdx];
    else
        return -1;
}
