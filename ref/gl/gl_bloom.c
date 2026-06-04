#include "gl_local.h"
#include "gl_bloom.h"
#include "cvar.h"

// nanogl (GLES 1.x) doesn't support FBOs
#if defined( XASH_NANOGL )
void GL_BloomInit( void ) {}
void GL_BloomShutdown( void ) {}
void GL_BloomRender( void ) {}
#else

#define BLOOM_FRAMEBUFFER          0x8D40
#define BLOOM_COLOR_ATTACHMENT0    0x8CE0
#define BLOOM_FRAMEBUFFER_COMPLETE 0x8CD5
#define BLOOM_VERTEX_SHADER        0x8B31
#define BLOOM_FRAGMENT_SHADER      0x8B30
#define BLOOM_COMPILE_STATUS       0x8B81
#define BLOOM_LINK_STATUS          0x8B82
#define BLOOM_DOWNSCALE            2

/* keep bloom cvars alive for the lifetime of the renderer */
CVAR_DEFINE( gl_bloom, "gl_bloom", "0", 0, "Enable bloom post-processing" );
CVAR_DEFINE( gl_bloom_threshold, "gl_bloom_threshold", "0.8", 0, "Bloom luminance threshold" );
CVAR_DEFINE( gl_bloom_intensity, "gl_bloom_intensity", "1.0", 0, "Bloom intensity multiplier" );
CVAR_DEFINE( gl_bloom_radius, "gl_bloom_radius", "1.0", 0, "Bloom blur radius" );

static const char *s_vsh =
 "attribute vec2 a_pos;\n"
 "attribute vec2 a_tex;\n"
 "varying vec2 v_tex;\n"
 "void main() {\n"
 " gl_Position = vec4(a_pos, 0.0, 1.0);\n"
 " v_tex = a_tex;\n"
 "}\n";

static const char *s_bright_fsh =
 "precision mediump float;\n"
 "varying vec2 v_tex;\n"
 "uniform sampler2D u_tex;\n"
 "uniform float u_threshold;\n"
 "void main() {\n"
 " vec4 c = texture2D(u_tex, v_tex);\n"
 " float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
 " float b = max(l - u_threshold, 0.0) / max(1.0 - u_threshold, 0.001);\n"
 " gl_FragColor = vec4(c.rgb * b, 1.0);\n"
 "}\n";

static const char *s_blur_fsh =
 "precision mediump float;\n"
 "varying vec2 v_tex;\n"
 "uniform sampler2D u_tex;\n"
 "uniform vec2 u_dir;\n"
 "void main() {\n"
 " vec2 o = u_dir;\n"
 " vec4 c = texture2D(u_tex, v_tex) * 0.227;\n"
 " c += texture2D(u_tex, v_tex + o) * 0.195;\n"
 " c += texture2D(u_tex, v_tex - o) * 0.195;\n"
 " c += texture2D(u_tex, v_tex + 2.0*o) * 0.122;\n"
 " c += texture2D(u_tex, v_tex - 2.0*o) * 0.122;\n"
 " c += texture2D(u_tex, v_tex + 3.0*o) * 0.070;\n"
 " c += texture2D(u_tex, v_tex - 3.0*o) * 0.070;\n"
 " gl_FragColor = c;\n"
 "}\n";

static const char *s_composite_fsh =
 "precision mediump float;\n"
 "varying vec2 v_tex;\n"
 "uniform sampler2D u_scene;\n"
 "uniform sampler2D u_bloom;\n"
 "uniform float u_intensity;\n"
 "void main() {\n"
 " vec4 s = texture2D(u_scene, v_tex);\n"
 " vec3 b = texture2D(u_bloom, v_tex).rgb * u_intensity;\n"
 " gl_FragColor = vec4(s.rgb + b, 1.0);\n"
 "}\n";

