#ifndef MINI_OPENGL_H
#define MINI_OPENGL_H


// ---------- OpenGL core (1.3 and earlier) ----------

typedef char GLchar;
typedef unsigned char GLubyte;
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef ptrdiff_t GLsizeiptr;
typedef float GLfloat;
typedef double GLdouble;
typedef void GLvoid;

#define GL_NO_ERROR 0x0
#define GL_FALSE 0x0
#define GL_TRUE 0x1
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_PACK_ROW_LENGTH 0x0D02
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_RED 0x1903
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_NEAREST 0x2600
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0

typedef void (*PFNGLACTIVETEXTUREPROC) (GLenum texture);
typedef void (*PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void (*PFNGLDISABLEPROC)(GLenum cap);
typedef void (*PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint *textures);
typedef void (*PFNGLDRAWBUFFERSPROC) (GLsizei n, const GLenum *bufs);
typedef void (*PFNGLDRAWELEMENTSPROC)(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices );
typedef void (*PFNGLGENTEXTURESPROC)(GLsizei n, GLuint *textures);
typedef GLenum (*PFNGLGETERRORPROC)(void);
typedef const GLubyte *(*PFNGLGETSTRINGPROC)(GLenum name);
typedef void (*PFNGLGETTEXIMAGEPROC)(GLenum target, GLint level,
                                     GLenum format, GLenum type,
                                     GLvoid *pixels);
typedef void (*PFNGLLOADIDENTITYPROC)(void);
typedef void (*PFNGLMATRIXMODEPROC)(GLenum mode);
typedef void (*PFNGLORTHOPROC)(GLdouble left, GLdouble right,
                               GLdouble bottom, GLdouble top,
                               GLdouble near_val, GLdouble far_val);
typedef void (*PFNGLPIXELSTOREIPROC)(GLenum pname, GLint param);

typedef void (*PFNGLGETTEXLEVELPARAMETERIVPROC)(GLenum target, GLint level,
                                                GLenum pname, GLint *params);
typedef void (*PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level,
                                    GLint internalFormat,
                                    GLsizei width, GLsizei height,
                                    GLint border, GLenum format, GLenum type,
                                    const GLvoid *pixels);
typedef void (*PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void (*PFNGLTEXSUBIMAGE2DPROC)(GLenum target, GLint level,
                                       GLint xoffset, GLint yoffset,
                                       GLsizei width, GLsizei height,
                                       GLenum format, GLenum type,
                                       const GLvoid *data);
typedef void (*PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (*PFNGLREADPIXELS)(GLint x, GLint y,
                                GLsizei width, GLsizei height,
                                GLenum format, GLenum type,
                                GLvoid *pixels);

// ---------- OpenGL 1.5 ----------

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4

typedef void (*PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void (*PFNGLDELETEBUFFERSPROC) (GLsizei n, const GLuint *buffers);
typedef void (*PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (*PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);

// ---------- OpenGL 2.0 ----------

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

typedef void (*PFNGLATTACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (*PFNGLCOMPILESHADERPROC) (GLuint shader);
typedef GLuint (*PFNGLCREATEPROGRAMPROC) (void);
typedef GLuint (*PFNGLCREATESHADERPROC) (GLenum type);
typedef void (*PFNGLDELETEPROGRAMPROC) (GLuint program);
typedef void (*PFNGLDELETESHADERPROC) (GLuint shader);
typedef void (*PFNGLDISABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef GLint (*PFNGLGETATTRIBLOCATIONPROC) (GLuint program, const GLchar *name);
typedef void (*PFNGLGETPROGRAMIVPROC) (GLuint program, GLenum pname, GLint *params);
typedef void (*PFNGLGETPROGRAMINFOLOGPROC) (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (*PFNGLGETSHADERIVPROC) (GLuint shader, GLenum pname, GLint *params);
typedef void (*PFNGLGETSHADERINFOLOGPROC) (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef GLint (*PFNGLGETUNIFORMLOCATIONPROC) (GLuint program, const GLchar *name);
typedef void (*PFNGLLINKPROGRAMPROC) (GLuint program);
typedef void (*PFNGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const GLchar* *string, const GLint *length);
typedef void (*PFNGLUNIFORM1IVPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (*PFNGLUNIFORM2IVPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (*PFNGLUNIFORM1FVPROC) (GLint location, GLsizei count, const GLfloat *value);
typedef void (*PFNGLUSEPROGRAMPROC) (GLuint program);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
typedef void (*PFNGLGETINTEGERV)(GLenum pname, GLint *data);


// ---------- OpenGL 3.0 ----------

#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#define GL_NUM_EXTENSIONS 0x821D
#define GL_RG 0x8227
#define GL_R32F 0x822E
#define GL_RG32F 0x8230
#define GL_RGBA32F 0x8814
#define GL_RGB32F 0x8815
#define GL_LUMINANCE32F 0x8818

// GL_ARB_framebuffer_object
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER 0x8D40

typedef void (*PFNGLBINDFRAMEBUFFERPROC) (GLenum target, GLuint framebuffer);
typedef GLenum (*PFNGLCHECKFRAMEBUFFERSTATUSPROC) (GLenum target);
typedef void (*PFNGLDELETEFRAMEBUFFERSPROC) (GLsizei n, const GLuint *framebuffers);
typedef void (*PFNGLFRAMEBUFFERTEXTURE2DPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (*PFNGLGENFRAMEBUFFERSPROC) (GLsizei n, GLuint *framebuffers);

typedef void (*PFNGLGENVERTEXARRAYS)(GLsizei n, GLuint *arrays);
typedef void (*PFNGLBINDVERTEXARRAY)(GLuint array);
typedef void (*PFNGLDELETEVERTEXARRAYS)(GLsizei n, const GLuint *arrays);
typedef const GLubyte *(*PFNGLGETSTRINGI)(GLenum name, GLuint index);
typedef void (*PFNDRAWBUFFERS)(GLsizei n, const GLenum *bufs);

#endif  // MINI_OPENGL_H
