// _s52_setupMaqin.i: various common test setup
//
// SD 2016APR29


#ifdef S52_USE_ANDROID
#define PATH     "/sdcard/s52droid"      // Android 4.2
#define PLIB     PATH "/PLAUX_00.DAI"
#define COLS     PATH "/plib_COLS-3.4.rle"
#define GPS      PATH "/bin/sl4agps"
#define AIS      PATH "/bin/s52ais"
#define PID           ".pid"
#define ALLSTOP  PATH "/bin/run_allstop.sh"

//ANDROID_LOG_VERBOSE,
//ANDROID_LOG_DEBUG,
//ANDROID_LOG_INFO,
//ANDROID_LOG_WARN,
//ANDROID_LOG_ERROR,
//ANDROID_LOG_FATAL,

#define  LOG_TAG    "s52droid"
#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define  g_print    g_message

#else   // S52_USE_ANDROID

#define  PATH       "/home/sduclos/dev/gis/data"
#define  PLIB       "PLAUX_00.DAI"
#define  COLS       "plib_COLS-3.4.rle"
#define  LOGI(...)  g_print(__VA_ARGS__)
#define  LOGD(...)  g_print(__VA_ARGS__)
#define  LOGE(...)  g_print(__VA_ARGS__)

#endif  // S52_USE_ANDROID


// -------------- s52gtkegl.c -----------------------------------------------------

#if 0
static int      _my_S52_loadObject_cb(const char *objname,   void *shape)
{
    //
    // .. do something clever with each object of a layer ..
    //

    // this fill the terminal
    //printf("\tOBJECT NAME: %s\n", objname);

    return S52_loadObject(objname, shape);

    //return TRUE;
}
#endif

