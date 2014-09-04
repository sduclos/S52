// S52PLib.c: S52 Presentation Library parser/manager
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



#include "S52PL.h"          // --
#include "S52CS.h"          // S52_CS_condTable[]
#include "S52MP.h"          // S52_MP_get/set()
#include "S52.h"            // S52MarinerParameter
#include "S52utils.h"       // PRINTF(), S52_atof()
#include "S57data.h"        // geocoord, S57_obj_t

#include <glib.h>

#ifdef S52_USE_GLIB2
//#include <glib/gstdio.h>   // file handling (PLib)
//#include <glib/gprintf.h>  //
#include <glib/gstdio.h>     // FILE
#else
#include <stdio.h>           // FILE, fopen(), ...
#include <stdlib.h>          // setenv(), putenv(), strtoll(),
#include <string.h>          // strstr()
#endif

#define S52_SMB_NMLN   8    // symbology name lenght


//-- PLIB ID MODULE STRUCTURE ---------------------------------------
typedef struct _LBID {
    int             RCID;
    gchar           EXPP;    // 'N' (new) or 'R' (revision)

    GString        *ID;
    // PTYP         string
    // ESID         string
    // EDTN         string
    // CODT         char[8]
    // COTI         char[6]
    // VRDT         char[8]
    // PROF         char[2]  // 'PN' (new) or 'PR' (revision)
    // OCDT         char[8]
    // COMT         string
    struct _LBID   *next;
} _LBID;

static _LBID *_plibID = NULL;


//-- COLOR MODULE STRUCTURE ---------------------------------------
typedef struct _colTable {
    GString *tableName;     // debug
    GArray  *colors;
} _colTable;

static GArray *_colTables = NULL;
static GTree  *_colref    = NULL;  // fast indexing of color array
//static GArray *_colIdxRef = NULL;  // first table loaded used as index ref
                                   // so that all color are in the same order
                                   // used sync color fetching in dis. list
typedef enum _colorTableStat {
    _COL_TBL_NOSTAT =  0 , 	// unknown color table status
    _COL_TBL_NIL    = 'N', 	// new edition
    _COL_TBL_ADD    = 'A', 	// insert
    _COL_TBL_MOD    = 'M', 	// replace
    _COL_TBL_DEL    = 'D',  // deletion
    _COL_TBL_NUM    =  4    // number of color table status
} _colorTableStat;

#define S52_COL_NMLN   5     // color name lenght
#define S52_COL_NUM   63     // number of color (#64 is transparent)
static const char *_colorName[] = {
    "NODTA", "CURSR", "CHBLK", "CHGRD", "CHGRF", "CHRED", "CHGRN", "CHYLW",
    "CHMGD", "CHMGF", "CHBRN", "CHWHT", "SCLBR", "CHCOR", "LITRD", "LITGN",
    "LITYW", "ISDNG", "DNGHL", "TRFCD", "TRFCF", "LANDA", "LANDF", "CSTLN",
    "SNDG1", "SNDG2", "DEPSC", "DEPCN", "DEPDW", "DEPMD", "DEPMS", "DEPVS",
    "DEPIT", "RADHI", "RADLO", "ARPAT", "NINFO", "RESBL", "ADINF", "RESGR",
    "SHIPS", "PSTRK", "SYTRK", "PLRTE", "APLRT", "UINFD", "UINFF", "UIBCK",
    "UIAFD", "UINFR", "UINFG", "UINFO", "UINFB", "UINFM", "UIBDR", "UIAFF",
    "OUTLW", "OUTLL", "RES01", "RES02", "RES03", "BKAJ1", "BKAJ2"
};

// FIXME: boken optimisation
//static int _crntPaletteID = -1; // start with out of bound
//static int _flush_Colors = FALSE; // TRUE if new colors replace old one

//-- SYMBOLISATION MODULE STRUCTURE -----------------------------
// position parameter:   LINE,    PATTERN, SYMBOL
typedef struct _Position {
    union    {int        dummy1,  PAMI,    dummy2; } minDist;
    union    {int        dummy1,  PAMA,    dummy2; } maxDist;
    union    {int        LICL,    PACL,    SYCL;   } pivot_x;
    union    {int        LIRW,    PARW,    SYRW;   } pivot_y;
    union    {int        LIHL,    PAHL,    SYHL;   } bbox_w;
    union    {int        LIVL,    PAVL,    SYVL;   } bbox_h;
    union    {int        LBXC,    PBXC,    SBXC;   } bbox_x; // UL crnr
    union    {int        LBXR,    PBXR,    SBXR;   } bbox_y; // UL crnr
} _Position;

typedef struct _Shape {
   // note: bitmap/vector mutually exclusive
    union { GString   *dummy,  *PBTM,   *SBTM;  } bitmap;  // unused
    union { GString   *LVCT,   *PVCT,   *SVCT;  } vector;  //
} Shape;

// symbology definition: LINE,    PATTERN, SYMBOL
typedef struct _S52_cmdDef {
    int      RCID;
    union    {char       LINM[S52_SMB_NMLN+1],  // symbology name
                         PANM[S52_SMB_NMLN+1],  // '\0' teminated
                         SYNM[S52_SMB_NMLN+1];
             } name;
    union    {char       dummy,   PADF,    SYDF;   } definition;
    union    {char       dummy1,  PATP,    dummy2; } fillType;
    union    {char       dummy1,  PASP,    dummy2; } spacing;
    union    {_Position  line,    patt,    symb;   } pos;
    union    {GString   *LXPO,   *PXPO,   *SXPO;   } exposition;
    union    {Shape      line,    patt,    symb;   } shape;
    union    {GString   *LCRF,   *PCRF,   *SCRF;   } colRef;

    // ---- not a S52 fields ------------------------------------
    S52_SMBtblName symType;     // debug
    S52_DListData  DListData;   // GL Display List / VBO

    S52_Color     *col;         // LC: color of last pen
    char           pen_w;       // LC: width in pixel of last pen in (ASCII)

#if (defined(S52_USE_GL2) || defined(S52_USE_GLES2))
    guint          mask_texID;  // texture ID of pattern after running VBO
    int            potW;        // tex widht
    int            potH;        // tex height
#endif

} _S52_cmdDef;

typedef struct _Text {
    GString   *frmtd;       // formated text string (could be NULL)
    char       hjust;
    char       vjust;
    char       space;
    char       style;       // CHARS
    char       weight;      // CHARS
    char       width;       // CHARS
    int        bsize;       // CHARS -body size
    int        xoffs;       // pica (1 = 0.351mm)
    int        yoffs;       // pica (1 = 0.351mm)
    S52_Color *col;         // colour
    int        dis;         // display (view group)
} _Text;

// this 'union' is to highlight that *cmdDef is a pointer to a
// 1) PLib symbole definition or 2) C function (CS) or 3) text struct
typedef union _cmdDef {
    _S52_cmdDef     *def;

    _Text           *text;      // after parsing this could de NULL

    S52_CS_condSymb *CS;

    // because there is no cmdDef for light sector
    // so put VBO here
    S52_DListData   *DList;     // for pattern in GLES2 this DL will create a texture

} _cmdDef;

// command word list
typedef struct _cmdWL {
    S52_CmdWrd        cmdWord;  // Command Word type
    char             *param;    // start of parameter for this command

    _cmdDef           cmd;      // command word definition or conditional symb func call

#ifdef S52_USE_FREETYPE_GL
    guint             vboID;    // ID if the OpenGL VBO text
    guint             len;      // VBO text length
#endif

    struct _cmdWL *next;
} _cmdWL;

// S52 lookup table name (fifth letter)
typedef enum _LUPtnm {
    _LUP_NONAM =  0 , // unknown LUP (META)
    _LUP_SIMPL = 'L', // points --SIMPLIFIED
    _LUP_PAPER = 'R', // points --PAPER_CHART
    _LUP_LINES = 'S', // lines  --LINES
    _LUP_PLAIN = 'N', // areas  --PLAIN_BOUNDARIES
    _LUP_SYMBO = 'O', // areas  --SYMBOLIZED_BOUNDARIES
    _LUP_NUM   =  5   // number of lookup name
} _LUPtnm;


//-- LOOKUP MODULE STRUCTURE ----------------------------------------
typedef struct _LUP {
    int          RCID;          // record identifier
    char         OBCL[S52_PL_NMLN+1]; // LUP name --'\0' terminated
    //S52_Obj_t    FTYP;          // 'A' Area, 'L' Line, 'P' Point
    S57_Obj_t    FTYP;          // 'A' Area, 'L' Line, 'P' Point
    S52_disPrio  DPRI;          // Display Priority
    S52_RadPrio  RPRI;          // 'O' or 'S', Radar Priority
       _LUPtnm   TNAM;          // FTYP:  areas, points, lines
    GString     *ATTC;          // Attribute Code/Value (repeat)
    GString     *INST;          // Instruction Field (rules)
    S52_DisCat   DISC;          // Display Categorie: B/S/O, Base, Standard, Other
    int          LUCM;          // Look-Up Comment (PLib3.x put 'groupes' here,
                                // hense 'int', but its a string in the specs)

    // ---- not a S52 fields ------------------------------------
    S52_objSup       sup;       // supress display of this object type
    struct _LUP *OBCLnext;  // next LUP with name OBCL
} _LUP;

typedef enum _poly_mode {
    PM_BEG = '0',           // begin a poly (enter poly mode)
    PM_SUB = '1',           // begin sub poly
    PM_END = '2'            // end a poly (leave poly mode)
} _poly_mode;

// hold state of vector command parser
typedef struct _S52_vec {
    char      *str;         // VCT field
    char      *colRef;      // CRF field
    S57_prim  *prim;        // x,y,z,x,y,z,... (DrawArray format)
    int        bbx;         // def->pos.line.bbox_x.LBXC;
    int        bby;         // def->pos.line.bbox_y.LBXR;
    int        pivot_x;     // def->pos.line.pivot_x.LICL;
    int        pivot_y;     // def->pos.line.pivot_y.LIRW;
    char       pen_w;       // pen width
    double     radius;      // disk radius
    char      *name;        // name of this symbology definition
    _poly_mode pm;          // polygon mode
} _S52_vec;

// NOTE: order important --index to '_table[]'
// agreegated tables name
typedef enum _table_t {
    //S52_PL_ID,            // S52 Library Identification Module DB
    //S52_PL_COL,           // Colour Table (6)

    // Look-Up Table for:
    LUP_PT_SIMPL,   // simplified point symbol
    LUP_PT_PAPER,   // paper chart point symbol
    LUP_LINE,       // line
    LUP_AREA_PLN,   // plain boundaries area
    LUP_AREA_SYM,   // symbolized bound. area

    // Symbolisation Table for:
    SMB_LINE,       // Complex Linestyle
    SMB_PATT,       // Pattern
    SMB_SYMB,       // Symbol
    SMB_COND,       // Conditional Symbology

    TBL_NUM         // number of Tables
} _table_t;

typedef struct _S52_obj {
    // 2 set of LUP: normal and alternate
    _LUP        *LUP;           // common data for the 2 set of LUP

    _cmdWL      *cmdLorig[2];   // instruction list command (parsed LUP.INST)

    GString     *CSinst[2];     // expanded (resolved) cond. symb. instruction list
    _cmdWL      *CScmdL[2];     // parsed cond. symb. instruction list command

    // final command list (array):
    // normal command word + those once CS has been resolve and parsed
    GArray      *cmdAfinal[2];  // command array: normal symbol and alternate
    GArray      *crntA;         // point to the current (active) command array (normal or alternate)
    guint        crntAidx;      // index in command array

    gint         textParsed[2]; // TRUE if parsed, need two flag because there is text for
                                // two type of point and area


    gdouble      orient;        // LIGHT angle (after parsing), heading of 'ownshp'
    gdouble      speed;         // 'ownshp' speed for drawing vertor lenght

    S57_geo     *geoData;       // S-57

    // override by CS
    int          prioOveride;   // CS overide display priority
    S52_disPrio  DPRI;          // Display Priority
    S52_RadPrio  RPRI;          // 'O' or 'S', Radar Priority
    S52_DisCat   DISC;          // Display Categorie: B/S/O, Base, Standard, Other
    int          LUCM;          // Look-Up Comment (PLib3.x put 'groupes' here,
                                // hense 'int', but its a string in the specs)

    struct _S52_obj *nextLeg;   // link to next leglin (need to draw arc)
    struct _S52_obj *prevLeg;   // link to previous leg so that we can clip the start of this leg
                                // of the amout of wholin dist of the previous leg

    struct _S52_obj *wholin;    // link to wholin obj

    GTimeVal         time;      // store time (use to find age of AIS)

} _S52_obj;

// Tables (LUP+symbology) --BBTree holder
static gboolean _initTbl        = TRUE;  // will load PLib
static GTree   *_table[TBL_NUM] = {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};

#define CR     '\r'      // carriage return
#define EOL    '\037'    // 31/037/0x1F/CTRL-_: ASCII Unit Separator
                         // used in S52 as an EOL (also ATTC field separator)
#define APOS   '\047'

#define DEFOBJ "######"  // default object (name)

// MAXINT-6 is how OGR tag an UNKNOWN value
// see gdal/ogr/ogrsf_frmts/s57/s57.h:126
// it is then turn into a string in gv_properties
//#define EMPTY_NUMBER_MARKER "2147483641"  /* MAXINT-6 */

// MAX_BUF == 1024 --for buffer overflow
//#define LNFMT  "%1024[^\n]"   // line format
#define LNFMT  "%[^\n]"   // line format

#define FIELD(str)    if(0==strncmp(#str, _pBuf, 4))

typedef unsigned char u8;

// used to parse the PLib
#define MAX_BUF    1024      // working buffer length
//#define MAXL       256       // max string 'str' length in buf
//static char buffer[MAX_BUF];
//static char *pBuf = (char *)&buffer;
static char _pBuf[MAX_BUF];

//typedef unsigned char *PL;
typedef struct _PL {
    //const u8 *data;   // current position in PL
    gchar *data;        // current position in PL
    //int       cnt;    // current offset of 'data'
    gsize       cnt;    // current offset of 'data'
    gsize       sz;     // total size of PL
} _PL;


#if 0
char S52AuxSymb[] =
0001    0
LUPT   34LU01056NIL$CSYMBP00009OSIMPLIFIED
ATTC   15$SCODESCALEB10
INST   13SY(SCALEB10)
DISC   12DISPLAYBASE
LUCM    611030
****    0
0001    501057
LUPT   34LU01057NIL$CSYMBP00009OSIMPLIFIED
ATTC   15$SCODESCALEB11
INST   13SY(SCALEB11)
DISC   12DISPLAYBASE
LUCM    611030
****    0
#endif

// 1 col: S-52 pslb03_2.pdf 13.4.3
// 2 col: S-57 name for 'nature of surface' attribute  --NATSUR (113)
// 3 col: S-57 attribute remark
static const char *natsur[] = {
    "",                    // 0 : no value in ENC (filler)

    "M ",    //"mud",      // 1 : mud IJ 2,20;
    "Cy ",   //"clay",     // 2 : clay IJ 3;
    "Si ",   //"silt",     // 3 : silt IJ 4;
    "S ",    //"sand",     // 4 : sand IC 6; IJ 1,20; 312.2;
    "St ",   //"stone",    // 5 : stone IC 7; IJ 5,20; 312.2; 425.5-6;
    "G ",    //"gravel",   // 6 : gravel IJ 6,20;
    "P ",    //"pebbles",  // 7 : pebbles IJ 7;
    "Cb ",   //"cobbles",  // 8 : cobbles IJ 8;
    "R ",    //"rock",     // 9 : rock IJ 9,21; 426.2
            "marsh ",      // 10 : marsh
    "R ",    //"lava",     // 11 : lava
            "snow ",       // 12 : snow
            "ice ",        // 13 : ice
    "Co ",   //"coral",    // 14 : coral IJ 10,22; 425.5; 426.3;
            "swamp ",      // 15 : swamp
            "bog/moor ",   // 16 : bog/moor
    "Sh ",   //"shells",   // 17 : shells IJ 11; 425.5-6;
    "R "     //"boulder"   // 18 : boulder
};
#define N_NATSUR   19     // number of natsur

// CMS: Color Management System --> lcms
#include "lcms.h"
static cmsHPROFILE   _xyzp = NULL;
static cmsHPROFILE   _rgbp = NULL;
static cmsHTRANSFORM _2RGB = NULL;
static int           _lcmsError = FALSE;   // TRUE an error occur in lcms




//------------------------
//
//  MODULES LINKING SECTION
//
//------------------------

#if 0
static int        _readS52Line(_PL *fp, char *buf)
// copy a line from fp into buf return number of
// char in buf or -1 on EOF. buf is null/null terminated.
{
   int len = 0;
   //int ret = 0;

   //buf[0]  = '\0';

   return_if_null(fp);
   return_if_null(buf);

   while ( (fp->cnt < fp->sz) && ('\n' != *fp->data) ) {
       *buf++ = *fp->data++;
       fp->cnt++;
       len++;
   }

   // EOF
   if (fp->cnt >= fp->sz)
       return -1;

   // skip EOL
   if ('\n' ==  *fp->data) {
       fp->data++;
       fp->cnt++;
       *buf = '\0';
   }

   // FIXME: remove this hack
   if ( len > 0 && *(buf-1) == EOL)
        *(buf-1) = '\0';   // chop trailing \037 --string is \0\0 terminated


   return len;
}
#endif

static int        _readS52Line(_PL *fp, char *buf)
// copy a line from fp into buf return number of
// char in buf or -1 on EOF. buf is null/null terminated.
{
   int linelen = 0;

   char *b = buf;

   return_if_null(fp);
   return_if_null(buf);

   while ( (fp->cnt < fp->sz) && ('\n' != *fp->data) && (linelen<MAX_BUF-1)) {
       *b++ = *fp->data++;
       fp->cnt++;
       ++linelen;
   }
   *b = '\0';

   if (fp->cnt < fp->sz) {
       fp->data++;
       fp->cnt++;
   }

   // skip comment - because # is used to indicate default rule
   if (';' == *buf)
       return linelen;

   // use the record lenght
   char dst[6];
   strncpy(dst, buf+4, 5);
   dst[5] = '\0';
   //int reclen = g_ascii_strtoll(buf+6, NULL, 10);
#ifdef S52_USE_GLIB2
   int reclen = g_ascii_strtoll(dst, NULL, 10);
#else
   int reclen = strtoll(dst, NULL, 10);
#endif
   for (int i=reclen+9; i<=linelen; ++i)
       buf[i] = '\0';

   if (EOL == buf[reclen+8])
       buf[reclen+8] = '\0';

   // EOF
   if (fp->cnt >= fp->sz)
       return -1;

   return linelen;
}

