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

static int _s52_setupMain(void)
{
    // can be called any time
    S52_version();


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

    // load AIS select symb.
    //S52_loadPLib("plib-test-priv.rle");

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

    // read cell location from s52.cfg
    S52_loadCell(NULL, NULL);

    // S-64 ENC
    //S52_loadCell("/home/sduclos/S52/test/ENC_ROOT/GB5X01SE.000", NULL);

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


    S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 1.0);

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
    //S52_setS57ObjClassSupp("M_QUAL", TRUE);  // supress display of the U pattern
    //S52_setS57ObjClassSupp("M_QUAL", FALSE);  // displaythe U pattern
    //S52_setS57ObjClassSupp ("M_QUAL");           //  suppression ON
    //S52_setS57ObjClassSupp("M_QUAL");         //  suppression OFF
    S52_setS57ObjClassSupp("M_QUAL", TRUE);


    S52_loadPLib(PLIB);
    S52_loadPLib(COLS);

    return TRUE;
}

/*
// -------------- s52egl.c -----------------------------------------------------
// load PLib in s52.cfg
    //S52_loadPLib(NULL);

    S52_loadPLib(PLIB);
    S52_loadPLib(COLS);

    // Inland Waterway rasterization rules (form OpenCPN)
    //S52_loadPLib("S52RAZDS.RLE");

    // debug - remove clutter from this symb in SELECT mode
    S52_setS57ObjClassSupp("M_QUAL", TRUE);     // suppress display of the U pattern
    //S52_setS57ObjClassSupp("M_QUAL", FALSE);  // display the U pattern

    S52_setS57ObjClassSupp("M_NSYS", TRUE);     // boundary between IALA-A and IALA-B systems (--A--B--, LC(MARSYS51))

    // DATCOVR/M_COVR:CATCOV=2
    S52_setS57ObjClassSupp("M_COVR", TRUE);     // HO data limit __/__/__ - LC(HODATA01)
    //S52_setS57ObjClassSupp("M_COVR", FALSE);  // default

    // Note: "m_covr" is on BASE, so display can't be suppressed
    //S52_setS57ObjClassSupp("m_covr", TRUE);   // fail

    //S52_setS57ObjClassSupp("sclbdy", TRUE);
    //S52_setS57ObjClassSupp("sclbdy", FALSE);  // default

    _s52_setupMarPar();

    //S52_setTextDisp(0, 100, TRUE);                // show all text (default)

    // if first start, find where we are looking
    _s52_getView(&engine->state);
    // then (re)position the 'camera'
    S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);

    // debug - anti-meridian
    //engine->state.rNM = 2300.0;
    //S52_setView(engine->state.cLat, engine->state.cLon, engine->state.rNM, engine->state.north);

    S52_newCSYMB();

    // must be first mariners' object so that the
    // rendering engine place it on top of OWNSHP/VESSEL
    _s52_setupVRMEBL(engine->state.cLat, engine->state.cLon);

    // set route
    _s52_setupIceRte();

    _s52_setupLEGLIN(engine->state.cLat, engine->state.cLon);

    // wind farme for testing centroids in a concave poly
    _s52_setupPRDARE(engine->state.cLat, engine->state.cLon);

#ifdef USE_FAKE_AIS
    // s52ais.c has it own OWNSHP for debugging (ie. ais target acting like OWNSHP)
    _s52_setupOWNSHP(engine->state.cLat, engine->state.cLon);
    _s52_setupVESSEL(engine->state.cLat, engine->state.cLon);
#endif

#ifdef USE_AIS
    s52ais_initAIS();
#endif

#ifdef S52_USE_EGL
    S52_setEGLCallBack((S52_EGL_cb)_egl_beg, (S52_EGL_cb)_egl_end, engine);
#endif

#ifdef S52_USE_RADAR
    _radar_init();
    S52_setMarinerParam(S52_MAR_DISP_RADAR_LAYER, 1.0);
    S52_setRADARCallBack(_s52_radar_cb1, Rmax);
    S52_setRADARCallBack(_s52_radar_cb2, Rmax);
#endif


// -------------- s52gtk2.c -----------------------------------------------------

// load default cell in s52.cfg
    //S52_loadCell(NULL, _my_S52_loadObject_cb);
    S52_loadCell(NULL, NULL);


    ////////////////////////////////////////////////////////////
    //
    // setup supression of chart object (for debugging)
    //
    // supresse display of adminitrative objects when
    // S52_MAR_DISP_CATEGORY is SELECT, to avoid cluttering
    //S52_setS57ObjClassSupp("M_NSYS");   // boundary between IALA-A and IALA-B systems (--A--B--, LC(MARSYS51))
    //S52_setS57ObjClassSupp("M_COVR");   // HO data limit __/__/__ - LC(HODATA01)
    //S52_setS57ObjClassSupp("M_NPUB");   // ??

    // debug - M_QUAL - the U pattern
    //S52_setS57ObjClassSupp("M_QUAL");
    //ret = S52_setS57ObjClassSupp("M_QUAL");  // OK - ret == TRUE
    //ret = S52_setS57ObjClassSupp ("M_QUAL");  // OK - ret == TRUE
    //ret = S52_setS57ObjClassSupp ("M_QUAL");  // OK - ret == FALSE
    S52_setS57ObjClassSupp("M_QUAL", TRUE);

    // test
    //S52_setS57ObjClassSupp("DRGARE");   // drege area

    // load additional PLib (facultative)
    //S52_loadPLib("plib_pilote.rle");
    //S52_loadPLib("plib-test2.rle");
    // load auxiliary PLib (fix waypnt/WAYPNT01, OWNSHP vector, put cursor on layer 9, ..)
    S52_loadPLib(PLIB);
    // lastest (S52 ed 6.0) IHO colors from www.ecdisregs.com
    S52_loadPLib(COLS);
    // load PLib from s52.cfg indication
    //S52_loadPLib(NULL);

    _s52_setupMarPar();


*/
