// S52PL.h: interface to S52 Presentation Library Parser
//
// Project:  OpENCview

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



#ifndef _S52PL_H_
#define _S52PL_H_

#include "S52.h"         // S52ObjectType
#include "S57data.h"     // S57_geo, S57_prim

#include <glib.h>        // GArray

#define S52_PL_SMB_NMLN   8    // symbology name lenght
#define S52_PL_COL_NMLN   5    // color name lenght

// S52 symbology table name
typedef enum S52_SMBtblName {
    S52_SMB_LINE = 0,   // Complex Linestyle --line
    S52_SMB_PATT,       // Pattern           --area
    S52_SMB_SYMB,       // Symbol            --point
    S52_SMB_COND,       // Conditional       --point,line,area
    S52_SMB_NUM         // number of tables
} S52_SMBtblName;

// S52 Display Priority
typedef enum S52_disPrio {
    S52_PRIO_NODATA = 0, //'0',    // no data fill area pattern
    S52_PRIO_GROUP1 = 1, //'1',    // S57 group 1 filled areas
    S52_PRIO_AREA_1 = 2, //'2',    // superimposed areas
    S52_PRIO_AREA_2 = 3, //'3',    // superimposed areas also water features
    S52_PRIO_SYM_PT = 4, //'4',    // point symbol also land features
    S52_PRIO_SYM_LN = 5, //'5',    // line symbol also restricted areas
    S52_PRIO_SYM_AR = 6, //'6',    // area symbol also traffic areas
    S52_PRIO_ROUTES = 7, //'7',    // routeing lines
    S52_PRIO_HAZRDS = 8, //'8',    // hazards
    S52_PRIO_MARINR = 9, //'9',    // VRM & EBL, own ship
    S52_PRIO_NUM    = 10,          // number of priority levels
    S52_PRIO_NOPRIO = '-'          // use default prio (CS)
} S52_disPrio;

// RADAR Priority
typedef enum S52_RadPrio {
    S52_RAD_OVER = 'O',      // presentation on top of RADAR
    S52_RAD_SUPP = 'S',      // presentation suppressed by RADAR
    S52_RAD_NUM  =  2,
    S52_RAD_NPR  = '-'       // No PRiority --use default prio (CS)
} S52_RadPrio;

// display categorie type (first letter from DISC field -- index 9)
typedef enum S52_DisCat {
    DISPLAYBASE        = 'D',    // 68
    STANDARD           = 'S',    // 83
    OTHER              = 'O',    // 79
    SELECT             = 'A',    // 65 - user selection (All)

    // index 18 (9 + 9) when index 9 is 'M' take 9th caracter further down
    MARINERS_STANDARD  = 'T',    // 'S' + 1 (dec 84)
    MARINERS_OTHER     = 'P',    // 'O' + 1 (dec 80)

    NO_DISP_CAT        = '-',    // use default categorie (CS)

    DISP_CAT_NUM       = 6       // total number of categorie
} S52_DisCat;

// Command Word
typedef enum S52_CmdWrd {
    S52_CMD_NONE,       // no rule type (init)
    S52_CMD_TXT_TX,     // TX --SHOWTEXT (formated)
    S52_CMD_TXT_TE,     // TE --SHOWTEXT
    S52_CMD_SYM_PT,     // SY --SHOWPOINT
    S52_CMD_SIM_LN,     // LS --SHOWLINE
    S52_CMD_COM_LN,     // LC --SHOWLINE COMPLEX
    S52_CMD_ARE_CO,     // AC --SHOWAREA
    S52_CMD_ARE_PA,     // AP --SHOWAREA PATTERN
    S52_CMD_CND_SY,     // CS --CALLSYMPROC (Conditional Symbology)

    S52_CMD_OVR_PR      // OVERRIDE PRIORITY (not in S52specs).
                        // Used 8 char to passe data from S52CS ('-' = field not used):
                        // 0   - S52_disPrio
                        // 1   - S52_RadPrio
                        // 2   - S52_DisCat,
                        // 3-7 - viewing group,
} S52_CmdWrd;