static int        _chopAtEOL(char *pBuffer, char c)
// replace S52 EOL (field separator) with char 'c'
{
   /*
   int i;

   for (i=0; pBuffer[i] != '\0'; ++i)
   if ( pBuffer[i] == EOL )
   pBuffer[i] = c;
   */

   while (*pBuffer != '\0') {
      if (*pBuffer == EOL )
          *pBuffer = c;
      ++pBuffer;
   }

   return TRUE;
}


static GTree     *_selLUP(_LUPtnm TNAM)
// select lookup table from its name
{
   switch (TNAM) {
       case _LUP_SIMPL: return _table[LUP_PT_SIMPL];
       case _LUP_PAPER: return _table[LUP_PT_PAPER];
       case _LUP_LINES: return _table[LUP_LINE];
       case _LUP_PLAIN: return _table[LUP_AREA_PLN];
       case _LUP_SYMBO: return _table[LUP_AREA_SYM];

       case _LUP_NONAM: return NULL;

       case _LUP_NUM:
       default:
           PRINTF("ERROR unknown lookup table (%i)\n", TNAM);
           g_assert(0);
   }

   return NULL;
}

static GTree     *_selSMB(S52_SMBtblName name)
// select symbology table
{
   switch (name) {
       case S52_SMB_LINE: return _table[SMB_LINE];
       case S52_SMB_PATT: return _table[SMB_PATT];
       case S52_SMB_SYMB: return _table[SMB_SYMB];
       case S52_SMB_COND: return _table[SMB_COND];
       default:
           PRINTF("ERROR unknown symbology table!!\n");
           g_assert(0);
   }

   return NULL;
}

static gint       _cmpCOL(gconstpointer nameA, gconstpointer nameB)
// compare color name
{
    //PRINTF("%s - %s\n",(char*)nameA,(char*)nameB);
    return strncmp((char*)nameA, (char*)nameB, S52_COL_NMLN);
}

#ifdef S52_USE_GLIB2
static gint       _cmpLUP(gconstpointer nameA, gconstpointer nameB, gpointer user_data)
#else
static gint       _cmpLUP(gconstpointer nameA, gconstpointer nameB)
#endif
// compare lookup name
{
    // 'user_data' useless warning
#ifdef S52_USE_GLIB2
    (void) user_data;
#endif

    //PRINTF("%s - %s\n",(char*)nameA,(char*)nameB);
    return strncmp((char*)nameA, (char*)nameB, S52_PL_NMLN);
}

#ifdef S52_USE_GLIB2
static gint       _cmpSMB(gconstpointer nameA, gconstpointer nameB, gpointer user_data)
#else
static gint       _cmpSMB(gconstpointer nameA, gconstpointer nameB)
#endif
// compare Symbology name
{
    // 'user_data' useless warning
#ifdef S52_USE_GLIB2
    (void) user_data;
#endif


    return strncmp((char*)nameA, (char*)nameB, S52_SMB_NMLN);
}

static gint       _cmpCOND(gconstpointer nameA, gconstpointer nameB)
// compare Cond Symbology name
{
   return strncmp((char*)nameA, (char*)nameB, S52_SMB_NMLN);
}


#ifdef S52_USE_GLIB2
static void       _delLUP(gpointer value)
#else
static gint       _delLUP(gpointer key, gpointer value, gpointer data )
#endif
// delete lookup
{
   _LUP *LUP = (_LUP*) value;

   //_doneCmdList(LUP->cmdList);

   while (NULL != LUP) {
       _LUP *crntLUP = LUP->OBCLnext;

       if (NULL != LUP->ATTC) g_string_free(LUP->ATTC, TRUE);
       if (NULL != LUP->INST) g_string_free(LUP->INST, TRUE);

       g_free(LUP);

       LUP = crntLUP;
   }

#ifndef S52_USE_GLIB2
   return TRUE;
#endif
}

#ifdef S52_USE_GLIB2
static void       _delSMB(gpointer value)
#else
static gint       _delSMB(gpointer key, gpointer value, gpointer data)
#endif
// delete symbol
{
   _S52_cmdDef *def = (_S52_cmdDef*) value;

   // debug
   //PRINTF("del %s\n", def->name.SYNM);

   g_string_free(def->exposition.LXPO,        TRUE);
   g_string_free(def->shape.line.vector.LVCT, TRUE);
   g_string_free(def->colRef.LCRF,            TRUE);

   g_free(def);

#ifndef S52_USE_GLIB2
   return TRUE;
#endif
}

static gint       _loadCondSymb()
// load Conditional Symbology in BBtree
{
    if (NULL == S52_CS_condTable)
        return FALSE;

    for (int i=0; NULL!=S52_CS_condTable[i].CScb; ++i) {
        g_tree_insert(_selSMB(S52_SMB_COND),
                      (gpointer)  S52_CS_condTable[i].name,
                      (gpointer) &S52_CS_condTable[i]);
    }

    return TRUE;
}

static int        _dumpATT(char *str)
// debug
{
    int   len = strlen(str);
    //g_print("LUP ATT:");
    printf("LUP ATT:");
    while (len != 0) {
        //g_print(" %s", str);
        printf(" %s", str);
        str += len+1;
        len = strlen(str);
    }
    //g_print("\n");
    printf("\n");

    return TRUE;
}

static _LUP      *_lookUpLUP(_LUP *LUPlist, S57_geo *geoData)
// Get the LUP with maximum Object attribute match.
//
// Note: reference are maide to section "8.3 How to use the look-up table"
//       of IHO ECDIS PRESENTATION LIBRARY USER'S MANUAL Ed/Rev 3.2 March 2000
//       (IHO Special Publication No. 52 ANNEX A of APPENDIX 2 --S52-A-2)
{
    int trace          = FALSE;   //TRUE = debug
    int best_nATTmatch = 0;  // best attribute value match

    return_if_null(LUPlist);
    return_if_null(geoData);

    // setup default LUP to the first LUP
    _LUP *LUP = LUPlist;

    // debug
    //if (0 == strncmp(LUPlist->OBCL, "SBDARE", 6)) {
    //    trace = 1;
    //    S57_dumpData(geoData, FALSE);
    //}
    //GString *FIDNstr = S57_getAttVal(geoData, "FIDN");
    //if (0==strcmp("2135158878", FIDNstr->str)) {
    //    trace = 1;
    //    S57_dumpData(geoData);
    //    PRINTF("%s\n", FIDNstr->str);
    //}

    // default LUP [ref S52-A-2:8.3.3.2]
    if (NULL == LUP->OBCLnext) {
        if (NULL == LUP->ATTC)
            return LUP;
        else {
            PRINTF("WARNING: single look-up non-empty attribute, RCID:%i\n", LUPlist->RCID);
            g_assert(0);
        }
    }

    // special case [S52-A-2:8.3.3.4(iii)]
    if (0 == strncmp(LUP->OBCL, "TSSLPT", S52_PL_NMLN)){
        if (NULL == S57_getAttVal(geoData, "ORIENT")) {
            // FIXME: hit this in S-64 ENC
            PRINTF("FIXME: TSSLPT found ... check this ... no ORIENT\n");
            //g_assert(0);
            return LUP;
        }
    }

    // Get next LUP --the first one is alway empty.
    LUPlist = LUPlist->OBCLnext;


    // Scan all LUP found for this S57 object
    // for the one that have the complet attribute name/value match.
    // [S52-A-2:8.3.3.3]
    while (LUPlist) {
        int      skipLUP   = 0;   //
        int      nATTmatch = 0;   // nbr of att value match for this LUP
        char    *attlv     = LUPlist->ATTC->str; // ATTL+ATTV

        if (trace)
            _dumpATT(attlv);

        while (*attlv != '\0' && !skipLUP) {
            char     attl[7] = {'\0'}; // attribute name
            GString *attv    = NULL;   // attribute value

            // scan object attribute name (ie propertie name in OGR)
            // for a name match
            strncat(attl, attlv, 6);
            attv = S57_getAttVal(geoData, attl);
            if (NULL != attv) {
                //PRINTF("attv: %s\n", attv->str);

                // Check for a attribute value match.
                // All attribute value must match.
                // So if attribute name doesn't matche try next LUP.

                // OK here we have an attribute name match
                // checking now for attribute value match

                // special case [S52-A-2:8.3.3.4(i)]
                // ie. use any attribute value (except value unknown)
                if ( (attlv[6] == ' ') && (0!=strcmp(attv->str, EMPTY_NUMBER_MARKER)) ) {
                    ++nATTmatch;

                } else {
                    // special case [S52-A-2:8.3.3.4(ii)]
                    // ie. match if value is unknown
                    if ( (attlv[6] == '?') && (0==strcmp(attv->str, EMPTY_NUMBER_MARKER)) ) {
                    //if ( (attlv[6] == '?') ) {
                        ++nATTmatch;
                        // give DRVAL1 = 0.0
                        // but should be unknown
                        // see CA49995A FIDN:327146 FIDS:9
                        // FIX: export PRESERVE_EMPTY_NUMBERS:ON


                    // value check
                    } else {
                        // no attribut value in LUP (ex ORIENT in TSSLPT)
                        if (attlv[6] == '\0')
                            ++nATTmatch;
                        else {
                            char *tmpVal = strstr(attv->str, attlv+6);
                            // must match *exacly*
                            // so '4,3,4' match '4,3,4,7' but not 3,4,3 (4,3 match)
                            // the trick is to use the lenght of of the value of
                            // the PLib *not* from S57
                            //char *tmpVal = strncmp(attv->str, attlv+6, strlen(attlv)-6);
                            // FIX: record the max lenght of att val str of a match
                            if (NULL != tmpVal) {
                                //int len = strlen(tmpVal);
                                //if (len < attv->len
                                //if (len >= best_attValLen) {
                                //    best_attValLen = len;
                                //    ++nATTmatch;
                                //}
                                int valS57 = atoi(attv->str);
                                int valLUP = atoi(attlv+6);
                                if (valS57 == valLUP)
                                    ++nATTmatch;
                            }
                            // skip this lookup
                            else
                                skipLUP = 1;
                        }
                    }
                }

                // get next attribute name/value for this LUP
                while (*attlv != '\0')
                    attlv++;  // find end of attribue name/value pair

                attlv++;  // skip end of field --witch is now a '\0'


            } else {
                skipLUP   = 1;
                nATTmatch = 0;
                //nATTmatch = 1;
                //PRINTF("SKIP\n");
            }

        } // while

        // BUG: the first match found is returned!
        //if (nATTmatch > best_nATTmatch) {
        // OR no match at all are discarded
        //if ((0 != nATTmatch) && (nATTmatch > best_nATTmatch)) {
        // FIX: last best match found, but zero match discarded
        // this seem like a S52 bug
        if ((0 != nATTmatch) && (nATTmatch >= best_nATTmatch)) {
                best_nATTmatch = nATTmatch;
                LUP = LUPlist;
                if (trace)
                    PRINTF("CANDIDATE(%i): %s\n", best_nATTmatch, LUPlist->ATTC->str);
        }

        LUPlist = LUPlist->OBCLnext;
        //nATTmatch = 0;

    } // while

    if (trace)
        PRINTF("SELECTED LUP: %s\n", LUP->INST->str);

    return LUP;
}

// command word
#define CMDWRD(s,t)   if (0==strncmp(#s, str, 2)) {  \
                              str += 3;              \
                              cmd->cmdWord = t;      \
                              cmd->param   = str;

#define LOOKUP(dbnm)  cmd->cmd.def = (_S52_cmdDef*)g_tree_lookup(_selSMB(dbnm), str);                     \
                      if (cmd->cmd.def == NULL) {                                                         \
                          cmd->cmd.def = (_S52_cmdDef*)g_tree_lookup(_selSMB(dbnm), (void*) "QUESMRK1");  \
                          PRINTF("WARNING: no lookup %s, %i, default to QUESMRK1\n", str, dbnm);            \
                      }

// scan foward stop on ; or end-of-line
#define SCANFWRD    while ( !(*str == ';' || *str == '\0')) str++; }

static _cmdWL    *_parseINST(GString *inst)
// Parse "Symbology Instruction"  (LUP) and link them to rendering rules
{
    char      *str  = inst->str;
    _cmdWL *top  = NULL;
    _cmdWL *last = NULL;

    // assume that the previous object that used this CS lookup
    // will have saved the instruction command word.
    // object of same classe can have different command word
    // because of the Cond. Symb.

    //PRINTF("_LUP2cmd str:%s\n", str);
    while (*str != '\0') {
        // 'marfea' end with ';'
        if (*str == ';') {
            str++;  // skip ';'
            continue;
        }

        _cmdWL *cmd = g_new0(_cmdWL, 1);
        //_cmdWL *cmd = g_try_new0(_cmdWL, 1);
        if (NULL == cmd)
            g_assert(0);

        ////////////////////////////////
        // parse Symbology Command Word
        //
        // NOTE: command might repeat except:
        //  -S52_CMD_COM_LN: complex line,
        //  -S52_CMD_ARE_CO: area color,
        //  -S52_CMD_CND_SY: conditional symbology

        // SHOWTEXT
             CMDWRD(TX, S52_CMD_TXT_TX) SCANFWRD
        else CMDWRD(TE, S52_CMD_TXT_TE) SCANFWRD

        // SHOWPOINT
        else CMDWRD(SY, S52_CMD_SYM_PT) LOOKUP(S52_SMB_SYMB) SCANFWRD

        // SHOWLINE
        else CMDWRD(LS, S52_CMD_SIM_LN) SCANFWRD
        else CMDWRD(LC, S52_CMD_COM_LN) LOOKUP(S52_SMB_LINE) SCANFWRD

        // SHOWAREA
        else CMDWRD(AC, S52_CMD_ARE_CO) SCANFWRD
        else CMDWRD(AP, S52_CMD_ARE_PA) LOOKUP(S52_SMB_PATT) SCANFWRD

        // CALLSYMPROC
        else CMDWRD(CS, S52_CMD_CND_SY) LOOKUP(S52_SMB_COND) SCANFWRD

        // OVERRIDE PRIORITY (not in S52 specs.)
        else CMDWRD(OP, S52_CMD_OVR_PR) SCANFWRD

        // failsafe
        else {
            PRINTF("ERROR: parsing Command Word:%s\n", str);
            g_assert(0);
        }

        // append command
        if (top == NULL) {
            top = cmd;
            last= top;
        } else {
            last->next = cmd;
            last= cmd;
        }
    }  /* while */

    // case when PLib say to draw nothing
    //if (NULL == top) {
    //     PRINTF("NOTE: LUP for %s\n", str);
    //    g_assert(0);
    //}

    return top;
}


static _cmdWL    *_initCmdA(_S52_obj *obj, int alt)
// return the CS command in cmd list, else NULL
// start to fill the command array upto the first CS
{
    _cmdWL *cmd = obj->cmdLorig[alt];

    g_array_set_size(obj->cmdAfinal[alt], 0);

    // scan for a CS
    while (NULL != cmd) {
        // there can only be one CS (CND_SY) per LUP
        if (S52_CMD_CND_SY == cmd->cmdWord) {
            return cmd;
            //break;
        }
        g_array_append_val(obj->cmdAfinal[alt], *cmd);
        cmd = cmd->next;
    }

    return cmd;
}

static int        _freeCmdList(_cmdWL *top)
// free command list
{
   while (top != NULL) {
      _cmdWL *cmd = top->next;
      g_free(top);
      top = cmd;
   }

   return TRUE;
}

static gint       _freeTXT(_Text *text)
{
    if (NULL != text->frmtd) {
        g_string_free(text->frmtd, TRUE);
        text->frmtd = NULL;
    }
    g_free(text);

    return TRUE;
}

static gint       _freeAllTXT(GArray *cmdArray)
// free all text
{
    for (guint i=0; i<cmdArray->len; ++i) {
        _cmdWL *cmd = &g_array_index(cmdArray, _cmdWL, i);

        if ((S52_CMD_TXT_TX==cmd->cmdWord) || (S52_CMD_TXT_TE==cmd->cmdWord)) {
            if (NULL != cmd->cmd.text) {
                _freeTXT(cmd->cmd.text);
                cmd->cmd.text = NULL;
            }
        }
    }

    return TRUE;
}


