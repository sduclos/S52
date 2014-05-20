// _GL2.i: definition & declaration for GL2.x
//
// SD 2014MAY20


#ifdef S52_USE_GLES2
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
typedef double GLdouble;
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif


// convert float to double for tess
static GArray *_tessWorkBuf_d  = NULL;
// for converting geo double to VBO float
static GArray *_tessWorkBuf_f  = NULL;

// glsl main
static GLint  _programObject  = 0;
static GLuint _vertexShader   = 0;
static GLuint _fragmentShader = 0;

// glsl uniform
static GLint _uProjection = 0;
static GLint _uModelview  = 0;
static GLint _uColor      = 0;
static GLint _uPointSize  = 0;
static GLint _uSampler2d  = 0;
static GLint _uBlitOn     = 0;
static GLint _uStipOn     = 0;
static GLint _uGlowOn     = 0;

static GLint _uPattOn     = 0;
static GLint _uPattGridX  = 0;
static GLint _uPattGridY  = 0;
static GLint _uPattW      = 0;
static GLint _uPattH      = 0;

// glsl varying
static GLint _aPosition    = 0;
static GLint _aUV          = 0;
static GLint _aAlpha       = 0;