typedef GLuint (APIENTRY *pfnCreateShader_t)(GLenum);
typedef void (APIENTRY *pfnShaderSource_t)(GLuint, GLsizei, const char **, const GLint *);
typedef void (APIENTRY *pfnCompileShader_t)(GLuint);
typedef void (APIENTRY *pfnGetShaderiv_t)(GLuint, GLenum, int *);
typedef void (APIENTRY *pfnGetShaderInfoLog_t)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (APIENTRY *pfnCreateProgram_t)(void);
typedef void (APIENTRY *pfnAttachShader_t)(GLuint, GLuint);
typedef void (APIENTRY *pfnLinkProgram_t)(GLuint);
typedef void (APIENTRY *pfnUseProgram_t)(GLuint);
typedef void (APIENTRY *pfnDeleteShader_t)(GLuint);
typedef void (APIENTRY *pfnDeleteProgram_t)(GLuint);
typedef void (APIENTRY *pfnGetProgramiv_t)(GLuint, GLenum, int *);
typedef void (APIENTRY *pfnGetProgramInfoLog_t)(GLuint, GLsizei, GLsizei *, char *);
typedef int (APIENTRY *pfnGetUniformLocation_t)(GLuint, const char *);
typedef void (APIENTRY *pfnUniform1f_t)(int, float);
typedef void (APIENTRY *pfnUniform1i_t)(int, int);
typedef void (APIENTRY *pfnUniform2f_t)(int, float, float);
typedef int (APIENTRY *pfnGetAttribLocation_t)(GLuint, const char *);
typedef void (APIENTRY *pfnVertexAttribPointer_t)(GLuint, int, GLenum, GLboolean, GLsizei, const void *);
typedef void (APIENTRY *pfnEnableVertexAttribArray_t)(GLuint);
typedef void (APIENTRY *pfnDisableVertexAttribArray_t)(GLuint);
typedef void (APIENTRY *pfnDrawArrays_t)(GLenum, int, GLsizei);

static struct {
 qboolean available;
 int width, height;
 int bloom_w, bloom_h;
 GLuint scene_tex;
 GLuint bloom_fbo[2];
 GLuint bloom_tex[2];
 GLuint bright_prog;
 GLuint blur_prog;
 GLuint composite_prog;
 int bright_threshold_loc;
 int bright_tex_loc;
 int bright_pos_loc;
 int bright_texcoord_loc;
 int blur_dir_loc;
 int blur_tex_loc;
 int blur_pos_loc;
 int blur_texcoord_loc;
 int comp_intensity_loc;
 int comp_scene_loc;
 int comp_bloom_loc;
 int comp_pos_loc;
 int comp_texcoord_loc;
 int pos_loc;
 int tex_loc;

 pfnCreateShader_t CreateShader;
 pfnShaderSource_t ShaderSource;
 pfnCompileShader_t CompileShader;
 pfnGetShaderiv_t GetShaderiv;
 pfnGetShaderInfoLog_t GetShaderInfoLog;
 pfnCreateProgram_t CreateProgram;
 pfnAttachShader_t AttachShader;
 pfnLinkProgram_t LinkProgram;
 pfnUseProgram_t UseProgram;
 pfnDeleteShader_t DeleteShader;
 pfnDeleteProgram_t DeleteProgram;
 pfnGetProgramiv_t GetProgramiv;
 pfnGetProgramInfoLog_t GetProgramInfoLog;
 pfnGetUniformLocation_t GetUniformLocation;
 pfnUniform1f_t Uniform1f;
 pfnUniform1i_t Uniform1i;
 pfnUniform2f_t Uniform2f;
 pfnGetAttribLocation_t GetAttribLocation;
 pfnVertexAttribPointer_t VertexAttribPointer;
 pfnEnableVertexAttribArray_t EnableVertexAttribArray;
 pfnDisableVertexAttribArray_t DisableVertexAttribArray;
 pfnDrawArrays_t DrawArrays;
} s_bloom;

static void *resolve_proc( const char *gles_name, const char *arb_name )
{
 void *p = gEngfuncs.GL_GetProcAddress( gles_name );
 if( !p && arb_name )
  p = gEngfuncs.GL_GetProcAddress( arb_name );
 return p;
}