static int        _resolveSMB(_S52_obj *obj, int alt)
// return TRUE if there is a CS in the INST file for this LUP (alt)
// also fill the command Array
{
    // debug
    //S57_geo *geoData = S52_PL_getGeo(obj);
    //GString *FIDNstr = S57_getAttVal(geoData, "FIDN");
    //if (NULL != FIDNstr && 0==strcmp("2135161787", FIDNstr->str)) {
    //    PRINTF("%s\n",FIDNstr->str);
    //}

    // clear old CS instruction
    if (NULL != obj->CSinst[alt])
        g_string_free(obj->CSinst[alt], TRUE);
    obj->CSinst[alt] = NULL;

    // clear old CS command list
    _freeCmdList(obj->CScmdL[alt]);
    obj->CScmdL[alt] = NULL;

    // search list for CS, start building command array
    _cmdWL *cmd = _initCmdA(obj, alt);
    if (NULL == cmd)
        return FALSE;


    // CS found, merge cmd list in command array (normal + CS)

    // reset priority override
    obj->prioOveride = FALSE;
    obj->DPRI = obj->LUP->DPRI;
    obj->RPRI = obj->LUP->RPRI;
    obj->DISC = obj->LUP->DISC;
    obj->LUCM = obj->LUP->LUCM;

    // expand CS
    S52_CS_cb CScb = cmd->cmd.CS->CScb;
    if (NULL != CScb) {
        obj->CSinst[alt] = CScb(obj->geoData);
        if (NULL != obj->CSinst[alt]) {
            _cmdWL *tmp   = NULL;
            obj->CScmdL[alt] = _parseINST(obj->CSinst[alt]);
            tmp = obj->CScmdL[alt];
            while (NULL != tmp) {
                // change object Display Priority, if any, at this point
                if (S52_CMD_OVR_PR == tmp->cmdWord) {
                    char *c = tmp->param;

                    obj->prioOveride = TRUE;

                    // Display Priority
                    if (S52_PRIO_NOPRIO != c[0])
                        obj->DPRI = (S52_disPrio) (c[0] - '0');

                    // 'O' or 'S', Radar Priority
                    if (S52_PRIO_NOPRIO != c[1])
                        obj->RPRI = (S52_RadPrio) c[1];

                    // Display Categorie: D/S/O, (ie baseDisplay, Standard, Other)
                    if (S52_PRIO_NOPRIO != c[2])
                        obj->DISC = (S52_DisCat) c[2];

                    // Look-Up Comment
                    if (S52_PRIO_NOPRIO != c[3])
                        sscanf(c+3, "%d",&obj->LUCM);
                }

                // continue to fill array with expanded CS
                g_array_append_val(obj->cmdAfinal[alt], *tmp);
                tmp = tmp->next;
            }
        } else {
            // FIXME: ENC_ROOT/US3NY21M/US3NY21M.000 land here
            PRINTF("NOTE: CS for object %s expand to NULL\n", S57_getName(obj->geoData));
            //g_assert(0);
            return FALSE;
        }
    } else {
        PRINTF("ERROR: CS not found (%s)\n", S57_getName(obj->geoData));
        return FALSE;
    }

    // jump over this CS cmd
    cmd = cmd->next;
    // finish to fill/append to cmd array the remaining cmd in list
    while (NULL != cmd) {
        g_array_append_val(obj->cmdAfinal[alt], *cmd);
        cmd = cmd->next;
    }

    return TRUE;
}

int         S52_PL_resloveSMB(_S52_obj *obj)
{
    return_if_null(obj);

    // First: delete all text attache to each command word
    // this will force to re-parse text
    S52_PL_resetParseText(obj);

    // Then: pull new command word from CS (wich could have diffent text)
    _resolveSMB(obj, 0);
    _resolveSMB(obj, 1);

    return TRUE;
}

//-------------------------
//
// PLIB PARSER SECTION
//
//-------------------------


static int        _parsePos(_Position *pos, char *buf, gboolean patt)
{
   if (patt) {
      sscanf(buf, "%5d%5d", &pos->minDist.PAMI, &pos->maxDist.PAMA);
      //sscanf(buf, "%5i%5i", &pos->minDist.PAMI, &pos->maxDist.PAMA);
      //PRINTF("sscanf test:%d %d %s\n",pos->minDist.PAMI, pos->maxDist.PAMA, buf);
      buf += 10;
   }

   sscanf(buf, "%5d%5d%5d%5d%5d%5d",&pos->pivot_x.PACL,&pos->pivot_y.PARW,
   //sscanf(buf, "%5i%5i%5i%5i%5i%5i",&pos->pivot_x.PACL,&pos->pivot_y.PARW,
                                    &pos->bbox_w.PAHL,&pos->bbox_h.PAVL,
                                    &pos->bbox_x.PBXC,&pos->bbox_y.PBXR);

   //PRINTF("w:%i h:%i\n",pos->bbox_w.PAHL,pos->bbox_h.PAVL);
   return TRUE;
}

static S52_Color *_parseCol(char c, char *colRef)
{

    while(*colRef != '\0'){
        if( *colRef == c)
            break;
        //else
        colRef+=6;
    }

    if ('\0' == *colRef) {
        // failsafe
        PRINTF("ERROR: colour not found in PLib color list\n");
        g_assert(0);
        return S52_PL_getColor("DNGHL");
    } else
        return S52_PL_getColor(colRef+1);
}

//static int        _filterVector(char *str, char *colRef, colorIdx *colIdx)
static guint      _filterVector(char *str, char *colRef, S52_Color *colors)
// resolve color index
// move color & transparency to 'colors'
// weed out redundent pen width/trans/color command
// return number of color switch (or number of sub-list of vector)
// FIXME: what if there is no color command!
// FIXME: refactor this whole color handling (trans uglyness)
{
    int  nList = 0;   // number sub-list/sub-vector
    char color = 0;
    char pen_w = 0;   // no pen width
    int  idx   = 0;   // pos in 'str'

    //int  curVecPen =  0;   // check if pen_w change for this color
    //char oldTrans  = '0';
    //int  curVecTrn =  0;   // check if trans change for this color


    if (S52_VC_SP!=*str && S52_VC__P!=*(str+1)) {
        PRINTF("FIXME: color command is not the verry first one!\n");
        g_assert(0);
    }


    // paranoia
    if (NULL == colors) {
        PRINTF("ERROR: no colors\n");
        g_assert(0);
    }

    while ('\0' != str[idx]) {
        //S52_Color *col = NULL;

        char *c1 = &str[idx];     // first character
        char *c2 = &str[idx+1];   // second character

        // 'S' 'P' --color
        if (S52_VC_SP==*c1 && S52_VC__P==*c2) {
            // if same color replace cmd with NOP cmd
            if (str[idx+2] != color) {

                S52_Color *col = _parseCol(str[idx+2], colRef);
                // init pen width
                //col->pen_w = 0;

                // advance to next color
                if ('\0' != color)
                    ++colors;

                *colors = *col;

                color   = str[idx+2];

                // to indicate beginning of new sub-vec
                *c1 = 'X';

                // tally sub list
                ++nList;

            } else {
                // same color --NOP
                *c1 = ';';
            }

            *c2        = ';';
            str[idx+2] = ';';  // overwright color (NOP)

            // advance to next command
            idx += 4;

            // go right to next command
            continue;
        }

        // 'S' 'W' --pen width
        if (S52_VC_SP==*c1 && S52_VC_SW==*c2) {
            // if same pen width replace cmd with NOP cmd
            if (str[idx+2] == pen_w) {
                *c1        = ';';
                *c2        = ';';
                str[idx+2] = ';';   // overwright pen_w (NOP)

            } else {
                pen_w = str[idx+2];
            }

            // for VBO, change of 'colors' need change in pen width
            if (0 == colors->pen_w)
                colors->pen_w = pen_w;

            idx += 4;

            continue;
        }

        // 'S' 'T' --transparency
        if (S52_VC_SP==*c1 && S52_VC_ST==*c2) {

            colors->trans = str[idx+2];

            *c1        = ';';
            *c2        = ';';
            str[idx+2] = ';';   // overwright trans. (NOP)

            idx += 4;

            continue;
        }

        // 'S' 'C' --symbol call
        if (S52_VC_SP==*c1 && S52_VC_SC==*c2) {
            *c1 = ';';
            *c2 = ';';
            idx += 2;
            while (';'!=str[idx] && '\0'!=str[idx]) {
                str[idx] = ';';   // overwright symbol call (NOP)
                ++idx;
            }
            continue;
        }

        // advance to begining of next command (or '\0')
        while (';'!=str[idx] && '\0'!=str[idx])
            ++idx;

        if (';' == str[idx])
            ++idx;
    }


    // FIXME: is a dynamic structure needed to prevent this!
    // Note that this will occure immediatly at start-up
    // and only if a home-made PLib is used.
    if (MAX_SUBLIST < nList) {
        PRINTF("ERROR: buffer overflow about to occur\n");
        g_assert(0);
    }

    return nList;
}

static int        _parseLBID(_PL *fp)
{
    // FIXME: 'fp' useless
    return_if_null(fp);

    _LBID *plib = g_new0(_LBID, 1);
    //_LBID *plib = g_try_new0(_LBID, 1);
    if (NULL == plib)
        g_assert(0);

    sscanf(_pBuf+11, "%d", &plib->RCID);
    plib->EXPP  = _pBuf[16];
    plib->ID    = g_string_new(_pBuf+16);
    //PRINTF("FIXME: parse ID of PLib\n");

    if (NULL != _plibID)
        plib->next = _plibID;
    _plibID = plib;

    return TRUE;
}

static _colTable *_findColTbl(const char *tblName)
{
    return_if_null(tblName);

    for (guint i=0; i<_colTables->len; ++i) {
        _colTable *pct =  &g_array_index(_colTables, _colTable, i);
        //if (0 == g_ascii_strcasecmp(tblName, pct->tableName->str))
        if (0 == g_strcmp0(tblName, pct->tableName->str))
            return pct;
    }

    return NULL;
}

static int        _cms_xyL2rgb(S52_Color *c);
#if 0
static int        _readColor(_PL *fp)
{

    _readS52Line(fp, _pBuf);
    while ( 0 != strncmp(_pBuf, "****",4)) {
        S52_Color c;

        // debug
        //PRINTF("%s\n", _pBuf);

        memset(&c, 0, sizeof(S52_Color));
        _chopAtEOL(_pBuf, ' ');
        strncpy(c.colName, _pBuf+9, 5);
        c.x = S52_atof(_pBuf+14);
        c.y = S52_atof(_pBuf+21);
        c.L = S52_atof(_pBuf+28);

        c.trans = '0';  // default to opaque

        _cms_xyL2rgb(&c);
        // debug
        //PRINTF("%s %f %f %f -> %i %i %i\n", c.colName, c.x, c.y, c.L, c.R, c.G, c.B);

        if (NULL == _colref) {
            g_array_append_val(ct.colors, c);
        } else {
            S52_Color *cref = (S52_Color *) g_tree_lookup(_colref, (gpointer*)c.colName);
            if (NULL != cref) {

                // check if color already loaded, if so
                // update color value in table
                if (63 == ct.colors->len) {
                    // table full --update color
                    S52_Color *c1 = &g_array_index(ct.colors, S52_Color, cref->cidx);
                    guchar idx = cref->cidx;
                    *c1      = c;
                     c1->cidx = idx;
                } else {
                    c.cidx = cref->cidx;
                    g_array_insert_val(ct.colors, cref->cidx, c);
                    //g_array_append_val(ct.colors, c);
                }


            } else {
                // something broke the PLib !?
                // or the first table was incomplet !?
                //PRINTF("ERROR: color %s not in ref table: %p \n", c.colName, _colref);
                PRINTF("ERROR: color %s not in ref table\n", c.colName);
                g_assert(0);
            }
        }

        _readS52Line(fp, _pBuf);
    }
}
#endif

static int        _readColor(_PL *fp, GArray *colors)
{
    _readS52Line(fp, _pBuf);
    while ( 0 != strncmp(_pBuf, "****",4)) {
        S52_Color c;

        memset(&c, 0, sizeof(S52_Color));
        _chopAtEOL(_pBuf, ' ');
        strncpy(c.colName, _pBuf+9, 5);

        c.x     = S52_atof(_pBuf+14);
        c.y     = S52_atof(_pBuf+21);
        c.L     = S52_atof(_pBuf+28);
        c.trans = '0';  // default to opaque

        _cms_xyL2rgb(&c);
        //PRINTF("%s %f %f %f -> %i %i %i\n", c.colName, c.x, c.y, c.L, c.R, c.G, c.B);

        //S52_Color *cref = (S52_Color *) g_tree_lookup(_colref, (gpointer*)c.colName);
        gpointer idx = g_tree_lookup(_colref, (gpointer*)c.colName);
        if (NULL != idx) {
            // color ref index - 1 == array index
            int i = GPOINTER_TO_INT(idx) - 1;

            if (i >= S52_COL_NUM) {
                PRINTF("ERROR: color index i >= S52_COL_NUM\n");
                g_assert(0);
            }

            c.cidx = i; // optimisation

            S52_Color *c1 = &g_array_index(colors, S52_Color, i);

            *c1 = c;
        } else {
            // something broke the PLib !?
            PRINTF("ERROR: color %s not in ref table\n", c.colName);
            g_assert(0);
        }

        _readS52Line(fp, _pBuf);
    }

    return TRUE;
}

static int        _flushColors()
{
    if (NULL != _colTables) {
        //unsigned int i = 0;
        for (guint i=0; i<_colTables->len; ++i) {
            _colTable *ct = &g_array_index(_colTables, _colTable, i);
            if (NULL != ct->colors)    g_array_free(ct->colors, TRUE);
            //if (NULL != ct->colors)    g_array_unref(ct->colors);
            if (NULL != ct->tableName) g_string_free(ct->tableName, TRUE);
            ct->colors    = NULL;
            ct->tableName = NULL;
        }
        g_array_set_size(_colTables, 0);
    } else {
        PRINTF("WARNING: trying to deleted PL Color Table twice!\n");
        g_assert(0);
    }

    //_flush_Colors = FALSE;

    return TRUE;
}

static int        _parseCOLS(_PL *fp)
{
    //unsigned int i = 0;
    _colTable *pct = NULL;
    //int replaceColor = FALSE;

    //if (TRUE == _flush_Colors)
    //    _flushColors();

    // trap color status that are not implemented
    switch (_pBuf[16]) {
    case _COL_TBL_NIL: break;  // NEW
    case _COL_TBL_ADD: ;       // TODO
    case _COL_TBL_MOD: ;       // TODO
    case _COL_TBL_DEL: ;       // TODO
    default:
        PRINTF("ERROR: unknown color table status\n");
        g_assert(0);
        return FALSE;
    }

    pct = _findColTbl(_pBuf+19);
    // NEW patelette
    if (NULL == pct) {
        _colTable  ct;
        // S52 say 15 char - could be any lenght
        ct.tableName = g_string_new(_pBuf+19);
        // Note: only 63 color are loaded from PLib (#64 is TRANS)
        ct.colors    = g_array_new(FALSE, FALSE, sizeof(S52_Color));
        g_array_set_size(ct.colors, S52_COL_NUM);

        g_array_append_val(_colTables, ct);

        // fetch entree
        pct = _findColTbl(_pBuf+19);
        if (NULL == pct) {
            PRINTF("ERROR: _findColTbl() failed\n");
            g_assert(0);
        }
    }

    _readColor(fp, pct->colors);

    // make sure the new table is full
    if (pct->colors->len != (guint) g_tree_nnodes(_colref)) {
        PRINTF("ERROR: color tables size mismatch\n");
        g_assert(0);
    }

    return TRUE;
}

static int        _parseLUPT(_PL *fp)
{
    return_if_null(fp);

    int      len      = 0;
    gboolean inserted = FALSE;
    _LUP *LUP         = g_new0(_LUP, 1);
    //_LUP *LUP      = g_try_new0(_LUP, 1);
    if (NULL == LUP)
        g_assert(0);


    sscanf(_pBuf+11, "%d", &LUP->RCID);
    //sscanf(_pBuf+11, "%i", &LUP->RCID);
    strncpy(LUP->OBCL, _pBuf+19, S52_PL_NMLN);
    //LUP->FTYP = (S52_Obj_t  )  _pBuf[25];
    LUP->FTYP = (S57_Obj_t  )  _pBuf[25];
    LUP->DPRI = (S52_disPrio) (_pBuf[30] - '0');
    LUP->RPRI = (S52_RadPrio)  _pBuf[31];
    LUP->TNAM = (   _LUPtnm )  _pBuf[36];

    if ('O'!=LUP->RPRI && 'S'!=LUP->RPRI)
        LUP->RPRI = (S52_RadPrio) 'O';  // failsafe

    // debug
    //if (LUP->TNAM == 103)
    //    PRINTF("found LUP TNAM\n");

    len = _readS52Line(fp, _pBuf);

    do {
        FIELD(ATTC) {                     // field do repeat
            if (_pBuf[9] != '\0') {            // could be empty!
                if (NULL != LUP->ATTC) {
                    PRINTF("ERROR repeating field ATTC not implemented!\n");
                } else {
                    _pBuf[len-1] = EOL;  // change "\0\0" to "EOL\0" for _chopS52Line()
                    // FIXME: use g_string_new_len () in glib-2.0
                    //LUP->ATTC = g_string_new_len(_pBuf+9, len-9);
                    LUP->ATTC = g_string_new(_pBuf+9);
                    _chopAtEOL(LUP->ATTC->str, '\0');   // rechop the line (ie "xyz\0zxy\0yzx\0\0")
                }
            }
        }

        FIELD(INST) { LUP->INST = g_string_new(_pBuf+9); }
        FIELD(DISC) {
            LUP->DISC = (S52_DisCat) _pBuf[9];
            // check if MARINER, remap to S52_DisCat MARINER
            if ('M' == _pBuf[9])
                LUP->DISC = (S52_DisCat) (_pBuf[18] + 1);
        }
        FIELD(LUCM) { sscanf(_pBuf+9, "%d",&LUP->LUCM);  }
        //FIELD(LUCM) { sscanf(_pBuf+9, "%i",&LUP->LUCM);  }

        FIELD(****) {
            GTree *LUPtype = _selLUP(LUP->TNAM);
            _LUP  *LUPtop  = (_LUP*)g_tree_lookup(LUPtype, (gpointer*)LUP->OBCL);

            // debug
            //if (0==strcmp(LUP->OBCL, "BUISGL")) {
            //    PRINTF("BUISGL found\n");
            //}

            // insert in BBTree if not already there
            if (NULL == LUPtop)
                //g_tree_insert(LUPtype, (gpointer*)key, (gpointer*)LUP);
                g_tree_insert(LUPtype, (gpointer*)LUP->OBCL, (gpointer*)LUP);
            else {
                int   replace = FALSE;
                _LUP *LUPprev = NULL;
                _LUP *LUPtmp  = NULL;

                // replace if it's the first one (top) in the tree
                // WARNING: the old LUP chain will be deleted
                if ((NULL == LUP->ATTC) && (NULL == LUPtop->ATTC)) {
                    LUP->OBCLnext    = LUPtop->OBCLnext;
                    LUPtop->OBCLnext = NULL;
                    LUP->sup         = LUPtop->sup; // keep user setting (supression)

                    g_tree_replace(LUPtype, (gpointer*)LUP->OBCL, (gpointer*)LUP);
                    //S52_tree_replace(LUPtype, (gpointer*)LUP->OBCL, (gpointer*)LUP);

                    LUPtop  = LUP;
                    replace = TRUE;
                } else {
                    // replace with new one
                    // start from the top so that 'LUPprev' get initialize
                    LUPtmp = LUPtop;
                    while (NULL != LUPtmp) {
                        // replace
                        if ((NULL != LUP->ATTC) && (NULL != LUPtmp->ATTC) &&
                            //(TRUE == g_string_equal(LUPtmp->ATTC, LUP->ATTC)) )
                            (TRUE == S52_string_equal(LUPtmp->ATTC, LUP->ATTC)) )
                        {   // can't replace more then one LUP
                            // the compination of LUP NAME & LUP ATTC is unique for all LUP
                            // this is juste to make sure that the list is consistant
                            if (TRUE == replace) {
                                PRINTF("ERROR: TRUE == replace\n");
                                g_assert(0);
                            }

                            replace = TRUE;

                            // keep user setting (supression)
                            LUP->sup = LUPtmp->sup;

                            // link previous LUP to this new one
                            if (NULL != LUPprev)
                                LUPprev->OBCLnext = LUP;

                            // link to next LUP
                            LUP->OBCLnext = LUPtmp->OBCLnext;
                            // this stop removing the whole chain
                            LUPtmp->OBCLnext = NULL;
#ifdef S52_USE_GLIB2
                            _delLUP(LUPtmp);
#else
                            _delLUP((gpointer*)LUPtmp->OBCL, (gpointer*)LUP, NULL);
#endif
                            LUPtmp = LUP;
                        }

                        LUPprev = LUPtmp;
                        LUPtmp  = LUPtmp->OBCLnext;
                    }
                }

                // this LUP is not a replacement --insert at the end
                if (FALSE == replace) {

                    if (NULL==LUPtmp && NULL!=LUPprev)
                        LUPprev->OBCLnext = LUP;
                    else {
                        PRINTF("ERROR: should be at the end of the list\n");
                        g_assert(0);
                    }
                }
            }

            // string '*****' reached, this mark the end of this LUP
            inserted = TRUE;
        }

        _readS52Line(fp, _pBuf);

    } while (inserted == FALSE);

    return len;
}
                // check to see that object class belong
                // to the same OpenEV layer (experimental)
                /*
                 bool dumpdata = FALSE;
                 if (dumpdata = (LUPtmp->DPRI != LUP->DPRI))
                 PRINTF("Object Class has different display priority\n");
                 else
                 if (dumpdata = (LUPtmp->RPRI != LUP->RPRI))
                 PRINTF("Object Class has different RADAR priority\n");

                 if (dumpdata) {
                 PRINTF("NEW LUP RecID:%d ObjNm:%s prio:%c rad:%c\n",
                 LUP->RCID,LUP->OBCL,LUP->DPRI,LUP->RPRI);
                 PRINTF("TOP LUP RecID:%d ObjNm:%s prio:%c rad:%c\n",
                 LUPtmp->RCID,LUPtmp->OBCL,LUPtmp->DPRI,LUPtmp->RPRI);
                 }
                 */