static int _s52_setupMain(void)
{
    // can be called any time
    LOGI("%s", S52_version());


    // read cell location fron s52.cfg
    //S52_loadCell(NULL, NULL);
    //S52_loadCell("CA479017.000", NULL);
    //S52_loadCell("SCX_CapSante.tif", NULL);
    //S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 1.0);


    //S52_loadCell("/home/vitaly/CHARTS/for_sasha/GB5X01SE.000", NULL);
    //S52_loadCell("/home/vitaly/CHARTS/for_sasha/GB5X01NE.000", NULL);

    // Ice - experimental
    //S52_loadCell("/home/sduclos/dev/gis/data/ice/East_Coast/--0WORLD.shp", NULL);

    // Bathy - experimental
    //S52_loadCell("/home/sduclos/dev/gis/data/bathy/2009_HD_BATHY_TRIALS/46307260_LOD2.merc.tif", NULL);
    //S52_loadCell("/home/sduclos/dev/gis/data/bathy/2009_HD_BATHY_TRIALS/46307250_LOD2.merc.tif", NULL);
    //S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 1.0);

    //S52_loadCell(NULL, _my_S52_loadObject_cb);
    //S52_loadCell(NULL, NULL);

#ifdef S52_USE_ANDROID

    // set GDAL data path
    g_setenv("S57_CSV", "/sdcard/s52droid/gdal_data", 1);

    // read cell location fron s52.cfg
    //S52_loadCell(NULL, NULL);

    // Tadoussac
    //S52_loadCell(PATH "/ENC_ROOT/CA379035.000", NULL);
    // load all 3 S57 charts
    //S52_loadCell(PATH "/ENC_ROOT", NULL);

    // Rimouski
    S52_loadCell(PATH "/ENC_ROOT_RIKI/CA579041.000", NULL);
    // Estuaire du St-Laurent
    //S52_loadCell(PATH "/ENC_ROOT_RIKI/CA279037.000", NULL);

    // Bec
    //S52_loadCell(PATH "/ENC_ROOT/CA579016.000", NULL);

    // Portneuf
    //S52_loadCell(PATH "/ENC_ROOT/CA479017.000", NULL);
    //S52_loadCell(PATH "/bathy/SCX_CapSante.tif", NULL);
    //S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 1.0);

#else  // S52_USE_ANDROID

    // Note:
    // GDAL profile (s57attributes_<profile>.csv, s57objectclasses_<profile>.csv)
    // iw: Inland Waterway profile add object classe OBCL:17000-17065 (and attributes)
    //g_setenv("S57_PROFILE", "iw", 1);
    //g_setenv("S57_PROFILE", "iw2", 1);
    // GDAL debug info ON
    //g_setenv("CPL_DEBUG", "ON", 1);

    //* read cell location from s52.cfg
    if (FALSE == S52_loadCell(NULL, NULL)) {
        LOGI("_s52_setupMain(): loadCell fail\n");
        return FALSE;
    }
    //*/

    // S-64 ENC
    //S52_loadCell("/home/sduclos/S52/test/ENC_ROOT/GB5X01SE.000", NULL);
    //S52_loadCell("/home/sduclos/dev/gis/S57/IHO_S-64/ENC_ROOT/GB4X0000.000", NULL);
    //S52_loadCell("/home/sduclos/dev/gis/S57/IHO_S-64/ENC_ROOT/GB5X01NE.000", NULL);
    //S52_loadCell("/home/sduclos/dev/gis/S57/IHO_S-64/ENC_ROOT/GB5X01NW.000", NULL);
    //S52_loadCell("/home/sduclos/dev/gis/S57/IHO_S-64/ENC_ROOT/GB5X01SE.000", NULL);
    //S52_loadCell("/home/sduclos/dev/gis/S57/IHO_S-64/ENC_ROOT/GB5X01SW.000", NULL);

    // debug anti-meridian
    //S52_loadCell("/home/sduclos/S52/test/ENC_ROOT/US5HA06M/US5HA06M.000", NULL);
    //S52_loadCell("/home/sduclos/S52/test/ENC_ROOT/US1EEZ1M/US1EEZ1M.000", NULL);

    // Rimouski
    //S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA579041.000", NULL);

    // load PLib in s52.cfg
    //S52_loadPLib(NULL);

    // Estuaire du St-Laurent
    //S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA279037.000", NULL);

    //Tadoussac
    //S52_loadCell("/home/sduclos/dev/gis/S57/riki-ais/ENC_ROOT/CA379035.000", NULL);

    // Ice - experimental (HACK: ice symb link to --0WORLD.shp for one shot test)
    //S52_loadCell("/home/sduclos/dev/gis/data/ice/East_Coast/--0WORLD.shp", NULL);

    // ------------ Bathy HD for CA479017.000 ----------------------
    // Bathy - experimental Cap Sante / Portneuf
    //S52_loadCell(PATH "/../S57/CA_QC-TR/ENC_ROOT/CA479017.000", NULL);
    //S52_loadCell(PATH "/../S57/CA_QC-TR/ENC_ROOT/CA479020.000", NULL);

    //S52_loadCell(PATH "/bathy/SCX_CapSante.tif", NULL);  // old test bathy

    // bathy overlapping CA479017.000 and CA479020.000
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4666N7170W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4664N7170W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4662N7170W_5.tiff", NULL);

    // CA479017.000
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4664N7172W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4662N7172W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4664N7174W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4662N7174W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4664N7176W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4662N7176W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4664N7178W_5.tiff", NULL);

    // ------------ Bathy HD for CA579016.000 ----------------------
    // ENC Bec
    //S52_loadCell(PATH "/../S57/CA_QC-TR/ENC_ROOT/CA579016.000", NULL);
    // ENC TRV
    //S52_loadCell(PATH "/../S57/CA_QC-TR/ENC_ROOT/CA479014.000", NULL);


    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4642N7236W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4640N7238W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4642N7238W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4640N7240W_5.tiff", NULL);

    // West of CA479017.000
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4636N7246W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4638N7246W_5.tiff", NULL);

    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4636N7248W_5.tiff", NULL);
    //S52_loadCell(PATH "/bathy/2016_HD_BATHY_QBC-TRV/4638N7248W_5.tiff", NULL);

    //S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 1.0);

    // RADAR - experimental
    //S52_loadCell("/home/sduclos/dev/gis/data/radar/RADAR_imitator/out.raw", NULL);


#endif  // S52_USE_ANDROID


#ifdef S52_USE_WORLD
    // World data
    if (TRUE == S52_loadCell(PATH "/0WORLD/--0WORLD.shp", NULL)) {
        //S52_setMarinerParam(S52_MAR_DISP_WORLD, 0.0);   // default
        S52_setMarinerParam(S52_MAR_DISP_WORLD, 1.0);     // show world
    }
#endif

    // debug - remove clutter from this symb in SELECT mode
    // debug - M_QUAL - the U pattern
    // supresse display of adminitrative objects when
    // S52_MAR_DISP_CATEGORY is SELECT, to avoid cluttering
    S52_setS57ObjClassSupp("M_QUAL", TRUE);  // supress display of the U pattern
    //S52_setS57ObjClassSupp("M_QUAL", FALSE);  // display the U pattern

    // debug
    //ret = S52_setS57ObjClassSupp ("M_QUAL", TRUE);  // OK - ret == TRUE  - first time
    //ret = S52_setS57ObjClassSupp ("M_QUAL", TRUE);  // OK - ret == FALSE - second time

    // test
    //S52_setS57ObjClassSupp("DRGARE");   // drege area

    S52_loadPLib(PLIB);
    S52_loadPLib(COLS);

    // Inland Waterway rasterization rules (form OpenCPN)
    //S52_loadPLib("S52RAZDS.RLE");

    S52_setS57ObjClassSupp("M_NSYS", TRUE);   // boundary between IALA-A and IALA-B systems (--A--B--, LC(MARSYS51))
    S52_setS57ObjClassSupp("M_NPUB", TRUE);   // ??

    // DATCOVR/M_COVR:CATCOV=2
    //S52_setS57ObjClassSupp("M_COVR", TRUE);   // HO data limit __/__/__ - LC(HODATA01)
    //S52_setS57ObjClassSupp("M_COVR", FALSE);  // default

    // Note: "m_covr" is on BASE, so display can't be suppressed
    //S52_setS57ObjClassSupp("m_covr", TRUE);   // fail

    //S52_setS57ObjClassSupp("sclbdy", TRUE);
    //S52_setS57ObjClassSupp("sclbdy", FALSE);  // default


    // load additional PLib (facultative)
    // load AIS select symb.
    //S52_loadPLib("plib-test-priv.rle");

    //S52_loadPLib("plib_pilote.rle");
    //S52_loadPLib("plib-test2-boylat.rle");

    //plib-test1-rle_vect.rle
    //plib-test3-col.rle

    return TRUE;
}
