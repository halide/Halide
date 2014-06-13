package com.example.hellohalide;

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
import android.opengl.GLES20;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.ByteOrder;

public class HelloHalide extends Activity {
    private static final String TAG = "HelloHalide";

    // Link to native Halide code
    static {
        System.loadLibrary("native");
    }
    private static native void processBitmapHalide(Bitmap src, Bitmap dst);
    private static native void processTextureHalide(Bitmap src, Bitmap dst);

    class MyView extends android.view.View {
        public MyView(Context context) {
            super(context);
        }

        protected void onDraw(Canvas canvas) {
            processBitmapHalide(input, bitmap);
            canvas.drawBitmap(bitmap, 0, 0, null);

            // Swap front and backbuffer
            Bitmap tmp = input;
            input = bitmap;
            bitmap = tmp;

            invalidate();
        }

        protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            Log.d("Hello", "onSizeChanged");
            bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            input = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        }

        private Bitmap input, bitmap;
    }

    class MyViewGL extends GLSurfaceView {
        MyViewGL(Context context) {
            super(context);
            setEGLContextClientVersion(2);
            setRenderer(new MyRenderer());
        }

        class MyRenderer implements GLSurfaceView.Renderer {
            private int input, output;
            private int surfaceWidth, surfaceHeight;
            private int program;

            /** Compile and link simple vertex and fragment shader for
             * rendering 2D graphics. */
            private void prepareShaders() {
                int vertex_shader = GLES20.glCreateShader(GLES20.GL_VERTEX_SHADER);
                GLES20.glShaderSource(vertex_shader,
                                      "attribute vec2 position;\n" +
                                      "void main(void) {\n" +
                                      "  gl_Position = vec4(position, 0.0, 1.0);\n" +
                                      "}\n");
                GLES20.glCompileShader(vertex_shader);

                int[] status = new int[] {0};
                GLES20.glGetShaderiv(vertex_shader, GLES20.GL_COMPILE_STATUS, status, 0);
                if (status[0] == 0) {
                    throw new RuntimeException("Compiling vertex shader failed");
                }

                int fragment_shader = GLES20.glCreateShader(GLES20.GL_FRAGMENT_SHADER);
                GLES20.glShaderSource(fragment_shader,
                                      "void main(void) {\n" +
                                      "  gl_FragColor = vec4(0.0, 1.0, 1.0, 1.0);\n" +
                                      "}\n");
                GLES20.glCompileShader(fragment_shader);
                GLES20.glGetShaderiv(vertex_shader, GLES20.GL_COMPILE_STATUS, status, 0);
                if (status[0] == 0) {
                    throw new RuntimeException("Compiling vertex shader failed");
                }

                program = GLES20.glCreateProgram();
                if (program == 0) {
                    throw new RuntimeException("Invalid GLSL program");
                }
                GLES20.glAttachShader(program, vertex_shader);
                GLES20.glAttachShader(program, fragment_shader);
                GLES20.glBindAttribLocation(program, 0, "position");
                GLES20.glLinkProgram(program);

                GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, status, 0);
                if (status[0] == 0) {
                    String log = GLES20.glGetProgramInfoLog(program);
                    Log.e("Hello", log);
                    throw new RuntimeException("Linking GLSL program failed");
                }
            }

            public void onSurfaceCreated(GL10 gl, EGLConfig config) {
                prepareShaders();
            }

            public void onSurfaceChanged(GL10 gl, int w, int h) {
                Log.d("Hello", "Allocating textures\n");

                int[] textures = { input, output};
                GLES20.glDeleteTextures(2, textures, 0);
                GLES20.glGenTextures(2, textures, 0);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textures[0]);
                ByteBuffer buf = ByteBuffer.allocate(w * h * 4);
                GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, w, h, 0,
                                GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, buf);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textures[1]);
                buf.rewind();
                GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, w, h, 0,
                                GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, buf);
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0);

                input = textures[0];
                output = textures[1];

                surfaceWidth = w;
                surfaceHeight = h;
            }

            public void onDrawFrame(GL10 gl) {
                Log.d("Hello", "onDrawFrame");

                GLES20.glViewport(0, 0, surfaceWidth, surfaceHeight);

                GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                GLES20.glClear(GL10.GL_COLOR_BUFFER_BIT);

                GLES20.glUseProgram(program);
                //                GLES20.glDisable(GLES20.GL_DEPTH_TEST);

                FloatBuffer vertices =
                    ByteBuffer.allocateDirect(8 * 4)
                    .order(ByteOrder.nativeOrder())
                    .asFloatBuffer();
                vertices.put(new float[] {
                        -1, -1,
                        1, -1,
                        -1, 1,
                        1, 1,
                    });
                vertices.flip();

                int positionLoc = GLES20.glGetAttribLocation(program, "position");
                GLES20.glVertexAttribPointer(positionLoc, 2, GLES20.GL_FLOAT, false, 0, vertices);
                GLES20.glEnableVertexAttribArray(positionLoc);

                GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
                GLES20.glUseProgram(0);
                GLES20.glDisableVertexAttribArray(0);

                int err = GLES20.glGetError();
                if (err != GLES20.GL_NO_ERROR) {
                    Log.e("Hello", "OpenGL error encountered " + err);
                }

                int tmp = input;
                input = output;
                output = tmp;
            }
        }
    }


    @Override
    public void onCreate(Bundle b) {
        super.onCreate(b);

        //        setContentView(new MyView(this));
        setContentView(new MyViewGL(this));
    }

    @Override
    public void onResume() {
        super.onResume();
    }

    @Override
    public void onPause() {
        super.onPause();
    }
}