static int        _parseLNST(_PL *fp)
// LiNe STyle
{
    return_if_null(fp);

    //int  len = 0;
    gboolean    inserted = FALSE;
    //_S52_cmdDef *lnstmp   = NULL;
    _S52_cmdDef *lnst     = g_new0(_S52_cmdDef, 1);
    //_S52_cmdDef *lnst     = g_try_new0(_S52_cmdDef, 1);
    if (NULL == lnst)
        g_assert(0);


    lnst->symType                = S52_SMB_LINE;
    lnst->exposition.LXPO        = g_string_new('\0');
    lnst->shape.line.vector.LVCT = g_string_new('\0');

    sscanf(_pBuf+11, "%d", &lnst->RCID);
    //sscanf(_pBuf+11, "%i", &lnst->RCID);

    _readS52Line(fp, _pBuf);
    do {
        FIELD(LIND) {
            strncpy(lnst->name.LINM, _pBuf+9, S52_SMB_NMLN);
            _parsePos(&lnst->pos.line, _pBuf+17, FALSE);
        }

        FIELD(LXPO) { lnst->exposition.LXPO        = g_string_append( lnst->exposition.LXPO, _pBuf+9 ); }
        FIELD(LVCT) { lnst->shape.line.vector.LVCT = g_string_append( lnst->shape.line.vector.LVCT, _pBuf+9 ); }
        FIELD(LCRF) { lnst->colRef.LCRF            = g_string_new(_pBuf+9 ); }  // CIDX + CTOK
        FIELD(****) {

            // update (replace)
            //g_tree_replace(_selSMB(S52_SMB_LINE), (gpointer*)lnst->name.LINM, (gpointer*)lnst);
            S52_tree_replace(_selSMB(S52_SMB_LINE), (gpointer*)lnst->name.LINM, (gpointer*)lnst);

            inserted = TRUE;
        }
        _readS52Line(fp, _pBuf);

    } while (inserted == FALSE);

    // not sure if new PLib symb can be NOT inserted
    if (TRUE == inserted) {
        lnst->DListData.create = TRUE;
        //lnst->DListData.len    = _filterVector(lnst->shape.line.vector.LVCT->str,
        lnst->DListData.nbr    = _filterVector(lnst->shape.line.vector.LVCT->str,
                                               lnst->colRef.LCRF->str,
                                               lnst->DListData.colors);
    }

    return TRUE;
}

static int        _parsePATT(_PL *fp)
{
    return_if_null(fp);

    //int  len = 0;
    gboolean    inserted = FALSE;
    //_S52_cmdDef *pattmp   = NULL;
    _S52_cmdDef *patt     = g_new0(_S52_cmdDef, 1);
    //_S52_cmdDef *patt     = g_try_new0(_S52_cmdDef, 1);
    if (NULL == patt)
        g_assert(0);


    patt->symType                = S52_SMB_PATT;
    patt->exposition.PXPO        = g_string_new('\0');   // field repeat
    patt->shape.patt.vector.PVCT = g_string_new('\0');   // field repeat

    sscanf(_pBuf+11, "%d", &patt->RCID);
    //sscanf(_pBuf+11, "%i", &patt->RCID);

    _readS52Line(fp, _pBuf);

    do {
        FIELD(PATD) {
            strncpy(patt->name.PANM, _pBuf+9, S52_SMB_NMLN);
            //PRINTF("%8s ", patt->name.PANM);
            patt->definition.PADF = _pBuf[17];
            patt->fillType.PATP   = _pBuf[18];
            patt->spacing.PASP    = _pBuf[21];
            _parsePos(&patt->pos.patt, _pBuf+24, TRUE);
        }

        FIELD(PBTM) { PRINTF("ERROR: pattern bitmap not in specs\n"); }
        FIELD(PXPO) { patt->exposition.PXPO        = g_string_append(patt->exposition.PXPO, _pBuf+9); }
        FIELD(PVCT) { patt->shape.patt.vector.PVCT = g_string_append(patt->shape.patt.vector.PVCT, _pBuf+9); }
        FIELD(PCRF) { patt->colRef.PCRF            = g_string_new(_pBuf+9); } // CIDX + CTOK
        FIELD(****) {

            //g_tree_replace(_selSMB(S52_SMB_PATT), (gpointer*)patt->name.PANM, (gpointer*)patt);
            S52_tree_replace(_selSMB(S52_SMB_PATT), (gpointer*)patt->name.PANM, (gpointer*)patt);

            inserted = TRUE;
        }
        _readS52Line(fp, _pBuf);

    } while (inserted == FALSE);

    if (TRUE == inserted) {
        patt->DListData.create = TRUE;
        //patt->DListData.len    = _filterVector(patt->shape.patt.vector.PVCT->str,
        patt->DListData.nbr    = _filterVector(patt->shape.patt.vector.PVCT->str,
                                               patt->colRef.PCRF->str,
                                               patt->DListData.colors);
    }

    return TRUE;
}

static int        _parseSYMB(_PL *fp)
{
    return_if_null(fp);

    //int  len = 0;
    gboolean    inserted = FALSE;
    _S52_cmdDef *symb    = g_new0(_S52_cmdDef, 1);
    //_S52_cmdDef *symb     = g_try_new0(_S52_cmdDef, 1);
    if (NULL == symb)
        g_assert(0);

    symb->symType                = S52_SMB_SYMB;
    symb->exposition.SXPO        = g_string_new('\0');
    symb->shape.symb.vector.SVCT = g_string_new('\0');

    sscanf(_pBuf+11, "%d", &symb->RCID);
    //sscanf(_pBuf+11, "%i", &symb->RCID);

    _readS52Line(fp, _pBuf);

    do {
        FIELD(SYMD) {
            strncpy(symb->name.SYNM, _pBuf+9, S52_SMB_NMLN);
            symb->definition.SYDF = _pBuf[17];
            _parsePos(&symb->pos.symb, _pBuf+18, FALSE);
        }

        FIELD(SBTM) { PRINTF("ERROR: symbol bitmap not in specs\n"); }
        FIELD(SXPO) { symb->exposition.SXPO        = g_string_append( symb->exposition.SXPO, _pBuf+9); }
        FIELD(SVCT) { symb->shape.symb.vector.SVCT = g_string_append( symb->shape.symb.vector.SVCT, _pBuf+9); }
        FIELD(SCRF) { symb->colRef.SCRF            = g_string_new(_pBuf+9); } // CIDX + CTOK
        FIELD(****) {

            // debug
            //PRINTF("add %s\n", symb->name.SYNM);
            //if (0==strcmp(symb->name.SYNM, "OWNSHP05")) {
            //    PRINTF("FOUND SYMBOL: %s \n", symb->name.SYNM);
            //}
            //if (0==strcmp(symb->name.SYNM, "SCALEB10")) {
            //    PRINTF("FOUND SYMBOL: %s \n", symb->name.SYNM);
            //}


            //g_tree_replace(_selSMB(S52_SMB_SYMB), (gpointer*)symb->name.SYNM, (gpointer*)symb);
            S52_tree_replace(_selSMB(S52_SMB_SYMB), (gpointer*)symb->name.SYNM, (gpointer*)symb);

            inserted = TRUE;
        }
        _readS52Line(fp, _pBuf);

    } while (inserted == FALSE );

    if (TRUE == inserted) {
        // debug
        //if (0==strcmp(symb->name.SYNM, "BOYLAT14")) {
        //    PRINTF("FOUND SYMBOL: %s \n", symb->name.SYNM);
        //}

        symb->DListData.create = TRUE;
        //symb->DListData.len    = _filterVector(symb->shape.line.vector.SVCT->str,
        symb->DListData.nbr    = _filterVector(symb->shape.line.vector.SVCT->str,
                                               symb->colRef.SCRF->str,
                                               symb->DListData.colors);
    }

    return TRUE;
}

static int        _initPL()
{

    // init allready done
    if (NULL != _colTables)
        return TRUE;

    _colTables = g_array_new(FALSE, FALSE, sizeof(_colTable));

    _colref = g_tree_new(_cmpCOL);
    //_colref = g_tree_new_full(_cmpCOL, NULL, NULL, NULL);

    for (guint i=1; i<=S52_COL_NUM; ++i) {
        gpointer pint = GINT_TO_POINTER(i);
        g_tree_insert(_colref, (gpointer)_colorName[i-1], pint);
    }



#ifdef S52_USE_GLIB2
    _table[LUP_LINE]     = g_tree_new_full(_cmpLUP, NULL, NULL, _delLUP);
    _table[LUP_AREA_PLN] = g_tree_new_full(_cmpLUP, NULL, NULL, _delLUP);
    _table[LUP_AREA_SYM] = g_tree_new_full(_cmpLUP, NULL, NULL, _delLUP);
    _table[LUP_PT_SIMPL] = g_tree_new_full(_cmpLUP, NULL, NULL, _delLUP);
    _table[LUP_PT_PAPER] = g_tree_new_full(_cmpLUP, NULL, NULL, _delLUP);

    _table[SMB_LINE]     = g_tree_new_full(_cmpSMB, NULL, NULL, _delSMB);
    _table[SMB_PATT]     = g_tree_new_full(_cmpSMB, NULL, NULL, _delSMB);
    _table[SMB_SYMB]     = g_tree_new_full(_cmpSMB, NULL, NULL, _delSMB);

    _table[SMB_COND]     = g_tree_new(_cmpCOND);
#else
    _table[LUP_LINE]     = g_tree_new(_cmpLUP);
    _table[LUP_AREA_PLN] = g_tree_new(_cmpLUP);
    _table[LUP_AREA_SYM] = g_tree_new(_cmpLUP);
    _table[LUP_PT_SIMPL] = g_tree_new(_cmpLUP);
    _table[LUP_PT_PAPER] = g_tree_new(_cmpLUP);

    _table[SMB_LINE]     = g_tree_new(_cmpSMB);
    _table[SMB_PATT]     = g_tree_new(_cmpSMB);
    _table[SMB_SYMB]     = g_tree_new(_cmpSMB);

    _table[SMB_COND]     = g_tree_new(_cmpCOND);
#endif

    return TRUE;
}

static int        _loadPL(_PL *fp)
{
    //_initPL();

    // FIXME: loading new palette reset "current selected palette"
    //_crntPaletteID = -1;

    // if colors are found when parsing the PLib
    // then flush the old one
    //_flush_Colors = TRUE;

    while (-1 != _readS52Line(fp, _pBuf) ) {
        // !!! order important !!!
        FIELD(LBID) { _parseLBID(fp); }
        FIELD(COLS) { _parseCOLS(fp); }
        FIELD(LUPT) { _parseLUPT(fp); }
        FIELD(LNST) { _parseLNST(fp); }
        FIELD(PATT) { _parsePATT(fp); }
        FIELD(SYMB) { _parseSYMB(fp); }

        FIELD(0001) { continue; }
        FIELD(****) { continue; }

        //PRINTF("read:%d %s\n", nRead,_pBuf);
    }
    //_flush_Colors = FALSE;

    /////////////////////////////////
    // statistic
    //
    /*
    PRINTF("lookup table loaded:\n");
    PRINTF("    -Point Simplified  :%i\n", g_tree_nnodes(_selLUP(_LUP_SIMPL)));
    PRINTF("    -Point Paper Chart :%i\n", g_tree_nnodes(_selLUP(_LUP_PAPER)));
    PRINTF("    -Line              :%i\n", g_tree_nnodes(_selLUP(_LUP_LINES)));
    PRINTF("    -Area Plain        :%i\n", g_tree_nnodes(_selLUP(_LUP_PLAIN)));
    PRINTF("    -Area Symbolized   :%i\n", g_tree_nnodes(_selLUP(_LUP_SYMBO)));
    PRINTF("\n");
    PRINTF("symbology loaded:\n");
    PRINTF("    -Line              :%i\n", g_tree_nnodes(_selSMB(S52_SMB_LINE)));
    PRINTF("    -Pattern           :%i\n", g_tree_nnodes(_selSMB(S52_SMB_PATT)));
    PRINTF("    -Symbol            :%i\n", g_tree_nnodes(_selSMB(S52_SMB_SYMB)));
    PRINTF("    -Conditional       :%i\n", g_tree_nnodes(_selSMB(S52_SMB_COND)));
    */

    return TRUE;
}

static char      *_getParamVal(S57_geo *geoData, char *str, char *buf, int bsz)
// Symbology Command Word Parameter Value Parser.
// Put in 'buf' one of:
//  1- LUP constant value,
//  2- ENC value,
//  3- LUP default value.
// Return pointer to the next field in the string (delim is ','), NULL to abort
{
    char    *tmp    = buf;
    GString *value  = NULL;
    int      defval = 0;    // default value
    int      len    = 0;

    // debug
    //PRINTF("--> buf:%s str:%s\n", buf, str);

    // parse constant parameter with concatenation operator "'"
    if (str != NULL && *str == APOS){
        str++;
        while (*str != APOS){
            *buf++ = *str++;
        }
        *buf = '\0';
        str++;  // skip "'"
        str++;  // skip ","

        return str;
    }

    //while (*str!=',' && *str!=')' && *str!='\0' /*&& len<bsz*/) {
    while ((','!=*str) && (')'!=*str) && ('\0'!=*str)) {
        *tmp++ = *str++;
        ++len;
    }

    //if (len > bsz)
    //    PRINTF("ERROR: chopping input S52 line !? \n");

    *tmp = '\0';
    str++;        // skip ',' or ')'

    if (len<6)
        return str;

    // chop string if default value present
    if (len > 6 && *(buf+6) == '='){
        *(buf+6) = '\0';
        defval = 1;
    }

    // debug
    //if (0 == g_strncasecmp(buf, "DRVAL1", 6))
    //    PRINTF("DRVAL1 found\n");

    value = S57_getAttVal(geoData, buf);
    if (NULL == value) {
        // debug
        //S57_dumpData(geoData);
        if (defval)
            _getParamVal(geoData, buf+7, buf, bsz-7);    // default value --recursion
        else {
            // PRINTF("NOTE: skipping TEXT no value for attribute:%s\n", buf);
            return NULL;                        // abort
        }
    } else {

        int vallen = strlen(value->str);

        if (vallen >= bsz) {
            vallen =  bsz;
            PRINTF("ERROR: chopping attribut value !? \n");
        }

        // special case ENC return an index
        if (0==strncmp(buf, "NATSUR", S52_PL_NMLN)) {
#ifdef S52_USE_GLIB2
            gchar** attvalL = g_strsplit_set(value->str, ",", 0);  // can't handle UTF-8, check g_strsplit() if needed
            gchar** freeL   = attvalL;
            buf[0]          = '\0';

            while (NULL != *attvalL) {
                int i = S52_atoi(*attvalL);

                if (0<=i && i<N_NATSUR) {
                    //strcpy(buf, natsur[i]);
                    strcat(buf, natsur[i]);
                } else {
                    //strcpy(buf, "FIXME:NATSUR");
                    strcat(buf, "FIXME:NATSUR");
                    PRINTF("WARNING: NATSUR out of bound (%i)\n", i);
                }

                ++attvalL;
            }

            g_strfreev(freeL);
#else
            int i = S52_atoi(value->str);

            if (0<=i && i<N_NATSUR)
                strcpy(buf, natsur[i]);
            else
                strcpy(buf, "FIXME:NATSUR");
#endif

        } else {
            // value from ENC
            if (0 == S52_strncmp(buf, "DRVAL1", 6)) {
                double height = S52_atof(value->str);

                // ajust datum if required
                if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
                    double datum  = S52_MP_get(S52_MAR_DATUM_OFFSET);
                    height += datum;
                }
                g_snprintf(buf, 4, "%4.1f", height);

                return str;
            }

            if (0 == S52_strncmp(buf, "VERCSA", 6) ||
                0 == S52_strncmp(buf, "VERCLR", 6) ||
                0 == S52_strncmp(buf, "VERCCL", 6) ||
                0 == S52_strncmp(buf, "VERCOP", 6) )
            {
                double height = S52_atof(value->str);

                // ajust datum if required
                if (TRUE == S52_MP_get(S52_MAR_FONT_SOUNDG)) {
                    double datum  = S52_MP_get(S52_MAR_DATUM_OFFSET);
                    height -= datum;
                }
                g_snprintf(buf, 4, "%4.1fm ", height);

                return str;
            }

            // default
            //else {
                strncpy(buf, value->str, vallen);
                // juste to be certain
                buf[vallen] = '\0';
            //}

        }


    }

    // debug
    //PRINTF("<-- buf:%s str:%s\n", buf, str);

    return str;
}


