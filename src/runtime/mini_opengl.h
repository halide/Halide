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
#define GL_VIEWPORT 0x0BA2
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
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_ACTIVE_UNIFORMS 0x8B86

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
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895

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
#define GL_IMPLEMENTATION_COLOR_READ_FORMAT 0x8B9B
#define GL_IMPLEMENTATION_COLOR_READ_TYPE 0x8B9A
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define GL_MAX_VERTEX_ATTRIBS             0x8869
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED    0x8622

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
typedef void (*PFNGLUNIFORM1FPROC) (GLuint location, GLfloat value);
typedef void (*PFNGLUNIFORM1IPROC) (GLuint location, GLint value);
typedef void (*PFNGLUNIFORM1IVPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (*PFNGLUNIFORM2IVPROC) (GLint location, GLsizei count, const GLint *value);
typedef void (*PFNGLUNIFORM1FVPROC) (GLint location, GLsizei count, const GLfloat *value);
typedef void (*PFNGLUSEPROGRAMPROC) (GLuint program);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
typedef void (*PFNGLGETINTEGERV)(GLenum pname, GLint *data);
typedef void (*PFNGLGETBOOLEANV)(GLenum pname, GLboolean *data);
typedef void (*PFNGLFINISHPROC) (void);
typedef void (*PFNGLGETVERTEXATTRIBIVPROC) (GLuint index, GLenum pname, GLint *params);



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
#define GL_VERTEX_ARRAY_BINDING 0x85B5

// GL_ARB_framebuffer_object
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_BINDING 0x8CA6

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

// ---------- OpenGL ES 3.1 ----------

#define GL_TEXTURE_BUFFER_EXT 0x8c2a

#define GL_COMPUTE_SHADER     0x91B9
#define GL_DYNAMIC_COPY       0x88ea

#define GL_READ_ONLY          0x88B8
#define GL_WRITE_ONLY         0x88B9

#define GL_MAP_READ_BIT       0x0001

#define GL_SHADER_STORAGE_BUFFER 0x90D2

#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT 0x00000001
#define GL_BUFFER_UPDATE_BARRIER_BIT      0x00000200
#define GL_ALL_BARRIER_BITS               0xFFFFFFFF

typedef unsigned int  GLbitfield;
typedef ptrdiff_t GLintptr;

typedef void (*PFNGLTEXBUFFEREXTPROC) (GLenum target, GLenum internalformat, GLuint buffer);
typedef void (*PFNGLBINDIMAGETEXTUREPROC) (GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format);
typedef void (*PFNGLMEMORYBARRIERPROC) (GLbitfield barriers);
typedef void *(*PFNGLMAPBUFFERRANGEPROC) (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
typedef void (*PFNGLDISPATCHCOMPUTEPROC) (GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
typedef void (*PFNGLUNMAPBUFFERPROC) (GLenum target);
typedef void (*PFNGLBINDBUFFERBASEPROC) (GLenum target, GLuint index, GLuint buffer);
typedef void (*PFNGLDELETEBUFFERSPROC) (GLsizei n, const GLuint* buffers);

typedef void (*PFNGLGETACTIVEUNIFORM)(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
typedef GLint (*PFNGLGETUNIFORMLOCATION)(GLuint program, const GLchar *name);

#endif  // MINI_OPENGL_H