static qboolean resolve_shader_funcs( void )
{
 s_bloom.CreateShader = (pfnCreateShader_t)resolve_proc( "glCreateShader", "glCreateShaderObjectARB" );
 s_bloom.ShaderSource = (pfnShaderSource_t)resolve_proc( "glShaderSource", "glShaderSourceARB" );
 s_bloom.CompileShader = (pfnCompileShader_t)resolve_proc( "glCompileShader", "glCompileShaderARB" );
 s_bloom.GetShaderiv = (pfnGetShaderiv_t)resolve_proc( "glGetShaderiv", "glGetObjectParameterivARB" );
 s_bloom.GetShaderInfoLog = (pfnGetShaderInfoLog_t)resolve_proc( "glGetShaderInfoLog", "glGetInfoLogARB" );
 s_bloom.CreateProgram = (pfnCreateProgram_t)resolve_proc( "glCreateProgram", "glCreateProgramObjectARB" );
 s_bloom.AttachShader = (pfnAttachShader_t)resolve_proc( "glAttachShader", "glAttachObjectARB" );
 s_bloom.LinkProgram = (pfnLinkProgram_t)resolve_proc( "glLinkProgram", "glLinkProgramARB" );
 s_bloom.UseProgram = (pfnUseProgram_t)resolve_proc( "glUseProgram", "glUseProgramObjectARB" );
 s_bloom.DeleteShader = (pfnDeleteShader_t)resolve_proc( "glDeleteShader", "glDeleteObjectARB" );
 s_bloom.DeleteProgram = (pfnDeleteProgram_t)resolve_proc( "glDeleteProgram", "glDeleteObjectARB" );
 s_bloom.GetProgramiv = (pfnGetProgramiv_t)resolve_proc( "glGetProgramiv", "glGetObjectParameterivARB" );
 s_bloom.GetProgramInfoLog = (pfnGetProgramInfoLog_t)resolve_proc( "glGetProgramInfoLog", "glGetInfoLogARB" );
 s_bloom.GetUniformLocation = (pfnGetUniformLocation_t)resolve_proc( "glGetUniformLocation", "glGetUniformLocationARB" );
 s_bloom.Uniform1f  = (pfnUniform1f_t)resolve_proc( "glUniform1f", "glUniform1fARB" );
 s_bloom.Uniform1i  = (pfnUniform1i_t)resolve_proc( "glUniform1i", "glUniform1iARB" );
 s_bloom.Uniform2f   = (pfnUniform2f_t)resolve_proc( "glUniform2f", "glUniform2fARB" );
 s_bloom.GetAttribLocation = (pfnGetAttribLocation_t)resolve_proc( "glGetAttribLocation", "glGetAttribLocationARB" );
 s_bloom.VertexAttribPointer = (pfnVertexAttribPointer_t)resolve_proc( "glVertexAttribPointer", "glVertexAttribPointerARB" );
 s_bloom.EnableVertexAttribArray = (pfnEnableVertexAttribArray_t)resolve_proc( "glEnableVertexAttribArray", "glEnableVertexAttribArrayARB" );
 s_bloom.DisableVertexAttribArray = (pfnDisableVertexAttribArray_t)resolve_proc( "glDisableVertexAttribArray", "glDisableVertexAttribArrayARB" );
 s_bloom.DrawArrays = (pfnDrawArrays_t)resolve_proc( "glDrawArrays", "glDrawArrays" );

 if( !s_bloom.CreateShader || !s_bloom.CompileShader || !s_bloom.CreateProgram ||
  !s_bloom.LinkProgram || !s_bloom.UseProgram || !s_bloom.GetUniformLocation ||
  !s_bloom.Uniform1f || !s_bloom.Uniform1i || !s_bloom.GetAttribLocation ||
  !s_bloom.VertexAttribPointer || !s_bloom.EnableVertexAttribArray || !s_bloom.DrawArrays )
  return false;
 return true;
}

static GLuint compile_shader( GLenum type, const char *source )
{
 GLuint shader;
 int compiled = 0;
 const char *strings[1];
 const GLint lengths[1];

 if( !source || !source[0] )
 {
  gEngfuncs.Con_Printf( S_ERROR "Bloom shader compile: null or empty source for type 0x%04x\n", (unsigned int)type );
  return 0;
 }

 shader = s_bloom.CreateShader( type );
 if( !shader ) return 0;

 strings[0] = source;
 lengths[0] = (GLint)Q_strlen( source );
 s_bloom.ShaderSource( shader, 1, strings, lengths );
 s_bloom.CompileShader( shader );
 s_bloom.GetShaderiv( shader, BLOOM_COMPILE_STATUS, &compiled );

 if( !compiled )
 {
  char log[1024];
  s_bloom.GetShaderInfoLog( shader, sizeof( log ), NULL, log );
  gEngfuncs.Con_Printf( S_ERROR "Bloom shader compile:\n%s\n", log );
  s_bloom.DeleteShader( shader );
  return 0;
 }
 return shader;
}