static _Text     *_parseTEXT(S57_geo *geoData, char *str)
{
    _Text *text = NULL;
    char buf[MAXL] = {'\0'};   // output string

    text = g_new0(_Text, 1);
    //text = g_try_new0(_Text, 1);
    if (NULL == text)
        g_assert(0);

    str = _getParamVal(geoData, str, &text->hjust, 1);   // HJUST
    str = _getParamVal(geoData, str, &text->vjust, 1);   // VJUST
    str = _getParamVal(geoData, str, &text->space, 1);   // SPACE

    // CHARS
    str         = _getParamVal(geoData, str, buf, 5);
    text->style = buf[0];
    // debug - there is only one weight: medium (default)
    //if ('5' != buf[1]) PRINTF("TEXT WEIGHT DIFF FROM MEDIUM ********************\n");
    text->weight= buf[1];
    text->width = buf[2];
    text->bsize = S52_atoi(buf+3);

    str         = _getParamVal(geoData, str, buf, MAXL);
    text->xoffs = S52_atoi(buf);             // XOFFS
    str         = _getParamVal(geoData, str, buf, MAXL);
    text->yoffs = S52_atoi(buf);             // YOFFS
    str         = _getParamVal(geoData, str, buf, MAXL);
    text->col   = S52_PL_getColor(buf);      // COLOUR
    str         = _getParamVal(geoData, str, buf, MAXL);
    text->dis   = S52_atoi(buf);             // DISPLAY

    return text;
}


//---------
// C M S
//---------

static int        _cms_error(int no, const char *err)
{

    PRINTF("ERROR: color management: %s (%i)", err, no);

    _lcmsError = TRUE;

    return TRUE;
}

static int        _cms_init()
{
    // this is all 'intent' of lcms
    //int intent = INTENT_PERCEPTUAL;             // 0 - blueish
    //int intent = INTENT_RELATIVE_COLORIMETRIC;  // 1 - blueish
    //int intent = INTENT_SATURATION;             // 2 - blueish
    int intent = INTENT_ABSOLUTE_COLORIMETRIC;  // 3 <<<< GOOD !!!

    DWORD dwFlags = 0;
    //DWORD dwFlags = cmsFLAGS_NOTPRECALC;       // OK same result as '0'

    //DWORD dwFlags = cmsFLAGS_NULLTRANSFORM;      // drak blue
    //DWORD dwFlags = cmsFLAGS_HIGHRESPRECALC;   // same as '0'
    //DWORD dwFlags = cmsFLAGS_BLACKPOINTCOMPENSATION;  // from Gimp source code (same result as '0')

    cmsSetErrorHandler(_cms_error);

    _xyzp = cmsCreateXYZProfile();
    //_xyzp = cmsOpenProfileFromFile("/home/sduclos/.color/icc/test2.icm", "r");

    _rgbp = cmsCreate_sRGBProfile();
    //_rgbp = cmsOpenProfileFromFile("/home/sduclos/.color/icc/test-2.2.icm", "r");
    //_rgbp = cmsOpenProfileFromFile("/home/sduclos/.color/icc/test-2.2-D50.icm", "r");
    // more blue than g=2.2
    //_rgbp = cmsOpenProfileFromFile("/home/sduclos/.color/icc/test-1.61-D50.icm", "r");
    // pale blue
    //_rgbp = cmsOpenProfileFromFile("/home/sduclos/.color/icc/test-3.0-D50.icm", "r");
    // same as  cmsCreate_sRGBProfile();
    //_rgbp = cmsOpenProfileFromFile("/home/sduclos/.color/icc/test-sRGB.icm", "r");
    // simple method default sRGB
    //_rgbp = cmsOpenProfileFromFile("/home/sduclos/.color/icc/test-G2.2-sRGB.icm", "r");
    // full default sRGB (give the same as  cmsCreate_sRGBProfile())
    //_rgbp = cmsOpenProfileFromFile("/home/sduclos/.color/icc/test.icm", "r");

    if (TRUE == _lcmsError) {
        PRINTF("ERROR: lcms\n");
        return FALSE;
    }

    // sRGBProfile()
    //_2RGB = cmsCreateTransform(_xyzp, TYPE_XYZ_DBL,
    //                           _rgbp, TYPE_RGB_16,
    //                           //_rgbp, TYPE_RGB_DBL,    // bus error
    //                           intent, dwFlags);

    // ProfileFromFile()
    _2RGB = cmsCreateTransform(
                               _xyzp, TYPE_XYZ_DBL,    // blueish (g=2.2)
    //                           //_xyzp, TYPE_XYZ_16,     // nop (mixed color)
                               //_xyzp, TYPE_XYZ_8,     // XYZ_8 doesn't existe
    //
                               _rgbp, TYPE_RGB_8,      // out RGB in BYTE
    //                           //_rgbp, TYPE_RGB_16,
    //                           //_rgbp, TYPE_RGB_DBL,  // bus error (!)
                               intent, dwFlags);

    return TRUE;
}

//static int        _cms_xyL2rgb(double *xr, double *yg, double *Lb)
static int        _cms_xyL2rgb(S52_Color *c)
{
    //cmsCIExyY xyY    = { *xr, *yg, *Lb };
    cmsCIExyY xyY    = { c->x, c->y, c->L };
    cmsCIEXYZ xyz    = {   0,   0,   0 };
    //WORD      rgb[3] = {   0,   0,   0 }; //unsigned short --16 bits
    BYTE      rgb[3] = {   0,   0,   0 }; //unsigned short --16 bits

    cmsxyY2XYZ(&xyz, &xyY);

    //double Y = *Lb;
    //double X = (*xr / *yg) * Y;
    //double Z = ((1 - *xr - *yg) / *yg) * Y;

    xyz.X /= 100.0;
    xyz.Y /= 100.0;
    xyz.Z /= 100.0;

    cmsDoTransform(_2RGB, &xyz, rgb, 1);

    // from OpenVG specs (for D65)
    // R =  3.240479 X - 1.537150 Y - 0.498535 Z
    // G = -0.969256 X + 1.875992 Y + 0.041556 Z
    // B =  0.055648 X - 0.204043 Y + 1.057311 Z
    //
    // and gamma mapping Ga
    //if x < 0.00304
    //    Ga(x) = 12.92 x
    //else
    //    Ga(x) = 1.0556**1/2.4 - 0.0556
    //
    // then sRGB is
    // sR = Ga(R)
    // sG = Ga(G)
    // sB = Ga(B)

    // from 'lcms' doc --> rgb[] * 255.0 / 65535.0 == 257.0
    //*xr = rgb[0] / 257.0;
    //*yg = rgb[1] / 257.0;
    //*Lb = rgb[2] / 257.0;

    //c->R = rgb[0] / 257.0;
    //c->G = rgb[1] / 257.0;
    //c->B = rgb[2] / 257.0;
    c->R = rgb[0];
    c->G = rgb[1];
    c->B = rgb[2];

    // debug
    //PRINTF("%.2i, %.2i, %.2i \n", rgb[0], rgb[1], rgb[2]);

    return TRUE;
}

static int        _cms_done()
{
    cmsCloseProfile(_rgbp);
    cmsCloseProfile(_xyzp);

    if (_2RGB) cmsDeleteTransform(_2RGB);

    _rgbp = NULL;
    _xyzp = NULL;
    _2RGB = NULL;

    return TRUE;
}

//-------------------------
//
// LIB ENTRY POINT SECTION
//
//-------------------------

// S52raz object (basic PLib C1)
extern const u8 S52raz[];
extern int      S52razLen;
//extern u8     _binary_S52raz_3_2_rle_start[];
//extern int    _binary_S52raz_3_2_rle_end;

int         S52_PL_init()
{
    _initPL();

    _cms_init();
    //if (FALSE == _cms_init())
    //    return FALSE;

    if (_initTbl) {
        _PL pl;

        pl.data  = (gchar*)S52raz;
        pl.sz    = S52razLen;

        pl.cnt   = 0;
        _loadPL(&pl);

        /* experiment to load extra
        pl.data  = (gchar*)S52AuxSymb;
        pl.sz    = strlen(S52AuxSymb);

        pl.cnt   = 0;
        _loadPL(&pl);
        */

        _initTbl = FALSE;
    }

    _loadCondSymb();


    return TRUE;
}

int         S52_PL_load(const char *PLib)
// FIXME: handle error
{
    _PL  pl;

    if (NULL == PLib) {
        PRINTF("WARNING: no PLib name given (NULL)\n");
        return FALSE;
    }


    pl.cnt = 0;

#ifdef S52_USE_GLIB2
    {
        int ret;
        GMappedFile *mf = g_mapped_file_new(PLib, FALSE, NULL);
        if (NULL == mf) {
            PRINTF("ERROR: in openning file (%s)\n", PLib);
            return FALSE;
        }

        ret = g_file_test(PLib, G_FILE_TEST_EXISTS);
        if (TRUE != ret) {
            PRINTF("ERROR: file not found (%s)\n", PLib);
            return FALSE;
        }

        pl.sz   = g_mapped_file_get_length(mf);
        pl.data = g_mapped_file_get_contents(mf);

        PRINTF("NOTE: start loading PLib (%s)\n", PLib);


        _loadPL(&pl);

        //g_mapped_file_free(mf);
        g_mapped_file_unref(mf);
    }
#else
    PRINTF("FIXME: mmap() code missing for glib-1\n");
    //fd = open(PLib, "r");
    //mf = mmap(NULL, 0, PROT_READ, MAP_PRIVATE, fd, 0);
#endif

    return TRUE;
}

int         S52_PL_done()
{
    _flushColors();

    if (NULL != _colTables) g_array_free(_colTables, TRUE);
    //if (NULL != _colTables) g_array_unref(_colTables);
    _colTables = NULL;

    if (NULL != _colref) g_tree_destroy(_colref);
    _colref = NULL;


//#ifndef S52_USE_GLIB2
    // destroy look-up tables
    //g_tree_traverse(_selLUP(_LUP_LINES),  _delLUP, G_IN_ORDER, NULL);
    g_tree_destroy (_selLUP(_LUP_LINES));
    //g_tree_unref(_selLUP(_LUP_LINES));

    //g_tree_traverse(_selLUP(_LUP_PLAIN),  _delLUP, G_IN_ORDER, NULL);
    g_tree_destroy (_selLUP(_LUP_PLAIN));
    //g_tree_unref(_selLUP(_LUP_PLAIN));

    //g_tree_traverse(_selLUP(_LUP_SYMBO),  _delLUP, G_IN_ORDER, NULL);
    g_tree_destroy (_selLUP(_LUP_SYMBO));
    //g_tree_unref(_selLUP(_LUP_SYMBO));

    //g_tree_traverse(_selLUP(_LUP_SIMPL),  _delLUP, G_IN_ORDER, NULL);
    g_tree_destroy (_selLUP(_LUP_SIMPL));
    //g_tree_unref(_selLUP(_LUP_SIMPL));

    //g_tree_traverse(_selLUP(_LUP_PAPER),  _delLUP, G_IN_ORDER, NULL);
    g_tree_destroy (_selLUP(_LUP_PAPER));
    //g_tree_unref(_selLUP(_LUP_PAPER));

    // destroy symbology tables
    //g_tree_traverse(_selSMB(S52_SMB_LINE), _delSMB, G_IN_ORDER, NULL);
    g_tree_destroy (_selSMB(S52_SMB_LINE));
    //g_tree_unref(_selSMB(S52_SMB_LINE));

    //g_tree_traverse(_selSMB(S52_SMB_PATT), _delSMB, G_IN_ORDER, NULL);
    g_tree_destroy (_selSMB(S52_SMB_PATT));
    //g_tree_unref(_selSMB(S52_SMB_PATT));

    //g_tree_traverse(_selSMB(S52_SMB_SYMB), _delSMB, G_IN_ORDER, NULL);
    g_tree_destroy (_selSMB(S52_SMB_SYMB));
    //g_tree_unref(_selSMB(S52_SMB_SYMB));

    // CS doesn't have node to free
    g_tree_destroy(_selSMB(S52_SMB_COND));
//#endif

    {   // set table to NULL
        for (int i=0; i<TBL_NUM; ++i)
            _table[i] = NULL;
    }

    // flush plibID
    while (NULL != _plibID) {
        _LBID *tmpID = _plibID->next;
        g_string_free(_plibID->ID, TRUE);
        g_free(_plibID);
        _plibID = tmpID;
    }

    _cms_done();

    return TRUE;
}

S52_Color  *S52_PL_getColor(const char *colorName)
// FIXME: should save index
{
    if (NULL == _colTables) {
        PRINTF("ERROR: PL not initialized .. exiting\n");
        g_assert(0);
    }

    return_if_null(colorName);

    //S52_Color *c   = (S52_Color*) g_tree_lookup(_colref, (gpointer*)colorName);
    gpointer idx = g_tree_lookup(_colref, (gpointer*)colorName);
    if (NULL != idx) {
        //c = &g_array_index(ct->colors, S52_Color, c->cidx);
        //c = &g_array_index(ct->colors, S52_Color, i-1);
        //return c;

        // IHO color index start at 1
        //int i = GPOINTER_TO_INT(idx);
        guchar i = GPOINTER_TO_INT(idx);

        // libS52 color index start at 0
        return S52_PL_getColorAt(i-1);
    }

    // failsafe color: 'no data'
    //S52_Color *c = &g_array_index(ct->colors, S52_Color, 0);
    PRINTF("ERROR: no color name: %s\n", colorName);
    g_assert(0);

    return NULL;

}

S52_Color  *S52_PL_getColorAt(guchar index)
{
    if (NULL == _colTables) {
        PRINTF("ERROR: PL not initialized .. exiting\n");
        g_assert(0);
    }

    if (index > S52_COL_NUM-1) {
        PRINTF("ERROR: color index out of bound\n");
        g_assert(0);
        index = 0; // NODTA
    }

    // getPalette()
    unsigned int n = (unsigned int) S52_MP_get(S52_MAR_COLOR_PALETTE);

    // this has allready taken care off
    // but left for safety
    // FIXME: delete this code
    if (n > _colTables->len-1) {
        PRINTF("ERROR: S52_MAR_COLOR_PALETTE out of range\n");
        g_assert(0);

        n = 0; // failsafe --select DAY_BRIGHT
    }

    _colTable *ct = &g_array_index(_colTables, _colTable, n);
    if (NULL == ct) {
        PRINTF("ERROR: no COLOR_PALETTE (NULL == ct) \n");
        g_assert(0);
    }

    S52_Color *c = &g_array_index(ct->colors, S52_Color, index);

    return c;
}

static int        _linkLUP(_S52_obj *obj, int alt)
// get the LUP that is approproate for this S57 object
// if alt is 1 compute alternate rasterization rules
{
    _LUP       *LUPlist = NULL;
    GTree      *tbl     = NULL;
    _LUPtnm     tblNm   = _LUP_NUM;
    const char *objName = S57_getName(obj->geoData);

    // find proper LUP table for this type of S57 object
    switch (S57_getObjtype(obj->geoData)) {
        case POINT_T: tblNm = (0==alt) ? _LUP_SIMPL : _LUP_PAPER; break;
        case LINES_T: tblNm = _LUP_LINES;                         break;
        case AREAS_T: tblNm = (0==alt) ? _LUP_SYMBO : _LUP_PLAIN; break;
        case _META_T: tblNm = _LUP_NONAM;                         break;
        default     : PRINTF("ERROR: unkown geometry!\n");
                      g_assert(0);
    }

    // get BBtree for this table
    tbl = _selLUP(tblNm);
    if (NULL == tbl) {
        //PRINTF("NOTE: no LUP for object %s (nothing to display)\n", objName);
        return FALSE;
    }

    // scan LUP for objectName
    //PRINTF("-----SEARCHING: %s-----\n", objectName);

    // get list of LUP for S57 object of this class
    LUPlist = (_LUP*)g_tree_lookup(tbl, (gpointer*)objName);
    if (NULL != LUPlist) {
        obj->LUP = _lookUpLUP(LUPlist, obj->geoData);
    } else {
        PRINTF("WARNING: defaulting to QUESMRK1, no LUP found for object name: %s\n", objName);

        obj->LUP = (_LUP*)g_tree_lookup(tbl, (gpointer*)DEFOBJ);
        if (NULL == obj->LUP) {
            PRINTF("ERROR: no PLIB program EXIT! [%s]\n", objName);
            exit(0);
        }

        obj->LUP->DPRI = S52_PRIO_HAZRDS;
    }

    // get tokenized instruction list
    obj->cmdLorig[alt] = _parseINST(obj->LUP->INST);


    // also hold parameter for CS command found in CSinst
    //LUP->cmdList = _LUP2cmd(LUP, geoData);
    //LUP->cmdList = _parseINST(LUP->INST, geoData);


    // priority overide
    /* FIXME: change DISP for this object rather then this LUP
     // OVERRIDE PRIORITY (not in S52 specs.)
     else CMDWRD(OP, S52_CMD_OVR_PR)
     {
     char *c = cmd->param;

     if (S52_PRIO_NOPRIO != *c) LUP->DPRI = *c - '0';    // Display Priority
     c++;
     if (S52_PRIO_NOPRIO != *c) LUP->RPRI = *c;          // 'O' or 'S', Radar Priority
     c++;
     if (S52_PRIO_NOPRIO != *c) LUP->DISC = *c;          // Display Categorie: B/S/O, Base, Standard, Other
     c++;
     if (S52_PRIO_NOPRIO != *c) sscanf(c, "%d",&LUP->LUCM);     // Look-Up Comment (PLib3.x put 'groupes' here,
     //if (S52_PRIO_NOPRIO != *c) sscanf(c, "%i",&LUP->LUCM);     // Look-Up Comment (PLib3.x put 'groupes' here,

     }
     SCANFWRD
     */


    return TRUE;
}