// fragment attributes - to fix S52_PL_getDListData() kludge
typedef struct S52_fragAtt {
    //--- not S52 field --------
    // FIXME: index of a GArray - pointer arithmetic - so guchar must be conveted gpointer!
    // #define g_ptr_array_index(array,index_) ((array)->pdata)[index_]
    // so i * sizeof(S52_Color)
    guchar   cidx;      // 2^6: index of this color in palette [0..63], used as lookup when palette change
    char     pen_w;     // 2^2: using VBO's we need to know this, Display List store this but not VBO
    char     trans;     // 2^2: command word can change this so 'trans' is linked to an object not a color
    //--------------------------
} S52_fragAtt;

// color definition
typedef struct S52_Color {
    S52_fragAtt fragAtt;  //--- not S52 field --------

    char     colName[S52_PL_COL_NMLN +1];   // '\0' terminated
    double   x;
    double   y;
    double   L;
    guchar   R;
    guchar   G;
    guchar   B;
} S52_Color;

// symbol's OpenGL Display List sub-list for color/pen_w/trans switch
#define MAX_SUBLIST 10  // ex: SCALEB10 need to switch color 9 times (2 colors)
//#define MAX_SUBLIST 11  // ex: SCALEB10 need to switch color 9 times (2 colors)
typedef struct S52_DListData {
    int       create;                // TRUE create new DL
    guint     nbr;                   // number of Display List / VBO
    guint     vboIds[MAX_SUBLIST];   // array of starting index of Display List / VBO ids
    S52_Color colors[MAX_SUBLIST];   // color of each Display List / VBO
    S57_prim *prim  [MAX_SUBLIST];   // hold PLib sym prim info for VBO

    int       crntPalIDX;            // -1 - init, 0..n palette index
} S52_DListData;

// Vector Command (a la HPGL)
typedef enum S52_vCmd {
    S52_VC_NONE = 0,    // initial / no (more) command
    S52_VC_NEW  = 'X',  // start new sub-list

    // second character
    S52_VC_ST = 'T',    // transparency
    S52_VC_SW = 'W',    // width
    S52_VC_PU = 'U',    // pen up
    S52_VC_PD = 'D',    // pen down
    S52_VC_CI = 'I',    // circle
    S52_VC_AA = 'A',    // arc
    S52_VC_SC = 'C',    // call symbol

    S52_VC_PM = 'M',    // polygone mode

    // first charater
    S52_VC__P = 'P',    // check first character
    S52_VC_SP = 'S',    // color (also symbolcall)
    S52_VC_EP = 'E',    // outline polygone
    S52_VC_FP = 'F'     // fill polygone
} S52_vCmd;

// display suppression flag
typedef enum S52_objSupp {
    S52_SUPP_OFF  = 0,    // initial object not suppressed
    S52_SUPP_ON   = 1,    // display of object is suppressed
    S52_SUPP_ERR  = 2     // object not found or not togglelable (displaybase)

} S52_objSupp;

typedef struct _S52_symDef S52_symDef;
typedef struct _S52_vec    S52_vec;
typedef struct _S52_obj    S52_obj;

// load/init presentation library
int            S52_PL_init(void);
// load supplemental PLib
int            S52_PL_load(const char *PLib);
// free presentation library
int            S52_PL_done(void);

// get RGB from color name, for the currently selected color table
S52_Color     *S52_PL_getColor(const char *colorName);

// get a rasterising rules for this S57 object
S52_obj       *S52_PL_newObj(S57_geo *geo);
// nilAuxInfo: TRUE del ref to obj, also in objlist
S57_geo       *S52_PL_delObj(S52_obj *obj, gboolean nilAuxInfo);
// get the geo part (S57) of this S52 object
#define        S52PLGETGEO(S52OBJ) (*(S57_geo **)S52OBJ)
#define        S52_PL_getGeo(obj) S52PLGETGEO(obj)
//S57_geo       *S52_PL_getGeo(obj);

