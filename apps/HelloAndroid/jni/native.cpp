#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <android/native_window_jni.h>
#include <time.h>

#include "halide.h"

#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,"halide_native",__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,"halide_native",__VA_ARGS__)

#define DEBUG 1

extern "C" void set_error_handler(void (*handler)(char *));

void handler(char *msg) {
    LOGE(msg);
}

extern "C" {
JNIEXPORT void JNICALL Java_com_example_hellohalide_CameraPreview_processFrame(JNIEnv * env, jobject obj, jbyteArray jSrc, jobject surf) {

    set_error_handler(handler);

    unsigned char *src = (unsigned char *)env->GetByteArrayElements(jSrc, NULL);

    ANativeWindow *win = ANativeWindow_fromSurface(env, surf);
    ANativeWindow_acquire(win);

    static bool first_call = true;
    if (first_call) {
      LOGD("Resetting buffer format");
      ANativeWindow_setBuffersGeometry(win, 640, 360, 0); 
      first_call = false;
    }

    ANativeWindow_Buffer buf;
    ARect rect = {0, 0, 640, 360};
    ANativeWindow_lock(win, &buf, &rect);

    uint8_t *dst = (uint8_t *)buf.bits;
    buffer_t srcBuf, dstBuf;
    srcBuf.host = (uint8_t *)src;
    srcBuf.dims[0] = 640;
    srcBuf.dims[1] = 360;
    srcBuf.dims[2] = 1;
    srcBuf.dims[3] = 1;
    srcBuf.elem_size = 1;

    dstBuf.host = dst;
    dstBuf.dims[0] = 640;
    dstBuf.dims[1] = 360;
    dstBuf.dims[2] = 1;
    dstBuf.dims[3] = 1;
    dstBuf.elem_size = 1;

    timeval t1, t2;
    gettimeofday(&t1, NULL);
    halide(&srcBuf, &dstBuf);
    gettimeofday(&t2, NULL);
    unsigned elapsed = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec - t1.tv_usec);
    LOGD("Time taken: %d", elapsed);


    // Just copy over chrominance untouched
    memcpy(dst + 640*360, src + 640*480, 320*180);
    memcpy(dst + 640*360 + 320*180, src + 640*480 + 320*240, 320*180);

    ANativeWindow_unlockAndPost(win);
    ANativeWindow_release(win);

    env->ReleaseByteArrayElements(jSrc, (jbyte *)src, 0);        
}
}