S52_obj    *S52_PL_newObj(S57_geo *geoData)
// wrap an S52 object around an S57 object
{
    return_if_null(geoData);

    if (_initTbl) {
        PRINTF("ERROR: PL not loaded  .. do S52_PL_init() first\n");
        g_assert(0);
    }

    S52_obj *obj  = g_new0(S52_obj, 1);
    //S52_obj *obj  = g_try_new0(S52_obj, 1);
    if (NULL == obj)
        g_assert(0);

    obj->cmdAfinal[0] = g_array_new(FALSE, FALSE, sizeof(_cmdWL));
    obj->cmdAfinal[1] = g_array_new(FALSE, FALSE, sizeof(_cmdWL));
    obj->crntA        = NULL; //obj->cmdAlt[0];  // for safety, point to something
    obj->crntAidx     = 0;

    obj->textParsed[0] = FALSE;
    obj->textParsed[1] = FALSE;

    obj->orient     = (1.0/0.0);   // NaN
    obj->speed      = (1.0/0.0);   // NaN

    obj->geoData    = geoData;


    // -- *TRICKY* -- *TRICKY* -- *TRICKY* --
    // check what S-101 do about this
    // normal LUP and alternate LUP are identical except for the last field
    // this can be read as, for example, DISP can differ for the same object
    // on the alternate LUP - wich make little sens (or does it!)
    //_linkLUP(obj, 0);
    // obj->_LUP will reference the alternate LUP
    // but the field have the same value (theoriticaly)
    _linkLUP(obj, 1);   // alternate symbology
    // FIX: parse alternate first so that normal LUP reference stay
    // the default
    _linkLUP(obj, 0);


    obj->nextLeg = NULL; // this is the default anyway (ie g_new0)
    obj->prevLeg = NULL; // this is the default anyway (ie g_new0)

    return obj;
}

//S52_obj    *S52_PL_delDta(_S52_obj *obj)
S57_geo    *S52_PL_delObj(_S52_obj *obj)
// free data in S52 obj
// return S57 obj geoData
{
    return_if_null(obj);

    /*
    //---------------------------------------------
    // debug
    _cmdWL *top = obj->cmdLorig[0];
    unsigned int ncmd = 0;
    // count normal command
    while (NULL != top) {
        ++ncmd;
        top = top->next;
    }
    // count CS command
    top = obj->CScmdL[0];
    while (NULL != top) {
        ++ncmd;
        top = top->next;
    }
    // total number of command must be the same in the command array
    // but if there is no CS then there is nothing in the command array
    if (ncmd != obj->cmdAfinal[0]->len) {
        PRINTF("ERROR: number of command in 'comnand List' vs 'command Array' mismatch\n");
        g_assert(0);
    }
    //---------------------------------------------
    */


    _freeAllTXT(obj->cmdAfinal[0]);
    _freeAllTXT(obj->cmdAfinal[1]);

    _freeCmdList(obj->cmdLorig[0]);
    _freeCmdList(obj->cmdLorig[1]);
    obj->cmdLorig[0] = NULL;
    obj->cmdLorig[1] = NULL;

    // clear conditional stuff
    _freeCmdList(obj->CScmdL[0]);
    _freeCmdList(obj->CScmdL[1]);
    obj->CScmdL[0] = NULL;
    obj->CScmdL[1] = NULL;

    if (obj->CSinst[0]) g_string_free(obj->CSinst[0], TRUE);
    if (obj->CSinst[1]) g_string_free(obj->CSinst[1], TRUE);
    obj->CSinst[0] = NULL;
    obj->CSinst[1] = NULL;

    if (obj->cmdAfinal[0]) g_array_free(obj->cmdAfinal[0], TRUE);
    if (obj->cmdAfinal[1]) g_array_free(obj->cmdAfinal[1], TRUE);
    //if (obj->cmdAfinal[0]) g_array_unref(obj->cmdAfinal[0]);
    //if (obj->cmdAfinal[1]) g_array_unref(obj->cmdAfinal[1]);
    obj->cmdAfinal[0] = NULL;
    obj->cmdAfinal[1] = NULL;
    obj->crntA        = NULL;
    obj->crntAidx     = 0;

    S57_geo *geo = obj->geoData;
    g_free(obj);

    return geo;
}

S57_geo    *S52_PL_getGeo(_S52_obj *obj)
{
    // OPTIMISATION: seem this is called everywhere, so this test
    // might slow down a bit (must mesure)
    return_if_null(obj);

    return obj->geoData;
}

S57_geo    *S52_PL_setGeo(_S52_obj *obj, S57_geo *geoData)
{
    return_if_null(obj);

    return obj->geoData = geoData;
}

const char *S52_PL_getOBCL(_S52_obj *obj)
// NOTE: geoData.name is the same as LUP.OBCL
// (ie S57_getName(geo))
{
    return_if_null(obj);

    if (NULL == obj->LUP)
        return S57_getName(obj->geoData);
    else
        return obj->LUP->OBCL;
}

//S52_Obj_t   S52_PL_getFTYP(_S52_obj *obj)
S57_Obj_t   S52_PL_getFTYP(_S52_obj *obj)
{
    return_if_null(obj);

    if (NULL == obj->LUP)
        return _META_T;    // special case --noting to display
    else
        return obj->LUP->FTYP;
}

S52_disPrio S52_PL_getDPRI(_S52_obj *obj)
{
    return_if_null(obj);

    // DSID has no LUP --put on nodata layer
    if (NULL == obj->LUP)
        return S52_PRIO_NODATA;

    S52_disPrio dpri = (TRUE == obj->prioOveride) ? obj->DPRI : obj->LUP->DPRI;

#ifdef S52_DEBUG
    if ((0==dpri) && (0!=obj->LUP->INST->len)) {
        PRINTF("DEBUG: rendering object on IHO layer 0 [%s:%s]\n",
               S57_getName(obj->geoData), S52_PL_infoLUP(obj));
    }
#endif

    return dpri;
}

static S52_DisCat _getDISC(_LUP *LUP)
{
    return LUP->DISC;
}

S52_DisCat  S52_PL_getDISC(_S52_obj *obj)
// get DISplay Category
{
    return_if_null(obj);

    if (TRUE == obj->prioOveride)
        return obj->DISC;

    if (NULL == obj->LUP)
        return NO_DISP_CAT;    // we get here on 'DISD'
    else
        return _getDISC(obj->LUP);
}

int         S52_PL_getLUCM(_S52_obj *obj)
{
    return_if_null(obj);

    return obj->LUCM;
}

S52_RadPrio S52_PL_getRPRI(_S52_obj *obj)

{
    if (NULL==obj) {
        return S52_RAD_OVER;
    }

    return obj->RPRI;
}

const char *S52_PL_infoLUP(_S52_obj *obj)
// for debugging
{
    const char *info = "unknown LUP type";

    return_if_null(obj);

    switch (obj->LUP->TNAM) {
        case _LUP_NUM  : info = "unknown LUP (META!)";        break;
        case _LUP_SIMPL: info = "points SIMPLIFIED";          break;
        case _LUP_PAPER: info = "points PAPER_CHART";         break;
        case _LUP_LINES: info = "lines";                      break;
        case _LUP_PLAIN: info = "areas PLAIN_BOUNDARIES";     break;
        case _LUP_SYMBO: info = "areas SYMBOLIZED_BOUNDARIES";break;

        default:
            PRINTF("ERROR: %s\n", info);
    }

    return info;
}

static int        _getAlt(_S52_obj *obj)
// get symbology for POINT_T / AREAS_T from Mariner's Parameter
{
    // Note: obj of type LINE_T have no alternate symbology
    int alt = 0;

    // use alternate point symbol
    if ((POINT_T==S52_PL_getFTYP(obj)) && (FALSE==S52_MP_get(S52_MAR_SYMPLIFIED_PNT)))
        alt = 1;

    // use alternate area symbol
    if ((AREAS_T==S52_PL_getFTYP(obj)) && (FALSE==S52_MP_get(S52_MAR_SYMBOLIZED_BND)))
        alt = 1;

    return alt;
}

const char *S52_PL_getCMDstr(_S52_obj *obj)
// use to get info for cursor pick
{
    return_if_null(obj);

    if ((NULL==obj->LUP) || (NULL==obj->LUP->INST))
        return NULL;

    return obj->LUP->INST->str;
}

S52_CmdWrd  S52_PL_iniCmd(_S52_obj *obj)
// init command list
// return the first commad word of NONE if empty
{
    return_if_null(obj);

    S52_CmdWrd cmdW  = S52_CMD_NONE;

    obj->crntAidx    = 0;
    obj->crntA       = obj->cmdAfinal[_getAlt(obj)];

    if (obj->crntA->len > 0) {
        _cmdWL *cmd = &g_array_index(obj->crntA, _cmdWL, 0);
        cmdW = cmd->cmdWord;
    }

    return cmdW;
}

S52_CmdWrd  S52_PL_getCmdNext(_S52_obj *obj)
// FIX: use a single GArray filled with cmd (normal+CS)
{
    return_if_null(obj);

    obj->crntAidx++;
    if (obj->crntAidx < obj->crntA->len) {
        _cmdWL *cmd = &g_array_index(obj->crntA, _cmdWL, obj->crntAidx);
        // FIXME: can this array call return NULL!
        if (NULL != cmd)
            return cmd->cmdWord;
    }

    return S52_CMD_NONE;
}

_cmdWL           *_getCrntCmd(_S52_obj *obj)
{
    if (NULL == obj->crntA)
        return NULL;

    if (obj->crntAidx >= obj->crntA->len)
        return NULL;

    _cmdWL *cmd = &g_array_index(obj->crntA, _cmdWL, obj->crntAidx);

    return cmd;
}

S52_CmdWrd  S52_PL_getCrntCmd(_S52_obj *obj)
// not used
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if (NULL != cmd)
        return cmd->cmdWord;

    return S52_CMD_NONE;
}

int         S52_PL_cmpCmdParam(_S52_obj *obj, const char *name)
{
    return_if_null(obj);

    // debug: dump command string
    if (NULL == name) {
        PRINTF("DEBUG: %s\n", obj->CSinst[0]->str);
        return FALSE;
    }

    _cmdWL *cmd = _getCrntCmd(obj);
    if (NULL == cmd)
        return FALSE;

    return strncmp(cmd->param, name, S52_SMB_NMLN);
}

const char *S52_PL_getCmdText(_S52_obj *obj)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if ((NULL==cmd) || (NULL==cmd->cmd.def) || (NULL==cmd->cmd.def->exposition.LXPO))
        return NULL;

    return cmd->cmd.def->exposition.LXPO->str;
}

S52_DListData *S52_PL_getDLData(_S52_cmdDef *def)
// TRUE if new (updated)
{
    if (NULL == def)
        return FALSE;

    return &def->DListData;
}

int         S52_PL_getLSdata(_S52_obj *obj, char *pen_w, char *style, S52_Color **color)
// get Line Style data, width in ASCII (pixel=0.32 mm) of current command word
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if (NULL == cmd)
        return FALSE;

    // paranoia
    if (S52_CMD_SIM_LN != cmd->cmdWord) {
        PRINTF("ERROR: S52_CMD_SIM_LN != cmd->cmdWord\n");
        g_assert(0);
    }

    *pen_w = cmd->param[5];
    *style = cmd->param[2];

    // visual check for S52_USE_SUPP_LINE_OVERLAP
    //if (TRUE == obj->highlight)
    if (TRUE == S57_getHighlight(obj->geoData)) {
        *color = S52_PL_getColor("DNGHL");
        S57_highlightOFF(obj->geoData);
    } else
        *color = S52_PL_getColor(cmd->param+7);

    // make sure that line have no transparency set by S52_PL_getACdata()
    //(*color)->trans = '0';

    return TRUE;
}

int         S52_PL_setSYorient(_S52_obj *obj, double orient)
{
    return_if_null(obj);

    // clamp to [0..360[
    while (360.0 < orient)
        orient -= 360.0;

    while (orient < 0.0)
        orient += 360.0;

    if ((0.0<=orient) && (orient<360.0)) {
        obj->orient = orient;
        return TRUE;
    } else
        return FALSE;
}

double      S52_PL_getSYorient(_S52_obj *obj)
// return symbol cmd orientation parameter [0..360[. -1 on error
{
    char   val[MAXL] = {'\0'};   // output string
    char  *str       = NULL;

    return_if_null(obj);

    if (TRUE == isinf(obj->orient)) {
        if (NULL != obj->crntA) {
            //return FALSE;

            //if (obj->crnt>=obj->cmdA->len)
            //    return FALSE;

            _cmdWL *cmd = &g_array_index(obj->crntA, _cmdWL, obj->crntAidx);

            if (NULL != cmd) {
                //return FALSE;

                str = cmd->param;

                // check if ORIENT param in symb cmd (ex SY(AAAA01,ORIENT))
                if (',' == *(str+8)) {
                    str = _getParamVal(obj->geoData, str+9, val, MAXL);

                    if (NULL != str) {
                        //orient = S52_atof(val) - 90.0;
                        obj->orient = S52_atof(val);
                    } else
                        obj->orient = 0.0;
                } else
                    obj->orient = 0.0;
            } else
                obj->orient = 0.0;
        } else
            obj->orient = 0.0;

        // default to upright
        //if isinf(obj->orient)
        //   obj->orient = 0;
    }

    return obj->orient;
}

int         S52_PL_getSYbbox  (_S52_obj *obj, int *width, int *height)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if ((NULL==cmd) || (NULL==cmd->cmd.def))
        return FALSE;

    *width  = cmd->cmd.def->pos.symb.bbox_w.SYHL;
    *height = cmd->cmd.def->pos.symb.bbox_h.SYVL;

    return TRUE;
}

int         S52_PL_setSYspeed(_S52_obj *obj, double speed)
{
    return_if_null(obj);

    obj->speed = speed;

    return TRUE;
}

int         S52_PL_getSYspeed(_S52_obj *obj, double *speed)
{
    return_if_null(obj);

    if (TRUE == isinf(obj->speed)) {
        // FIXME: try to get the speed from att !!
        // NOTE: this is a general holder for speed depending on
        // the object type. So it could be for current, ship, AIS, ...
        return FALSE;
    }

    *speed = obj->speed;

    return TRUE;
}

int         S52_PL_setLCdata(_S52_cmdDef *def, char pen_w)
{
    return_if_null(def);
    /*
    if (NULL == color) {
        // danger high light
        def->col = S52_PL_getColor("DNGHL");
        PRINTF("ERROR: no color .. defaulting to DNGHL\n");
    } else
        def->col = color;
    */

    def->pen_w = pen_w;

    return TRUE;
}

//double      S52_PL_getLCdata(S52_obj *obj, double dotpitch_x, char *pen_w)
int         S52_PL_getLCdata(_S52_obj *obj, double *symlen, char *pen_w)
// compute symbol run lenght in pixel
// set pen_w
{
    *symlen = 0.0;
    *pen_w  = '1';

    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if ((NULL==cmd) || (NULL==cmd->cmd.def))
        return FALSE;

    int bbx = cmd->cmd.def->pos.symb.bbox_x.SBXC;
    int bbw = cmd->cmd.def->pos.symb.bbox_w.SYHL;
    int ppx = cmd->cmd.def->pos.symb.pivot_x.SYCL;

    // check for pivot inside symbol cover (ex: QUESMRK1)
    if (ppx > bbx) {
        //symlen = bbw / (dotpitch_x*100.0);
        *symlen = bbw;
    }
    else
    {
        //symlen = fabs(bbx-ppx+bbw) / (dotpitch_x*100.0);
        int bb = bbx-ppx+bbw;
        if (bb < 0) bb = -bb;
        //symlen = bb / (dotpitch_x*100.0);
        *symlen = bb;
    }

    //PRINTF("ppx:%i bbx:%i bbw:%i \n", ppx, bbx, bbw);
    //*color = S52_PL_getColorAt(cmd->def->col->i);

    *pen_w = cmd->cmd.def->pen_w;
    if (*pen_w < '1' || *pen_w > '9' ) {
        PRINTF("WARNING: out of bound pen width for LC (%c)\n", *pen_w);
        *pen_w = '1';
    }

    //return symlen;
    return TRUE;
}

S52_Color  *S52_PL_getACdata(_S52_obj *obj)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if (NULL == cmd)
        return NULL;

    //S52_Color *color = S52_PL_getColor(cmd->param);
    S52_Color *color = NULL;
    //if (TRUE == obj->highlight)
    if (TRUE == S57_getHighlight(obj->geoData)) {
        color = S52_PL_getColor("DNGHL");
        S57_highlightOFF(obj->geoData);
    } else
        color = S52_PL_getColor(cmd->param);


    // FIXME: this will put this object transparancy to
    // all objects using this color
    if (cmd->param[5] == ',')
        color->trans = cmd->param[6];
    else
        color->trans = '0';

    return color;
}