static GLuint link_program( GLuint vs, GLuint fs )
{
 GLuint prog = s_bloom.CreateProgram();
 if( !prog ) return 0;

 int linked = 0;
 s_bloom.AttachShader( prog, vs );
 s_bloom.AttachShader( prog, fs );
 s_bloom.LinkProgram( prog );
 s_bloom.GetProgramiv( prog, BLOOM_LINK_STATUS, &linked );

 if( !linked )
 {
  char log[1024];
  s_bloom.GetProgramInfoLog( prog, sizeof( log ), NULL, log );
  gEngfuncs.Con_Printf( S_ERROR "Bloom program link:\n%s\n", log );
  s_bloom.DeleteProgram( prog );
  return 0;
 }
 return prog;
}

static void draw_fullscreen_quad( int pos_loc, int tex_loc )
{
 const float verts[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
 const float texs[]  = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };

 s_bloom.VertexAttribPointer( pos_loc, 2, GL_FLOAT, GL_FALSE, 0, verts );
 s_bloom.EnableVertexAttribArray( pos_loc );
 s_bloom.VertexAttribPointer( tex_loc, 2, GL_FLOAT, GL_FALSE, 0, texs );
 s_bloom.EnableVertexAttribArray( tex_loc );
 s_bloom.DrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
 s_bloom.DisableVertexAttribArray( pos_loc );
 s_bloom.DisableVertexAttribArray( tex_loc );
}

