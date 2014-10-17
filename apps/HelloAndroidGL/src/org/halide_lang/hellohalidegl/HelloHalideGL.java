package org.halide_lang.hellohalidegl;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.hardware.Camera;
import android.util.Log;
import android.widget.FrameLayout;
import android.view.SurfaceView;
import android.view.Surface;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.opengl.GLSurfaceView;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.ByteOrder;

class HalideGLView extends GLSurfaceView {
    static {
        System.loadLibrary("android_halide_gl_native");
    }
    private static native void processTextureHalide(int dst, int width, int height);
    private static native void halideContextLost();

    private static final android.opengl.GLES20 gl = new android.opengl.GLES20();

    // If set to true, let Halide render directly to the framebuffer.
    // Otherwise, Halide renders to a texture which we then blit to the
    // screen.
    private boolean halideDirectRender = true;

    HalideGLView(Context context) {
        super(context);
        setEGLContextClientVersion(2);
        setPreserveEGLContextOnPause(true);
        setDebugFlags(DEBUG_CHECK_GL_ERROR);
        setRenderer(new MyRenderer());
    }

    class MyRenderer implements GLSurfaceView.Renderer {
        private int output;
        private int surfaceWidth, surfaceHeight;
        private int program;

        private FloatBuffer quad_vertices;

        final String vs_source =
            "attribute vec2 position;\n" +
            "varying vec2 texpos;\n" +
            "void main(void) {\n" +
            "  gl_Position = vec4(position, 0.0, 1.0);\n" +
            "  texpos = position * 0.5 + 0.5;\n" +
            "}\n";
        final String fs_source =
            "uniform sampler2D tex;\n" +
            "varying highp vec2 texpos;\n" +
            "void main(void) {\n" +
            "  gl_FragColor = texture2D(tex, texpos.xy);\n" +
            "}\n";

        public MyRenderer() {
            final float[] vertices = new float[] {
                -1.0f, -1.0f,
                1.0f, -1.0f,
                -1.0f, 1.0f,
                1.0f, 1.0f,
            };
            quad_vertices =
                ByteBuffer.allocateDirect(4 * vertices.length)
                .order(ByteOrder.nativeOrder())
                .asFloatBuffer();
            quad_vertices.put(vertices);
        }

        /** Compile a single vertex or fragment shader. */
        private int compileShader(int type, String source) {
            int shader = gl.glCreateShader(type);
            gl.glShaderSource(shader, source);
            gl.glCompileShader(shader);
            int[] status = new int[1];
            gl.glGetShaderiv(shader, gl.GL_COMPILE_STATUS, status, 0);
            if (status[0] == 0) {
                String log = gl.glGetShaderInfoLog(shader);
                Log.e(HelloHalideGL.TAG, log);
                throw new RuntimeException("Compiling shader failed");
            }
            return shader;
        }

        /** Compile and link simple vertex and fragment shader for rendering
         * 2D graphics. */
        private void prepareShaders() {
            int vertex_shader = compileShader(gl.GL_VERTEX_SHADER,
                                              vs_source);
            int fragment_shader = compileShader(gl.GL_FRAGMENT_SHADER,
                                                fs_source);

            program = gl.glCreateProgram();
            if (program == 0) {
                throw new RuntimeException("Invalid GLSL program");
            }
            gl.glAttachShader(program, vertex_shader);
            gl.glAttachShader(program, fragment_shader);
            gl.glBindAttribLocation(program, 0, "position");
            gl.glLinkProgram(program);

            int[] status = new int[1];
            gl.glGetProgramiv(program, gl.GL_LINK_STATUS, status, 0);
            if (status[0] == 0) {
                String log = gl.glGetProgramInfoLog(program);
                Log.e(HelloHalideGL.TAG, log);
                throw new RuntimeException("Linking GLSL program failed");
            }
        }

        private int createTexture(int w, int h) {
            int[] id = new int[1];
            gl.glGenTextures(1, id, 0);
            gl.glBindTexture(gl.GL_TEXTURE_2D, id[0]);
            gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_MIN_FILTER, gl.GL_NEAREST);
            gl.glTexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_MAG_FILTER, gl.GL_NEAREST);

            ByteBuffer buf = ByteBuffer.allocate(w * h * 4);
            gl.glTexImage2D(gl.GL_TEXTURE_2D, 0, gl.GL_RGBA, w, h, 0,
                            gl.GL_RGBA, gl.GL_UNSIGNED_BYTE, buf);
            return id[0];
        }

        @Override
        public void onSurfaceCreated(GL10 unused, EGLConfig config) {
            Log.d("Hello", "onSurfaceCreated");
            prepareShaders();
        }

        @Override
        public void onSurfaceChanged(GL10 unused, int w, int h) {
            halideContextLost();
            int[] textures = { output };
            gl.glDeleteTextures(1, textures, 0);
            output = createTexture(w, h);
            surfaceWidth = w;
            surfaceHeight = h;
        }

        @Override
        public void onDrawFrame(GL10 unused) {
            Log.d("Hello", "onDrawFrame");

            if (halideDirectRender) {
                // Call Halide filter; 0 as the texture ID in this case
                // indicates render to framebuffer.
                processTextureHalide(0, surfaceWidth, surfaceHeight);
            } else {
                // Call Halide filter
                processTextureHalide(output, surfaceWidth, surfaceHeight);

                // Draw result to screen
                gl.glViewport(0, 0, surfaceWidth, surfaceHeight);

                gl.glUseProgram(program);

                int positionLoc = gl.glGetAttribLocation(program, "position");
                quad_vertices.position(0);
                gl.glVertexAttribPointer(positionLoc, 2, gl.GL_FLOAT, false, 0, quad_vertices);
                gl.glEnableVertexAttribArray(positionLoc);

                int texLoc = gl.glGetUniformLocation(program, "tex");
                gl.glUniform1i(texLoc, 0);
                gl.glActiveTexture(gl.GL_TEXTURE0);
                gl.glBindTexture(gl.GL_TEXTURE_2D, output);

                gl.glDrawArrays(gl.GL_TRIANGLE_STRIP, 0, 4);

                gl.glDisableVertexAttribArray(positionLoc);
                gl.glBindTexture(gl.GL_TEXTURE_2D, 0);
                gl.glUseProgram(0);
                gl.glDisableVertexAttribArray(0);
            }
        }
    }
}

public class HelloHalideGL extends Activity {
    static final String TAG = "HelloHalideGL";

    private GLSurfaceView view;

    @Override
    public void onCreate(Bundle b) {
        super.onCreate(b);
        view = new HalideGLView(this);
        setContentView(view);
    }

    @Override
    public void onResume() {
        super.onResume();
        view.onResume();
    }

    @Override
    public void onPause() {
        super.onPause();
        view.onPause();
    }
}