int         S52_PL_getAPTileDim(_S52_obj *obj, double *w, double *h, double *dx)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if ((NULL==cmd) || (NULL==cmd->cmd.def))
        return FALSE;

    int bbx     = cmd->cmd.def->pos.patt.bbox_x.LBXC;
    int bby     = cmd->cmd.def->pos.patt.bbox_y.LBXR;
    int bbw     = cmd->cmd.def->pos.patt.bbox_w.LIHL;
    int bbh     = cmd->cmd.def->pos.patt.bbox_h.LIVL;
    int pivot_x = cmd->cmd.def->pos.patt.pivot_x.LICL;
    int pivot_y = cmd->cmd.def->pos.patt.pivot_y.LIRW;

    double tw,th;   // tile width/height 1 = 0.01 mm

    // bounding box + pivot (X)
    if (pivot_x < bbx)
        tw = bbx - pivot_x + bbw;
    else {
        if (pivot_x > bbx + bbw)
            tw = pivot_x - bbx;
        else
            tw = bbw;     // pivot inside bounding box
    }
    //PRINTF("pattern-X px:%i bbx:%i bbw:%i \n", pivot_x, bbx, bbw);

    // bounding box + pivot (Y)
    if (pivot_y < bby)
        th = bby - pivot_y + bbh;
    else {
        if (pivot_y > bby + bbh)
            th = pivot_y - bby;
        else
            th = bbh;     // pivot inside bounding box
    }

    *w = cmd->cmd.def->pos.patt.minDist.PAMI;
    *h = cmd->cmd.def->pos.patt.minDist.PAMI;

    *w += tw;
    *h += th;    //PRINTF("patt cover (pixel) tw:%f th:%f \n", tw, th);

    // pattern spacing (STG/LIN ie 'S'/'L')
    // LINEAR: & &   STAGGERED:  & &
    //         & &              & &
    *dx = (cmd->cmd.def->fillType.PATP == 'S')? *w/2.0 : 0.0;

    return TRUE;
}

#if (defined(S52_USE_GL2) || defined(S52_USE_GLES2))
int         S52_PL_getAPTilePos(_S52_obj *obj, double *bbx, double *bby, double *pivot_x, double *pivot_y)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if ((NULL==cmd) || (NULL==cmd->cmd.def))
        return FALSE;

    *bbx     = cmd->cmd.def->pos.patt.bbox_x.LBXC;
    *bby     = cmd->cmd.def->pos.patt.bbox_y.LBXR;
    *pivot_x = cmd->cmd.def->pos.patt.pivot_x.LICL;
    *pivot_y = cmd->cmd.def->pos.patt.pivot_y.LIRW;

    return TRUE;
}

int         S52_PL_setAPtexID(_S52_obj *obj, guint mask_texID)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if ((NULL==cmd) || (NULL==cmd->cmd.def))
        return FALSE;

    cmd->cmd.def->mask_texID = mask_texID;

    return TRUE;
}

guint       S52_PL_getAPtexID(_S52_obj *obj)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if ((NULL==cmd) || (NULL==cmd->cmd.def))
        return FALSE;

    return cmd->cmd.def->mask_texID;
}
#endif  // S52_USE_GL2 | S52_USE_GLES2

#if 0
int         S52_PL_setAPcover(_S52_cmdDef *def, double dotpitch_x, double dotpitch_y, char pen_w)
// compute tile size (cover)
{

    int  bbx      = def->pos.patt.bbox_x.LBXC;
    int  bby      = def->pos.patt.bbox_y.LBXR;
    int  bbw      = def->pos.patt.bbox_w.LIHL;
    int  bbh      = def->pos.patt.bbox_h.LIVL;
    int  pivot_x  = def->pos.patt.pivot_x.LICL;
    int  pivot_y  = def->pos.patt.pivot_y.LIRW;
    double tw,th;   // tile width/height 1 = 0.01 mm

    def->pen_w = pen_w;

    // bounding box + pivot (X)
    if (pivot_x < bbx)
        tw = bbx - pivot_x + bbw;
    else
        if (pivot_x > bbx + bbw)
            tw = pivot_x - bbx;
        else
            tw = bbw;     // pivot inside bounding box
    //PRINTF("pattern-X px:%i bbx:%i bbw:%i \n", pivot_x, bbx, bbw);

    // bounding box + pivot (Y)
    if (pivot_y < bby)
        th = bby - pivot_y + bbh;
    else
        if (pivot_y > bby + bbh)
            th = pivot_y - bby;
        else
            th = bbh;     // pivot inside bounding box
    //PRINTF("pattern-Y py:%i bby:%i bbh:%i \n", pivot_y, bby, bbh);
    //PRINTF("pattern cover tw:%f th:%f \n", tw, th);

    // NOTE: cover size is made of pivot postion + bounding box in pixel.
    // Add pen_width since line thickness is not part of bbw/bbh
    // (ie 2 row; 1 top / 1 bottom and 2 column 1 left / 1 right
    // Add 2 row and 2 column because of rounding error.
    // BUG: exercise with AP(TSSJCT02) --check pen_w
    def->cover_w = tw;
    def->cover_h = th;
    //PRINTF("pattern cover (pixel) tw:%i th:%i \n", def->cover_w, def->cover_h);

    // not sure if this is true all the time
    // hence asserted
    //g_assert(0 < def->cover_w);
    //g_assert(0 < def->cover_h);

    return 1;
}
#endif

#if 0
int         S52_PL_getSymOff(_S52_vec *vecObj, int patt, int *off_x, int *off_y)
{
    int bbx     = vecObj->bbx;
    int bby     = vecObj->bby;
    int pivot_x = vecObj->pivot_x;
    int pivot_y = vecObj->pivot_y;

    // Offset is different for symbol and pattern.
    if (patt) {
        *off_x = (pivot_x < bbx) ? pivot_x : bbx;
        *off_y = (pivot_y < bby) ? pivot_y : bby;
    } else {
        *off_x = pivot_x;
        *off_y = pivot_y;
    }

    return 1;
}
#endif

gint        S52_PL_traverse(S52_SMBtblName tableNm, GTraverseFunc callBack)
{
    GTree *tbl = _selSMB(tableNm);

    if (NULL != tbl) {
#ifdef S52_USE_GLIB2
        g_tree_foreach(tbl, callBack, NULL);
#else
        // deprecated
        g_tree_traverse(tbl, callBack, G_IN_ORDER, NULL);
#endif
        return TRUE;
    }


    PRINTF("WARNING: should not reach this\n");
    g_assert(0);

    return FALSE;
}

gint        S52_PL_getTableSz(S52_SMBtblName tableNm)
// return number of entree in a symbology table
{
    gint nnodes = 0;
    GTree *tbl = _selSMB(tableNm);

    if (NULL != tbl)
        nnodes =  g_tree_nnodes(tbl);

    return nnodes;
}

S52_DListData *S52_PL_newDListData(_S52_obj *obj)
// _renderAC_LIGHTS05() has no cmd->cmd.def / DList
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if (NULL == cmd) {
        PRINTF("WARNING: internal inconsistency\n");
        g_assert(0);

        return NULL;
    }

    if ((NULL==cmd->cmd.DList) && (S52_CMD_ARE_CO==cmd->cmdWord)) {
        cmd->cmd.DList = g_new0(S52_DListData, 1);
    } else {
        PRINTF("WARNING: internal inconsistency\n");
        g_assert(0);

        return NULL;
    }


    return cmd->cmd.DList;
}

S52_DListData *S52_PL_getDListData(_S52_obj *obj)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if (NULL == cmd) {
        PRINTF("ERROR: no 'cmd' .. exiting\n");
        g_assert(0);
        return NULL;
    }

    // parano
    if ((S52_CMD_COM_LN!=cmd->cmdWord) &&
        (S52_CMD_ARE_PA!=cmd->cmdWord) &&
        (S52_CMD_ARE_CO!=cmd->cmdWord) &&   // _renderAC_LIGHTS05() pass here now
        (S52_CMD_SYM_PT!=cmd->cmdWord))
    {
        PRINTF("ERROR: this type of cmd word has no DL!\n");
        g_assert(0);
    }


    guint      nbr = 0;
    S52_Color *c   = NULL;
    if (S52_CMD_ARE_CO == cmd->cmdWord) {
        // need to init DList for light sector
        if (NULL == cmd->cmd.DList)
            return NULL;

        nbr = cmd->cmd.DList->nbr;
        c   = cmd->cmd.DList->colors;
    } else {
        nbr = cmd->cmd.def->DListData.nbr;
        c   = cmd->cmd.def->DListData.colors;
    }

    //debug - trying to nail a curious bug
    if (MAX_SUBLIST < nbr) {
        PRINTF("ERROR: color index out of bound\n");
        g_assert(0);
    }

    for (guint i=0; i<nbr; ++i) {
            S52_Color *col = S52_PL_getColorAt(c[i].cidx);

            // ugly: pull transparency from symbol color not from PLib
            guchar cidx  = c[i].cidx;
            guchar trans = c[i].trans;
            guchar pen_w = c[i].pen_w;

            // debug --trans is set to a value
            // but ..
            //if (0 == trans)
            //    g_assert(0);

            //if (TRUE == obj->highlight) {
            if (TRUE == S57_getHighlight(obj->geoData)) {
                S57_highlightOFF(obj->geoData);
                S52_Color *colhigh = S52_PL_getColor("DNGHL");
                c[i] = *colhigh;
            } else
                c[i] = *col;    // this will also copy the 'cidx' from the color table

            // FIXME: this is not needed
            c[i].cidx  = cidx;   // in realty there should be the same
            // FIXME: this is comming from symb vector
            c[i].trans = trans;  // overwrite 'trans' with the symbol trans
            c[i].pen_w = pen_w;
    }

    if (S52_CMD_ARE_CO == cmd->cmdWord)
        return  cmd->cmd.DList;
    else
        return &cmd->cmd.def->DListData;
}

S52_vec    *S52_PL_initVOCmd(_S52_cmdDef *def)
{
    if (NULL == def) {
        PRINTF("ERROR: no symbology definition\n");
        return NULL;
    }

    S52_vec *vecObj = g_new0(S52_vec, 1);
    //S52_vec *vecObj = g_try_new0(S52_vec, 1);
    if (NULL == vecObj)
        g_assert(0);

    vecObj->colRef  = def->colRef.LCRF->str;
    vecObj->str     = def->shape.line.vector.LVCT->str;
    vecObj->bbx     = def->pos.line.bbox_x.LBXC;
    vecObj->bby     = def->pos.line.bbox_y.LBXR;
    vecObj->pivot_x = def->pos.line.pivot_x.LICL;
    vecObj->pivot_y = def->pos.line.pivot_y.LIRW;
    vecObj->name    = def->name.LINM;
    vecObj->pm      = PM_END;

    //if (NULL == _vec.vertex)
    //    _vec.vertex = g_array_new(FALSE, FALSE, sizeof(double));
    //if (NULL == _vec.prim)

    // set/reset
    vecObj->prim = S57_initPrim(vecObj->prim);

    // debug
    //if (0 == strcmp(def->name.LINM, "BCNSPP21")) {
    //    PRINTF("FOUND BCNSPP21\n");
    //}

    return vecObj;
}

int         S52_PL_doneVOCmd(_S52_vec *vecObj)
{
    return_if_null(vecObj);

    S57_donePrim(vecObj->prim);

    g_free(vecObj);

    return TRUE;
}


S52_vCmd    S52_PL_getNextVOCmd(_S52_vec *vecObj)
// return S52_VC_* (Vector Command)
{
    return_if_null(vecObj);

    if (NULL==vecObj->str)
        return S52_VC_NONE;

    // debug
    //PRINTF("vecObj: %s\n", vecObj->str);

    while (';'==*vecObj->str && '\0'!=*vecObj->str)
        vecObj->str++;

    while ('\0' != *vecObj->str) {
        // FIXME: try with wchar_t !
        //wchar_t  cw = *(wchar_t*)vecObj->str;
        S52_vCmd c1 = (S52_vCmd) *vecObj->str++;     // first character
        S52_vCmd c2 = (S52_vCmd) *vecObj->str++;     // second character

        // new list
        if (S52_VC_NEW == c1) {
            vecObj->str++;  // skip color idx
            //vecObj->str++;  // skip ';'
            return S52_VC_NEW;
        }

        switch (c2) {
            // Select Width
            case S52_VC_SW:
                vecObj->pen_w =  *vecObj->str++;
                return S52_VC_SW;

            // Pen Up /  Pen Down
            case S52_VC_PU:
            case S52_VC_PD:
                break;

            // CIrcle
            case S52_VC_CI:
                vecObj->radius = strtod(vecObj->str, &vecObj->str);
                return S52_VC_CI;

            // Polygone Mode
            // FIXME: PM1 (sub-poly) not used .. not tested
            case S52_VC_PM:
                // FIXME: save original pen position and status
                vecObj->pm = (_poly_mode) *vecObj->str++;
                return S52_VC_PM;
                //break;

            // parse first char
            case S52_VC__P:
                switch (c1) {
                    // Fill Polygon
                    case S52_VC_FP:
                        S57_endPrim(vecObj->prim); // finish with this prim
                        return S52_VC_FP;

                    case S52_VC_EP: // Edge Polygon --not used
                    case S52_VC_SP: // Color --should note happen now
                    default:
                        PRINTF("ERROR: first vector command unknown (%c)\n", c1);
                        g_assert(0);

                }
                break;


            case S52_VC_SC: // Symbol Call --not used
            case S52_VC_AA: // Arc Angle   --not used
            case S52_VC_ST: // Select Transparency --should not happen now
            default:
                PRINTF("ERROR: second vector command unknown (%c)\n", c2);
                g_assert(0);
        }

        // reset array if its not a sub poly
        if (PM_BEG!=vecObj->pm) {
            if (S52_VC_PD==c2 || S52_VC_PU==c2)
                vecObj->prim = S57_initPrim(vecObj->prim);
        }

        // read coordinate (x,y,x,y,...) into array (xyzxyzxyz...)
        {
            double off_x = vecObj->pivot_x;
            double off_y = vecObj->pivot_y;
            //double off_x = vecObj->bbx;
            //double off_y = vecObj->bby;
            //double off_x = 0.0;
            //double off_y = 0.0;

            // start poly mode --keep last vertex
            if (PM_BEG != vecObj->pm) {
                S57_begPrim(vecObj->prim, 0);
            }

            while (*vecObj->str != ';') {
                struct {vertex_t x,y,z;} pt3 = {0.0, 0.0, 0.0};
                pt3.x = S52_atof(vecObj->str);
                pt3.x -= off_x;

                while (',' != *vecObj->str++)
                    ;  // advance to Y coord, skip ','

                pt3.y = S52_atof(vecObj->str);
                pt3.y -= off_y;

                S57_addPrimVertex(vecObj->prim, (vertex_t *)&pt3);

                // advance past the Y coord.
                while (','!=*vecObj->str && ';'!=*vecObj->str)
                    vecObj->str++;

                // set position for next X coord.
                if ( ',' == *vecObj->str)
                    vecObj->str++;   // skip ','

                // check for end
                if ('\0' != *(vecObj->str+1)) {
                    // case PD..;PD..; --continue to read vector
                    if ('D' == *(vecObj->str+2))
                        vecObj->str += 3;

                    // entrering into polygone mode ..;PM0;
                    //if ('M' == *(vecObj->str+2) &&
                    //    '0' == *(vecObj->str+3))
                    //    vecObj->str += 6;

                }
            }  // while

            // terminate primitive if we're starting a new one
            // and we're not into poly
            // or there is nothing left (ex ..;PD;PD;)
            /*
            if (('\0' != *(vecObj->str+1)) ||
                ('U'  == *(vecObj->str+2) && PM_BEG!=vecObj->pm))
                S57_endPrim(vecObj->prim);
            */
            if ('\0' != *(vecObj->str+1))
                S57_endPrim(vecObj->prim);
            else {
                if ('U' == *(vecObj->str+2) && PM_BEG!=vecObj->pm)
                    S57_endPrim(vecObj->prim);
            }
        }

        vecObj->str++; // skip ';'

        // if still into a poly continue to read for PMx and FP/EP
        if (PM_BEG == vecObj->pm)
            continue;

        // signal ready for drawing
        //return S52_VC_PU;

        // return last command read
        if (S52_VC__P == c2)
            return c1;
        else
            return c2;
    }

    return S52_VC_NONE;
}

GArray     *S52_PL_getVOdata(_S52_vec *vecObj)
{
    return_if_null(vecObj);
    return S57_getPrimVertex(vecObj->prim);
}

S57_prim   *S52_PL_getVOprim(_S52_vec *vecObj)
{
    return_if_null(vecObj);
    return vecObj->prim;
}

char        S52_PL_getVOwidth(_S52_vec *vecObj)
{
    return_if_null(vecObj);
    return vecObj->pen_w;
}

double      S52_PL_getVOradius(_S52_vec *vecObj)
{
    return_if_null(vecObj);
    return vecObj->radius;
}

const char *S52_PL_getVOname(_S52_vec *vecObj)
{
    return_if_null(vecObj);
    return vecObj->name;
}


//-----------------------------
//
// S52 TEXT COMMAND WORD PARSER
//
//-----------------------------