// get LUP name
const char    *S52_PL_getOBCL(S52_obj *obj);
// get addressed object S52 obj TYPe
S52ObjectType  S52_PL_getFTYP(S52_obj *obj);

// -- obj priority -----------------------------
// get override prio state
int            S52_PL_isPrioO(S52_obj *obj);
// get Display PRIority
S52_disPrio    S52_PL_getDPRI(S52_obj *obj);
// get RADAR Priority
S52_RadPrio    S52_PL_getRPRI(S52_obj *obj);
// get DISplay Category
S52_DisCat     S52_PL_getDISC(S52_obj *obj);
// get LUCM (view group) - not used {here for compliness
int            S52_PL_getLUCM(S52_obj *obj);
// -- obj priority -----------------------------

// get LUP table name - not used anymore
//S52_LUPtnm     S52_PL_getTNAM(S52_obj *obj);
// return plain text info for this type (TNAM) of lookup
const char    *S52_PL_infoLUP(S52_obj *obj);
// debug
const char    *S52_PL_getCMDstr(S52_obj *obj);

// command word list handling
S52_CmdWrd     S52_PL_iniCmd(S52_obj *obj);
// get next command word in the list
S52_CmdWrd     S52_PL_getCmdNext(S52_obj *obj);

// compare name to parameter of current command word
int            S52_PL_cmpCmdParam   (S52_obj *obj, const char *name);  // 8 chars
int            S52_PL_cmpCmdParamLUP(S52_obj *obj, const char *name);  // 8 chars
// get str for the current command (PLib exposition field: LXPO/PXPO/SXPO)
const char    *S52_PL_getCmdText(S52_obj *obj);

S52_DListData *S52_PL_getDLData(S52_symDef *def);

//-------------------------------------------------------
// init vector commands parser
S52_vec    *S52_PL_initVOCmd(S52_symDef *def);
// free parser mem
int         S52_PL_doneVOCmd(S52_vec *vecObj);
// get (parse) next vector command, width in ASCII (1 pixel=0.32 mm)
// vector origine: pivot_x/y
S52_vCmd    S52_PL_getNextVOCmd(S52_vec *vecObj);
// get vector for this command - not used
//S57_prim   *S52_PL_getVOprim(S52_vec *vecObj);
// get disk radius
double      S52_PL_getVOradius(S52_vec *vecObj);
// get vextex array
GArray     *S52_PL_getVOdata(S52_vec *vecObj);
// get name --debug
const char *S52_PL_getVOname(S52_vec *vecObj);
//-------------------------------------------------------


// return symbol orientation [0..360[
int            S52_PL_setSYorient(S52_obj *obj, double orient);
double         S52_PL_getSYorient(S52_obj *obj);
int            S52_PL_getSYbbox  (S52_obj *obj, int *width, int *height);

// not used
//int            S52_PL_setSYspeed (S52_obj *obj, double  speed);
//int            S52_PL_getSYspeed (S52_obj *obj, double *speed);

// get Line Style data, width in ASCII (dotpitch 1 pixel=0.32 mm) for a S52/IHO display size viewed at 1 m.
// Note: that it is more 'natural visualy', for a screen of 300ppi viewed at 10 cm, to go to a smaller dotpitch
int            S52_PL_getLSdata(S52_obj *obj, char *pen_w, char *style, S52_Color **color);
// get Line Complex data
int            S52_PL_getLCdata(S52_obj *obj, double *symlen, char *pen_w);
// get Area Color data
S52_Color     *S52_PL_getACdata(S52_obj *obj);
// get Area Pattern data
int            S52_PL_getAPTileDim(S52_obj *obj, double *tw,  double *th,  double *dx);