void GL_BloomInit( void )
{
 GLuint vs, bright_fs, blur_fs, composite_fs;

 if( s_bloom.available ) return;

 memset( &s_bloom, 0, sizeof( s_bloom ));

 if( !gEngfuncs.pfnGetCvarPointer || !gEngfuncs.pfnGetCvarPointer( "gl_bloom" ))
 {
  gEngfuncs.Cvar_RegisterVariable( &gl_bloom );
  gEngfuncs.Cvar_RegisterVariable( &gl_bloom_threshold );
  gEngfuncs.Cvar_RegisterVariable( &gl_bloom_intensity );
  gEngfuncs.Cvar_RegisterVariable( &gl_bloom_radius );
 }

 if( !resolve_shader_funcs() )
 {
  gEngfuncs.Con_Printf( S_ERROR "Bloom: shader functions not available\n" );
  return;
 }

 s_bloom.width  = gpGlobals->width;
 s_bloom.height = gpGlobals->height;
 s_bloom.bloom_w = Q_max( s_bloom.width  / BLOOM_DOWNSCALE, 1 );
 s_bloom.bloom_h = Q_max( s_bloom.height / BLOOM_DOWNSCALE, 1 );

 // compile shaders
 vs = compile_shader( BLOOM_VERTEX_SHADER, s_vsh );
 bright_fs  = compile_shader( BLOOM_FRAGMENT_SHADER, s_bright_fsh );
 blur_fs    = compile_shader( BLOOM_FRAGMENT_SHADER, s_blur_fsh );
 composite_fs = compile_shader( BLOOM_FRAGMENT_SHADER, s_composite_fsh );

 if( !vs || !bright_fs || !blur_fs || !composite_fs )
 {
  if( vs ) s_bloom.DeleteShader( vs );
  if( bright_fs ) s_bloom.DeleteShader( bright_fs );
  if( blur_fs ) s_bloom.DeleteShader( blur_fs );
  if( composite_fs ) s_bloom.DeleteShader( composite_fs );
  gEngfuncs.Con_Printf( S_ERROR "Bloom: shader compilation failed\n" );
  return;
 }

 s_bloom.bright_prog    = link_program( vs, bright_fs );
 s_bloom.blur_prog      = link_program( vs, blur_fs );
 s_bloom.composite_prog = link_program( vs, composite_fs );

 s_bloom.DeleteShader( vs );
 s_bloom.DeleteShader( bright_fs );
 s_bloom.DeleteShader( blur_fs );
 s_bloom.DeleteShader( composite_fs );

 if( !s_bloom.bright_prog || !s_bloom.blur_prog || !s_bloom.composite_prog )
 {
  gEngfuncs.Con_Printf( S_ERROR "Bloom: program linking failed\n" );
  return;
 }

 // query uniform locations
 s_bloom.UseProgram( s_bloom.bright_prog );
 s_bloom.pos_loc = s_bloom.GetAttribLocation( s_bloom.bright_prog, "a_pos" );
 s_bloom.tex_loc = s_bloom.GetAttribLocation( s_bloom.bright_prog, "a_tex" );
 s_bloom.bright_threshold_loc = s_bloom.GetUniformLocation( s_bloom.bright_prog, "u_threshold" );
 s_bloom.bright_tex_loc       = s_bloom.GetUniformLocation( s_bloom.bright_prog, "u_tex" );
 s_bloom.bright_pos_loc       = s_bloom.pos_loc;
 s_bloom.bright_texcoord_loc  = s_bloom.tex_loc;

 s_bloom.UseProgram( s_bloom.blur_prog );
 s_bloom.blur_pos_loc = s_bloom.GetAttribLocation( s_bloom.blur_prog, "a_pos" );
 s_bloom.blur_texcoord_loc = s_bloom.GetAttribLocation( s_bloom.blur_prog, "a_tex" );
 s_bloom.blur_dir_loc = s_bloom.GetUniformLocation( s_bloom.blur_prog, "u_dir" );
 s_bloom.blur_tex_loc = s_bloom.GetUniformLocation( s_bloom.blur_prog, "u_tex" );

 s_bloom.UseProgram( s_bloom.composite_prog );
 s_bloom.comp_pos_loc = s_bloom.GetAttribLocation( s_bloom.composite_prog, "a_pos" );
 s_bloom.comp_texcoord_loc = s_bloom.GetAttribLocation( s_bloom.composite_prog, "a_tex" );
 s_bloom.comp_intensity_loc = s_bloom.GetUniformLocation( s_bloom.composite_prog, "u_intensity" );
 s_bloom.comp_scene_loc     = s_bloom.GetUniformLocation( s_bloom.composite_prog, "u_scene" );
 s_bloom.comp_bloom_loc     = s_bloom.GetUniformLocation( s_bloom.composite_prog, "u_bloom" );

 s_bloom.UseProgram( 0 );

 // create scene texture
 pglGenTextures( 1, &s_bloom.scene_tex );
 pglBindTexture( GL_TEXTURE_2D, s_bloom.scene_tex );
 pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, s_bloom.width, s_bloom.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
 pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
 pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
 pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
 pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

 // create bloom textures and FBOs
 for( int i = 0; i < 2; i++ )
 {
  pglGenTextures( 1, &s_bloom.bloom_tex[i] );
  pglBindTexture( GL_TEXTURE_2D, s_bloom.bloom_tex[i] );
  pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, s_bloom.bloom_w, s_bloom.bloom_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
  pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
  pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
  pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
  pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

  pglGenFramebuffers( 1, &s_bloom.bloom_fbo[i] );
  pglBindFramebuffer( BLOOM_FRAMEBUFFER, s_bloom.bloom_fbo[i] );
  pglFramebufferTexture2D( BLOOM_FRAMEBUFFER, BLOOM_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_bloom.bloom_tex[i], 0 );

  if( pglCheckFramebufferStatus( BLOOM_FRAMEBUFFER ) != BLOOM_FRAMEBUFFER_COMPLETE )
  {
   gEngfuncs.Con_Printf( S_ERROR "Bloom: FBO %d incomplete\n", i );
   GL_BloomShutdown();
   return;
  }
 }

 pglBindFramebuffer( BLOOM_FRAMEBUFFER, 0 );

 s_bloom.available = true;
 gEngfuncs.Con_Printf( "Bloom initialized (%dx%d -> %dx%d)\n", s_bloom.width, s_bloom.height, s_bloom.bloom_w, s_bloom.bloom_h );
}

void GL_BloomShutdown( void )
{
 if( !s_bloom.available ) return;

 for( int i = 0; i < 2; i++ )
 {
  if( s_bloom.bloom_fbo[i] ) pglDeleteFramebuffers( 1, &s_bloom.bloom_fbo[i] );
  if( s_bloom.bloom_tex[i] ) pglDeleteTextures( 1, &s_bloom.bloom_tex[i] );
 }
 if( s_bloom.scene_tex ) pglDeleteTextures( 1, &s_bloom.scene_tex );
 if( s_bloom.bright_prog ) s_bloom.DeleteProgram( s_bloom.bright_prog );
 if( s_bloom.blur_prog ) s_bloom.DeleteProgram( s_bloom.blur_prog );
 if( s_bloom.composite_prog ) s_bloom.DeleteProgram( s_bloom.composite_prog );

 memset( &s_bloom, 0, sizeof( s_bloom ));
}