static _Text     *_parseTX(S57_geo *geoData, _cmdWL *cmd)
{
    return_if_null(cmd);

    char buf[MAXL] = {'\0'};   // output string
    char *str = _getParamVal(geoData, cmd->param, buf, MAXL);   // STRING
    if (NULL == str) {
        return NULL;
    }

    _Text *text = _parseTEXT(geoData, str);

#ifdef S52_USE_ANDROID
    // no g_convert() on android
    if (NULL != text)
        text->frmtd = g_string_new(buf);
#else
    if (NULL != text) {
        gchar *gstr = g_convert(buf, -1, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
        text->frmtd = g_string_new(gstr);
        g_free(gstr);
    }
#endif

    return text;
}


static _Text     *_parseTE(S57_geo *geoData, _cmdWL *cmd)
// same as S52_PL_parseTX put parse 'C' format first
{
    char arg[MAXL] = {'\0'};   // ATTRIB list
    char fmt[MAXL] = {'\0'};   // FORMAT
    char buf[MAXL] = {'\0'};   // output string
    char *b        = buf;
    char *parg     = arg;
    char *pf       = fmt;
    _Text *text    = NULL;
    char *str      = NULL;

    if (NULL == cmd)
        return NULL;

    // get FORMAT
    str = _getParamVal(geoData, cmd->param, fmt, MAXL);
    if (NULL == str)
        return NULL;   // abort this command word if mandatory param absent

    // get ATTRIB list
    str = _getParamVal(geoData, str, arg, MAXL);
    if (NULL == str)
        return NULL;   // abort this command word if mandatory param absent

    while (*pf != '\0') {

        // begin a convertion specification
        if (*pf == '%') {
            char val[MAXL] = {'\0'};   // value of arg
            char tmp[MAXL] = {'\0'};   // temporary format string
            char *t = tmp;
            int  cc        = 0;        // 1 == Convertion Character found

            // get value for this attribute
            parg = _getParamVal(geoData, parg, val, MAXL);
            if (NULL == parg)
                return NULL;   // abort

            if (0==strcmp(val, EMPTY_NUMBER_MARKER))
                return NULL;

            *t = *pf;       // stuff the '%'

            // scan for end at convertion character
            do {
                *++t = *++pf;   // fill conver spec

                switch (*pf) {
                    case 'c':
                    case 's': b += SPRINTF(b, tmp, val);           cc = 1; break;
                    case 'f': b += SPRINTF(b, tmp, S52_atof(val)); cc = 1; break;
                    case 'd':
                    case 'i': b += SPRINTF(b, tmp, S52_atoi(val)); cc = 1; break;
                }
            } while (!cc);
            pf++;             // skip conv. char

        } else {
            *b++ = *pf++;
        }
    }

    text = _parseTEXT(geoData, str);

#ifdef S52_USE_ANDROID
    // no g_convert() on android
    if (NULL != text)
        text->frmtd = g_string_new(buf);
#else
    if (NULL != text) {
        gchar *gstr = g_convert(buf, -1, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
        text->frmtd = g_string_new(gstr);
        g_free(gstr);
    }
#endif

    return text;
}

const char *S52_PL_getEX(_S52_obj *obj, S52_Color **col,
                         int *xoffs, int *yoffs, unsigned int *bsize,
                         unsigned int *weight, int *dis)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if (NULL == cmd)
        return NULL;

    if ((S52_CMD_TXT_TX!=cmd->cmdWord) && (S52_CMD_TXT_TE!=cmd->cmdWord)) {
        PRINTF("ERROR: bug, not a text command\n");
        g_assert(0);
    }

    if (FALSE == obj->textParsed[_getAlt(obj)]) {

        if (S52_CMD_TXT_TX == cmd->cmdWord) {

            // FIXME: this should not be needed!
            if (NULL != cmd->cmd.text) {
                _freeTXT(cmd->cmd.text);
                //g_assert(0);
            }

            cmd->cmd.text = _parseTX(obj->geoData, cmd);

            // debug - will return NULL
            //cmd->cmd.text = _parseTX(obj->geoData, NULL);
        }

        if (S52_CMD_TXT_TE == cmd->cmdWord) {

            // FIXME: this should not occur!
            if (NULL != cmd->cmd.text) {
                _freeTXT(cmd->cmd.text);
                //g_assert(0);
            }

            cmd->cmd.text = _parseTE(obj->geoData, cmd);

            // debug - will return NULL
            //cmd->cmd.text = _parseTE(obj->geoData, NULL);
        }
    }

    if (NULL == cmd->cmd.text)
        return NULL;

    *col    = S52_PL_getColorAt(cmd->cmd.text->col->cidx);
    *xoffs  = cmd->cmd.text->xoffs;
    *yoffs  = cmd->cmd.text->yoffs;
    *bsize  = cmd->cmd.text->bsize;
    *weight = cmd->cmd.text->weight - '4';
    *dis    = cmd->cmd.text->dis;

    return cmd->cmd.text->frmtd->str;
}

int         S52_PL_resetParseText(_S52_obj *obj)
{
    return_if_null(obj);

    _freeAllTXT(obj->cmdAfinal[0]);
    _freeAllTXT(obj->cmdAfinal[1]);

    obj->textParsed[0] = FALSE;
    obj->textParsed[1] = FALSE;

    return TRUE;
}

int         S52_PL_setTextParsed(_S52_obj *obj)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if (NULL == cmd)
        return FALSE;

    if ((S52_CMD_TXT_TX!=cmd->cmdWord) && (S52_CMD_TXT_TE!=cmd->cmdWord)) {
        PRINTF("ERROR: bug, not a text command\n");
        g_assert(0);
        return FALSE;
    }

    // flag text as parsed
    obj->textParsed[_getAlt(obj)] = TRUE;

    return TRUE;
}

int         S52_PL_hasText(_S52_obj *obj)
// return TRUE if there is at least one TEXT command word
// NOTE: the text itself could be unvailable yet!
{
    return_if_null(obj);

    // debug
    //S57_geo *geo = S52_PL_getGeo(obj);
    //if (0 == strcmp("LIGHTS", S57_getName(geo))) {
    //    PRINTF("%s\n", S57_getName(geo));
    //}

    S52_CmdWrd cmdWrd = S52_PL_iniCmd(obj);

    // FIXME: flags this inside _draw() instead of searching every list
    while (S52_CMD_NONE != cmdWrd) {
        switch (cmdWrd) {
            case S52_CMD_TXT_TX:
            case S52_CMD_TXT_TE: return TRUE; break;

            default: break;
        }
        cmdWrd = S52_PL_getCmdNext(obj);
    }

    return FALSE;
}

int         S52_PL_hasLC(_S52_obj *obj)
{
    return_if_null(obj);

    S52_CmdWrd cmdWrd = S52_PL_iniCmd(obj);

    while (S52_CMD_NONE != cmdWrd) {
        switch (cmdWrd) {
            case S52_CMD_COM_LN: return TRUE; break;

            default: break;
        }
        cmdWrd = S52_PL_getCmdNext(obj);
    }
    return FALSE;
}

const char *S52_PL_hasCS(_S52_obj *obj)
{
    return_if_null(obj);

    S52_CmdWrd cmdWrd = S52_PL_iniCmd(obj);

    while (S52_CMD_NONE != cmdWrd) {
        switch (cmdWrd) {
            case S52_CMD_CND_SY: break;

            default: break;
        }
        cmdWrd = S52_PL_getCmdNext(obj);
    }

    if (S52_CMD_CND_SY == cmdWrd) {
        _cmdWL *cmd = _getCrntCmd(obj);

        return cmd->param;
    }
    return FALSE;
}

static S52_objSup _toggleObjType(_LUP *LUP)
// toggle an S57 object type, return state
// rules on BASE can't be soppressed
{
    if (DISPLAYBASE == _getDISC(LUP))
        return S52_SUP_ERR;

    if (S52_SUP_ERR == LUP->sup)
        return S52_SUP_ERR;

    if (S52_SUP_OFF == LUP->sup)
        LUP->sup = S52_SUP_ON;
    else
        LUP->sup = S52_SUP_OFF;

    return LUP->sup;
}

static S52_objSup _toggleLUPlist(_LUP *LUPlist)
// toggle all rules that are not in BASE
{
    S52_objSup sup = S52_SUP_ERR;

    while (NULL != LUPlist) {
        S52_objSup ret = _toggleObjType(LUPlist);
        if (S52_SUP_ERR != ret)
            sup = ret;
        LUPlist = LUPlist->OBCLnext;
    }

    return sup;
}

S52_objSup  S52_PL_toggleObjClass(const char *className)
// toggle an S57 object class
{
    S52_objSup sup = S52_SUP_ERR;
    _LUP  *LUPlist = NULL;

    for (int tblType=LUP_PT_SIMPL; tblType<=LUP_AREA_SYM; ++tblType) {
        LUPlist = (_LUP*)g_tree_lookup(_table[tblType], (gpointer*)className);
        if (NULL != LUPlist)
            sup = _toggleLUPlist(LUPlist);
    }

    return sup;
}

S52_objSup  S52_PL_getObjClassState(const char *className)
// get S57 object class supp state (other then DISPLAYBASE)
{
    S52_objSup sup = S52_SUP_ERR;
    _LUP  *LUPlist = NULL;

    for (int tblType=LUP_PT_SIMPL; tblType<=LUP_AREA_SYM; ++tblType) {
        LUPlist = (_LUP*)g_tree_lookup(_table[tblType], (gpointer*)className);
        while (NULL != LUPlist) {
            if ('D' != LUPlist->DISC)
                return LUPlist->sup;
             LUPlist = LUPlist->OBCLnext;
        }
    }

    return sup;
}

S52_objSup  S52_PL_getToggleState(_S52_obj *obj)
{
    if (NULL == obj)
        return S52_SUP_ERR;

    // debug
    //if (0 == g_strcmp0(S52_PL_getOBCL(obj), "M_QUAL")) {
    //    PRINTF("M_QUAL found\n");
    //}

    // META's can't be displayed (ex: C_AGGR)
    if (NULL == obj->LUP)
        return S52_SUP_ON;

    int lupDisp = S52_PL_getDISC(obj);

    // Mariners Objects
    if (MARINERS_STANDARD==lupDisp || MARINERS_OTHER==lupDisp) {
        unsigned int mask = (unsigned int)S52_MP_get(S52_MAR_DISP_LAYER_LAST);

        // off - no rendering of mariners Object
        //if (S52_MAR_DISP_LAYER_LAST_NONE == (int)S52_MP_get(S52_MAR_DISP_LAYER_LAST)) {
        if (S52_MAR_DISP_LAYER_LAST_NONE    & mask) {
            return S52_SUP_ON;
        }

        // MAR Selected choice (override STD & OTHER)
        if (S52_MAR_DISP_LAYER_LAST_SELECT  & mask) {
            if (S52_SUP_ON == obj->LUP->sup)
                return S52_SUP_ON;
            return S52_SUP_OFF;
        }

        // MAR STD + OTHER
        if ((S52_MAR_DISP_LAYER_LAST_STD    & mask) &&
            (S52_MAR_DISP_LAYER_LAST_OTHER  & mask) ) {
            if (MARINERS_STANDARD == lupDisp)
                return S52_SUP_OFF;
            return S52_SUP_ON;
        }

        // MAR STD
        if (S52_MAR_DISP_LAYER_LAST_STD     & mask) {
            if (MARINERS_STANDARD == lupDisp)
                return S52_SUP_OFF;
            return S52_SUP_ON;
        }

        // MAR OTHER
        if (S52_MAR_DISP_LAYER_LAST_OTHER   & mask) {
            if (MARINERS_OTHER == lupDisp)
                return S52_SUP_OFF;
            return S52_SUP_ON;
        }

        // 3.0 and above
        return S52_SUP_OFF;
    }


    // Base - no supression (whatever S52_MAR_DISP_CATEGORY say)
    if (DISPLAYBASE == lupDisp)
        return S52_SUP_OFF;

    // ENC objects
    {
        unsigned int mask = (unsigned int)S52_MP_get(S52_MAR_DISP_CATEGORY);

        // override mariner choice (BASE & STD & OTHER)
        // this is so that if select is switch back to OFF then
        // previous mariner choice is preserve
        //if (FALSE != (S52_MAR_DISP_CATEGORY_SELECT & mask))
        if (0 < (S52_MAR_DISP_CATEGORY_SELECT & mask))
            return obj->LUP->sup;

        // case of BASE only - all other cat. are OFF
        //if (S52_MAR_DISP_CATEGORY_BASE   & (int)S52_MP_get(S52_MAR_DISP_CATEGORY)) {
        if (0 == mask) {
            if (DISPLAYBASE == lupDisp)
                return S52_SUP_OFF;

            return S52_SUP_ON;
        }

        // case of STD + OTHER
        if ((S52_MAR_DISP_CATEGORY_STD    & mask) &&
            (S52_MAR_DISP_CATEGORY_OTHER  & mask) ) {
                if ((STANDARD==lupDisp) || (OTHER==lupDisp))
                    return S52_SUP_OFF;

            return S52_SUP_ON;
        }

        // case of STD only
        if (S52_MAR_DISP_CATEGORY_STD    & mask) {
            if (STANDARD == lupDisp)
                return S52_SUP_OFF;

            return S52_SUP_ON;
        }

        // case of OTHER only
        if (S52_MAR_DISP_CATEGORY_OTHER  & mask) {
            if (OTHER == lupDisp)
                return S52_SUP_OFF;

            return S52_SUP_ON;
        }
    }

    // NOTE: this is the standard display with object added (from 'other'
    // category) or removed (from 'standard') but not from the base display.
    return S52_SUP_ERR;
}

int         S52_PL_getOffset(_S52_obj *obj, double *offset_x, double *offset_y)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if ((NULL==cmd) || (NULL==cmd->cmd.def))
        return FALSE;

    //int bbw = cmd->def->pos.symb.bbox_w.SYHL;
    //int bbh = cmd->def->pos.symb.bbox_h.SYVL;
    //int bbx = cmd->def->pos.symb.bbox_x.SBXC;
    //int bby = cmd->def->pos.symb.bbox_y.SBXR;
    //int ppx = cmd->def->pos.symb.pivot_x.SYCL;
    //int ppy = cmd->def->pos.symb.pivot_y.SYRW;

    int bbw = cmd->cmd.def->pos.symb.bbox_w.SYHL;
    int bbh = cmd->cmd.def->pos.symb.bbox_h.SYVL;
    int bbx = cmd->cmd.def->pos.symb.bbox_x.SBXC;
    int bby = cmd->cmd.def->pos.symb.bbox_y.SBXR;
    int ppx = cmd->cmd.def->pos.symb.pivot_x.SYCL;
    int ppy = cmd->cmd.def->pos.symb.pivot_y.SYRW;

    *offset_x = bbx - ppx + (bbw / 2);
    *offset_y = bby - ppy + (bbh / 2);

    return TRUE;
}

//#ifdef S52_USE_SUPP_LINE_OVERLAP
//int         S52_PL_setNextCN(_S52_obj *objA, S52_obj *objB)
//{
//    objA->nextCN = objB;
//
//    return TRUE;
//}
//#endif

int         S52_PL_setRGB(const char *colorName, unsigned char  R, unsigned char  G, unsigned char  B)
{
    if (NULL == colorName)
        return FALSE;

    //float dummy;
    S52_Color *c = S52_PL_getColor(colorName);

    c->R = R;
    c->G = G;
    c->B = B;

    return TRUE;
}

int         S52_PL_getRGB(const char *colorName, unsigned char *R, unsigned char *G, unsigned char *B)
{
    S52_Color *c = S52_PL_getColor(colorName);

    *R = c->R;
    *G = c->G;
    *B = c->B;

    return TRUE;
}

int         S52_PL_getPalTableSz()
{
    if (NULL == _colTables)
        return 0;

    return _colTables->len;
}

const char* S52_PL_getPalTableNm(unsigned int idx)
{
    if (NULL != _colTables) {
        _colTable *ct  = NULL;
        if (idx > _colTables->len-1) {
            PRINTF("ERROR: unknown colors table\n");
            // failsafe --select active one
            idx = (int) S52_MP_get(S52_MAR_COLOR_PALETTE);;
        }

        ct = &g_array_index(_colTables, _colTable, idx);
        if ((NULL != ct) && (NULL != ct->tableName))
            return ct->tableName->str;
    }

    return NULL;
}

int         S52_PL_setNextLeg(_S52_obj *obj, S52_obj *objNextLeg)
{
    return_if_null(obj);
    return_if_null(objNextLeg);

    obj->nextLeg = objNextLeg;
    objNextLeg->prevLeg = obj;

    return TRUE;
}

S52_obj    *S52_PL_getNextLeg(_S52_obj *obj)
{
    return_if_null(obj);

    return obj->nextLeg;
}

S52_obj    *S52_PL_getPrevLeg(_S52_obj *obj)
{
    return_if_null(obj);

    return obj->prevLeg;
}

S52_obj    *S52_PL_setWholin(_S52_obj *obj)
{
    return_if_null(obj);

    if (NULL != obj->wholin)

    obj->wholin = obj;

    return obj;
}

S52_obj    *S52_PL_getWholin(_S52_obj *obj)
{
    return_if_null(obj);

    return obj->wholin;
}

int         S52_PL_setTimeNow(_S52_obj *obj)
{
    return_if_null(obj);

    g_get_current_time(&obj->time);

    return TRUE;
}

long        S52_PL_getTimeSec(_S52_obj *obj)
{
    return_if_null(obj);

    return obj->time.tv_sec;
}

#ifdef S52_USE_FREETYPE_GL
guint       S52_PL_getFreetypeGL_VBO(_S52_obj *obj, guint *len)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if (NULL == cmd)
        return FALSE;

    // parano
    // Note: S52_CMD_SYM_PT pass here - speed text on leg
    if ((S52_CMD_TXT_TX!=cmd->cmdWord) && (S52_CMD_TXT_TE!=cmd->cmdWord) && (S52_CMD_SYM_PT!=cmd->cmdWord)) {
        PRINTF("DEBUG: logic bug, not a text command [cmdWord:%i]\n", cmd->cmdWord);
        //g_assert(0);
    }

    *len = cmd->len;

    return cmd->vboID;
}

int         S52_PL_setFreetypeGL_VBO(_S52_obj *obj, guint vboID, guint len)
{
    return_if_null(obj);

    _cmdWL *cmd = _getCrntCmd(obj);
    if (NULL == cmd)
        return FALSE;

    // parano
    // Note: S52_CMD_SYM_PT pass here - speed text on leg
    if ((S52_CMD_TXT_TX!=cmd->cmdWord) && (S52_CMD_TXT_TE!=cmd->cmdWord) && (S52_CMD_SYM_PT!=cmd->cmdWord)) {
        PRINTF("DEBUG: logic bug, not a text command\n");
        //g_assert(0);
    }

    cmd->vboID = vboID;
    cmd->len   = len;

    return TRUE;
}
#endif

// test broken !?
#ifdef S52_TEST
int main()
{

   S52_load_Plib();

   //_CIE2RGB();

   S52_lookup(S52_LUP_AREA_PLN, "ACHARE", "CATACH", NULL);
   S52_flush_Plib();

   return TRUE;
}
#endif