#if defined(S52_USE_GL2) || defined(S52_USE_GLES2)
// get Area Pattern Position - not used
int            S52_PL_getAPTilePos(S52_obj *obj, double *bbox_x, double *bbox_y, double *pivot_x, double *pivot_y);
// store texture ID of patterns in GLES2
int            S52_PL_setAPtexID(S52_obj *obj, guint mask_texID);
guint          S52_PL_getAPtexID(S52_obj *obj);
#endif

// traverse a symbology table calling 'callback' for each entree
gint           S52_PL_traverse(S52_SMBtblName tableNm, GTraverseFunc callBack);

// return a list of Display List --one per color/pen_w/trans
S52_DListData *S52_PL_newDListData(S52_obj *obj);
S52_DListData *S52_PL_getDListData(S52_obj *obj);

// text parser
const char    *S52_PL_getEX(S52_obj *obj, S52_Color **col,
                               int *xoffs, int *yoffs, unsigned int *bsize, unsigned int *weight, int *dis);

// TRUE: flag to run the text parser again
int            S52_PL_resetParseText(S52_obj *obj);
// TRUE: flag that the text(s) (ie TXs and TEs) has been parsed
// for the current type of symbol (normal or alternate)
int            S52_PL_setTextParsed(S52_obj *obj);

// TRUE if this object has text else FALSE
int            S52_PL_hasText(S52_obj *obj);
// TRUE if this object has LC (Line Complex) else FALSE - not used
//int            S52_PL_hasLC(S52_obj *obj);
// return CS name if this object has CS (Conditional Symbology) else NULL - not used (eventual optimisation)
//const char    *S52_PL_hasCS(S52_obj *obj);

// toggle display suppression of this class of object
S52_objSupp    S52_PL_toggleObjClass(const char *className);
// get display state for this class of object
S52_objSupp    S52_PL_getObjClassState(const char *className);
// get display state
//S52_objSupp    S52_PL_getObjToggleState(S52_obj *obj);
S52_objSupp    S52_PL_getObjSuppState(S52_obj *obj);

// set/get Mariner obj suppression
gboolean       S52_PL_setSupp(S52_obj *obj, gboolean supp);
gboolean       S52_PL_getSupp(S52_obj *obj);

void           S52_PL_resolveSMB(S52_obj *obj, gpointer dummy);
// dx/dy between center and pivot
int            S52_PL_getPivotOffset(S52_obj *obj, double *offset_x, double *offset_y);

#ifdef S52_USE_SUPP_LINE_OVERLAP
// link chain-node
int            S52_PL_setNextCN(S52_obj *objA, S52_obj *objB);
#endif

int            S52_PL_setRGB(const char *colorName, unsigned char  R, unsigned char  G, unsigned char  B);
int            S52_PL_getRGB(const char *colorName, unsigned char *R, unsigned char *G, unsigned char *B);

guint          S52_PL_getPalTableSz(void);
const char    *S52_PL_getPalTableNm(unsigned int idx);

int            S52_PL_setNextLeg(S52_obj *obj, S52_obj *objNextLeg);
S52_obj       *S52_PL_getNextLeg(S52_obj *obj);
S52_obj       *S52_PL_getPrevLeg(S52_obj *obj);

// not used
//S52_obj       *S52_PL_getWholin (S52_obj *obj);
//S52_obj       *S52_PL_setWholin (S52_obj *obj);

int            S52_PL_setTimeNow(S52_obj *obj);
long           S52_PL_getTimeSec(S52_obj *obj);


#ifdef S52_USE_FREETYPE_GL
guint          S52_PL_getFreetypeGL_VBO(S52_obj *obj, guint *len, double *strWpx, double *strHpx, char *hjust, char *vjust);
int            S52_PL_setFreetypeGL_VBO(S52_obj *obj, guint vboID, guint len, double strWpx, double strHpx);
#endif

S52_obj       *S52_PL_isObjValid(unsigned int objH);

#endif // _S52PL_H_
