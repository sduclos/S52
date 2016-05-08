// _radar.i: test radar code path
//
// SD 2016APR28 - Vitaly code


#ifdef S52_USE_RADAR
#define RADARLOG PATH "/radar/RADAR_imitator/radarlog"

// Description of management structures and radar images
typedef struct {
    unsigned int RAIN      : 1;
    unsigned int FRUIT     : 1;
    unsigned int SCALE     : 4;
    unsigned int MODE_2    : 1;
    unsigned int _Status    : 1;     // collide with symbole 'Status' !
    unsigned int reserved1 : 8;
    unsigned int reserved2 : 8;
    unsigned int reserved3 : 8;
} PSO_APMode;

typedef struct {
    unsigned int    dwHeader;
    PSO_APMode      mrAPMode;
    unsigned short  Td;
    unsigned short  IPCHG;
    unsigned short  iStringsCount;
    unsigned short  iStringLength;
    unsigned char   reserved[4];
    unsigned short  iCurrentString; // number of line [0..2047]
    unsigned char   image[1280];    // image line
} PSO_ImageDGram;

#define ANGLEmax 2048
#define Rmax     1280
static FILE *_radarlog_fd = NULL;

typedef struct {
    double x;
    double y;
} POINT;

static guchar _RADARtex[Rmax*2][Rmax*2];  // Alpha
//static guchar _RADARtex[Rmax*2][Rmax*2][4];  // RGBA
static POINT  _Polar_Matrix_Of_Coords[ANGLEmax][Rmax];

/*
static guchar  *_s52_radar_cb1(double *cLat, double *cLng, double *rNM)
{
    (void)cLat;
    (void)cLng;
    (void)rNM;

    //g_print("_radar_cb()\n");

    return NULL;
}
*/

static guchar  *_s52_radar_cb1  (double *cLat, double *cLng, double *rNM)
{
    //*cLat = _engine.state.cLat + 0.01;
    //*cLng = _engine.state.cLon - 0.01;

    // Cap Sante
    *cLat = 46.65;
    *cLng = -71.7;

    //*rNM = 12.0;  // rNM
    *rNM = 3.0;  // rNM
    //*rNM = 1.5;  // rNM

    return (unsigned char *)_RADARtex;
    //return (unsigned char *)NULL;
}

/*
static guchar  *_s52_radar_cb2  (double *cLat, double *cLng, double *rNM)
{
    *cLat = _engine.state.cLat - 0.01;
    *cLng = _engine.state.cLon - 0.02;

    //*rNM = 12.0;  // rNM
    //*rNM = 3.0;  // rNM
    *rNM = 1.5;  // rNM

    return (unsigned char *)_RADARtex;
    //return (unsigned char *)NULL;
}
*/

static int      _radar_init()
{
    S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 1.0);
    S52_setRADARCallBack(_s52_radar_cb1, Rmax);
    //S52_setRADARCallBack(_s52_radar_cb2, Rmax);

    if (NULL == (_radarlog_fd = fopen(RADARLOG, "rb"))) {
        g_print("s52egl:_initRadar(): can't open file %s\n", RADARLOG);
        g_assert(0);
        return FALSE;
    }

    memset(_Polar_Matrix_Of_Coords, 0, sizeof(_Polar_Matrix_Of_Coords));
    memset(_RADARtex,               0, sizeof(_RADARtex));

    // calculate polar coords
    for (int ANGLE = 0; ANGLE < ANGLEmax; ANGLE++) {
        double ANGLE_RAD = (double)ANGLE/ANGLEmax*2*M_PI;
        double cosinus   = cos(ANGLE_RAD);
        double sinus     = sin(ANGLE_RAD);

        for (int R = 0; R < Rmax; R++) {
            _Polar_Matrix_Of_Coords[ANGLE][R].x = R * cosinus + Rmax;
            _Polar_Matrix_Of_Coords[ANGLE][R].y = R * sinus   + Rmax;
        }
    }

    return TRUE;
}

static int      _radar_writePoint (unsigned char VALUE, int ANGLE, int R)
// Alpha texture,
{
    double x = _Polar_Matrix_Of_Coords[ANGLE][R].x;
    double y = _Polar_Matrix_Of_Coords[ANGLE][R].y;


    //_RADARtex[(int)y][(int)x] = VALUE;  // Alpha
    //_RADARtex[(int)(y+0.5)][(int)(x+0.5)] = VALUE;  // Alpha
    //_RADARtex[(int)y][(int)x][3] = VALUE;  // Alpha

    // debug - rounding x\y+.5 make no diff
    _RADARtex[(int)y][(int)x] = 255 - VALUE;  // Alpha reverse (more conspic)
    //_RADARtex[(int)(y+0.5)][(int)(x+0.5)] = 255 - VALUE;

    return TRUE;
}

static int      _radar_writeString(guchar *string, int ANGLE)
{
    for (int R = 0; R < Rmax; R++)
        _radar_writePoint(string[R], ANGLE, R);

    return TRUE;
}

static int      _radar_readLog(int nLine)
{
    //LOGI("_radar_readLog()\n");

    PSO_ImageDGram img;
    while (nLine--) {
        if (1 == fread(&img, sizeof(PSO_ImageDGram), 1, _fd)) {
            _radar_writeString(img.image, img.iCurrentString);
        } else {
            // return to the top of the file
            rewind(_fd);
            g_print("fread = 0\n");
        }
        if (0 != ferror(_radarlog_fd)) {
            // handle error
            g_print("ferror != 0\n");
            g_assert(0);
            return FALSE;
        }
    }

    return TRUE;
}

static int      _radar_done(void)
{
    // close radarlog
    fclose(_radarlog_fd);

    S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 0.0);

    return TRUE;
}
#endif  // S52_USE_RADAR