void GL_BloomRender( void )
{
 GLboolean depth_enabled, blend_enabled;

 if( !s_bloom.available ) return;
 if( gl_bloom.value <= 0.0f ) return;

 depth_enabled = pglIsEnabled( GL_DEPTH_TEST );
 blend_enabled = pglIsEnabled( GL_BLEND );

 // 1. capture current framebuffer to scene texture
 pglReadBuffer( GL_BACK );
 pglBindTexture( GL_TEXTURE_2D, s_bloom.scene_tex );
 pglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, s_bloom.width, s_bloom.height );

 // 2. bright pass: scene -> bloom_tex[0] (also downscales)
 pglBindFramebuffer( BLOOM_FRAMEBUFFER, s_bloom.bloom_fbo[0] );
 pglViewport( 0, 0, s_bloom.bloom_w, s_bloom.bloom_h );
 s_bloom.UseProgram( s_bloom.bright_prog );
 s_bloom.Uniform1i( s_bloom.bright_tex_loc, 0 );
 s_bloom.Uniform1f( s_bloom.bright_threshold_loc, gl_bloom_threshold.value );
 pglActiveTexture( GL_TEXTURE0_ARB );
 pglBindTexture( GL_TEXTURE_2D, s_bloom.scene_tex );
 draw_fullscreen_quad( s_bloom.bright_pos_loc, s_bloom.bright_texcoord_loc );

 // 3. gaussian blur passes
 float radius = gl_bloom_radius.value;
 if( radius > 0.0f )
 {
  // horizontal: bloom_tex[0] -> bloom_tex[1]
  pglBindFramebuffer( BLOOM_FRAMEBUFFER, s_bloom.bloom_fbo[1] );
  s_bloom.UseProgram( s_bloom.blur_prog );
  s_bloom.Uniform1i( s_bloom.blur_tex_loc, 0 );
  s_bloom.Uniform2f( s_bloom.blur_dir_loc,
   radius / (float)s_bloom.bloom_w, 0.0f );
  pglBindTexture( GL_TEXTURE_2D, s_bloom.bloom_tex[0] );
  draw_fullscreen_quad( s_bloom.blur_pos_loc, s_bloom.blur_texcoord_loc );

  // vertical: bloom_tex[1] -> bloom_tex[0]
  pglBindFramebuffer( BLOOM_FRAMEBUFFER, s_bloom.bloom_fbo[0] );
  s_bloom.Uniform2f( s_bloom.blur_dir_loc,
   0.0f, radius / (float)s_bloom.bloom_h );
  pglBindTexture( GL_TEXTURE_2D, s_bloom.bloom_tex[1] );
  draw_fullscreen_quad( s_bloom.blur_pos_loc, s_bloom.blur_texcoord_loc );
 }

 // 4. composite: scene + bloom_tex[0] onto default framebuffer
 pglBindFramebuffer( BLOOM_FRAMEBUFFER, 0 );
 pglViewport( 0, 0, s_bloom.width, s_bloom.height );
 s_bloom.UseProgram( s_bloom.composite_prog );
 s_bloom.Uniform1i( s_bloom.comp_scene_loc, 0 );
 s_bloom.Uniform1i( s_bloom.comp_bloom_loc, 1 );
 s_bloom.Uniform1f( s_bloom.comp_intensity_loc, gl_bloom_intensity.value );

 pglActiveTexture( GL_TEXTURE0_ARB );
 pglBindTexture( GL_TEXTURE_2D, s_bloom.scene_tex );
 pglActiveTexture( GL_TEXTURE1_ARB );
 pglBindTexture( GL_TEXTURE_2D, s_bloom.bloom_tex[0] );

 pglDisable( GL_DEPTH_TEST );
 pglDisable( GL_BLEND );
 draw_fullscreen_quad( s_bloom.comp_pos_loc, s_bloom.comp_texcoord_loc );

 // restore state
 s_bloom.UseProgram( 0 );
 if( depth_enabled )
  pglEnable( GL_DEPTH_TEST );
 else
  pglDisable( GL_DEPTH_TEST );
 if( blend_enabled )
  pglEnable( GL_BLEND );
 else
  pglDisable( GL_BLEND );
	pglActiveTexture( GL_TEXTURE0_ARB );
}
#endif // !XASH_NANOGL
