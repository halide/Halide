/// Copyright (c) 2012 The Native Client Authors. All rights reserved.
/// Use of this source code is governed by a BSD-style license that can be
/// found in the LICENSE file.
///
/// @file hello_nacl.cpp
/// This example demonstrates loading, running and scripting a very simple NaCl
/// module.  To load the NaCl module, the browser first looks for the
/// CreateModule() factory method (at the end of this file).  It calls
/// CreateModule() once to load the module code from your .nexe.  After the
/// .nexe code is loaded, CreateModule() is not called again.
///
/// Once the .nexe code is loaded, the browser than calls the CreateInstance()
/// method on the object returned by CreateModule().  It calls CreateInstance()
/// each time it encounters an <embed> tag that references your NaCl module.
///
/// The browser can talk to your NaCl module via the postMessage() Javascript
/// function.  When you call postMessage() on your NaCl module from the browser,
/// this becomes a call to the HandleMessage() method of your pp::Instance
/// subclass.  You can send messages back to the browser by calling the
/// PostMessage() method on your pp::Instance.  Note that these two methods
/// (postMessage() in Javascript and PostMessage() in C++) are asynchronous.
/// This means they return immediately - there is no waiting for the message
/// to be handled.  This has implications in your program design, particularly
/// when mutating property values that are exposed to both the browser and the
/// NaCl module.

#include <cstdio>
#include <string>
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/graphics_2d.h"
#include "ppapi/cpp/image_data.h"
#include "ppapi/cpp/completion_callback.h"
#include "ppapi/cpp/input_event.h"
#include <sys/time.h>
#include <string.h>
#include <sstream>

#include "halide_game_of_life.h"

#define WIDTH 1024
#define HEIGHT 1024
#define MARGIN 8

// A low-level scalar C version to use for timing comparisons
extern "C" void c_game_of_life(buffer_t *in, buffer_t *out);

using namespace pp;

bool busy;
void completion_callback(void *data, int32_t flags) {
    fprintf(stderr, "Got a completion callback with data %p flags %d\n", data, flags);
    busy = false;
}

buffer_t ImageToBuffer(const ImageData &im) {
    buffer_t buf;
    memset(&buf, 0, sizeof(buffer_t));
    buf.host = (uint8_t *)im.data();
    buf.extent[0] = im.size().width();
    buf.stride[0] = 1;
    buf.extent[1] = im.size().height();
    buf.stride[1] = im.stride()/4;
    buf.elem_size = 4;
    return buf;
}

extern "C" void halide_shutdown_thread_pool();

bool pipeline_barfed = false;
static Instance *inst = NULL;
extern "C" void halide_error(char *msg) {
    if (inst) {
        inst->PostMessage(msg);
        pipeline_barfed = true;
    }
}

/// The Instance class.  One of these exists for each instance of your NaCl
/// module on the web page.  The browser will ask the Module object to create
/// a new Instance for each occurence of the <embed> tag that has these
/// attributes:
///     type="application/x-nacl"
///     src="hello_nacl.nmf"
/// To communicate with the browser, you must override HandleMessage() for
/// receiving messages from the browser, and use PostMessage() to send messages
/// back to the browser.  Note that this interface is asynchronous.
class HelloHalideInstance : public Instance {
public:
    Graphics2D::Graphics2D graphics;
    ImageData im1, im2;
    CompletionCallback callback;

    /// The constructor creates the plugin-side instance.
    /// @param[in] instance the handle to the browser-side plugin instance.
    explicit HelloHalideInstance(PP_Instance instance) : 
        Instance(instance),
        graphics(this, Size(WIDTH, HEIGHT), false),
        im1(this, PP_IMAGEDATAFORMAT_BGRA_PREMUL, Size(WIDTH, HEIGHT), false),
        im2(this, PP_IMAGEDATAFORMAT_BGRA_PREMUL, Size(WIDTH, HEIGHT), false),
        callback(completion_callback, this) {
        BindGraphics(graphics);
        RequestInputEvents(PP_INPUTEVENT_CLASS_MOUSE);
        inst = this;
    }
    virtual ~HelloHalideInstance() {}
    
    virtual bool HandleInputEvent(const pp::InputEvent &event) {
        if (event.GetType() == PP_INPUTEVENT_TYPE_MOUSEMOVE) {
            pp::MouseInputEvent ev(event);
            Point p = ev.GetPosition();
            for (int dy = -4; dy <= 4; dy++) {
                int y = p.y() + dy;
                if (y < MARGIN) y = MARGIN;
                if (y > HEIGHT - MARGIN - 1) y = HEIGHT - MARGIN - 1;
                for (int dx = -4; dx <= 4; dx++) {
                    int x = p.x() + dx;
                    if (x < MARGIN) x = MARGIN;
                    if (x > WIDTH - MARGIN - 1) x = WIDTH - MARGIN - 1;
                    if (dx*dx + dy*dy < 4*4) {                        
                        uint32_t col;
                        switch (rand() & 3) {
                        case 0:
                            col = 0x000000ff;
                            break;
                        case 1:
                            col = 0x0000ffff;
                            break;
                        case 2:
                            col = 0x00ff00ff;
                            break;
                        case 3:
                            col = 0xff0000ff;
                        }
                        Point q(x, y);
                        *(im1.GetAddr32(q)) = col;
                        *(im2.GetAddr32(q)) = col;
                    }
                }
            }

            return true;
        }
        return false;
    }

    /// Handler for messages coming in from the browser via postMessage().  The
    /// @a var_message can contain anything: a JSON string; a string that encodes
    /// method names and arguments; etc.  For example, you could use
    /// JSON.stringify in the browser to create a message that contains a method
    /// name and some parameters, something like this:
    ///   var json_message = JSON.stringify({ "myMethod" : "3.14159" });
    ///   nacl_module.postMessage(json_message);
    /// On receipt of this message in @a var_message, you could parse the JSON to
    /// retrieve the method name, match it to a function call, and then call it
    /// with the parameter.
    /// @param[in] var_message The message posted by the browser.
    virtual void HandleMessage(const Var& var_message) {

        if (busy) return;
        busy = true;

        static int thread_pool_size = 8;
        static int halide_last_t = 0;
        static int halide_time_weight = 0;
        static int c_last_t = 0;
        static int c_time_weight = 0;
        static bool use_halide = true;

        if (var_message.is_string()) {            
            std::string msg = var_message.AsString();
            int threads = atoi(msg.c_str()+2);
            if (threads < 1) threads = 1;
            if (threads > 32) threads = 32;
            if (threads > 0 && threads <= 32 && thread_pool_size != threads) {
                halide_shutdown_thread_pool();
                thread_pool_size = threads;
                char buf[256];
                snprintf(buf, 256, "%d", threads);
                setenv("HL_NUMTHREADS", buf, 1);
                halide_last_t = 0;
                halide_time_weight = 0;
            }

            bool new_use_halide = (msg[0] == '0');
            if (new_use_halide != use_halide) {
                use_halide = new_use_halide;
            }
        }

        buffer_t input = ImageToBuffer(im1);
        buffer_t output = ImageToBuffer(im2);

        // Only compute the inner part of output so that we don't have
        // to worry about boundary conditions.
        output.min[0] = output.min[1] = MARGIN;
        output.extent[0] -= MARGIN*2;
        output.extent[1] -= MARGIN*2;
        output.host += (output.stride[1] + output.stride[0]) * MARGIN * 4;

        // Initialize the input with noise
        static bool first_run = true;
        if (first_run) {
            first_run = false; 

            // Start with 8 threads
            setenv("HL_NUMTHREADS", "8", 1);

            //  Initialize the buffers                        
            memset(im2.data(), 0, im2.stride() * im2.size().height());

            for (int y = 0; y < HEIGHT; y++) {
                uint8_t *ptr = ((uint8_t *)im1.data()) + im1.stride() * y;
                for (int x = 0; x < WIDTH; x++) {
                    ptr[x*4] = ((rand() & 31) == 0) ? 255 : 0;
                    ptr[x*4+1] = ((rand() & 31) == 0) ? 255 : 0;
                    ptr[x*4+2] = ((rand() & 31) == 0) ? 255 : 0;                
                    ptr[x*4+3] = (x >= MARGIN && 
                                  (x < (WIDTH - MARGIN)) && 
                                   y >= MARGIN && 
                                   y < (HEIGHT - MARGIN)) ? 255 : 0;
                }
            }            
                
        }
        
        timeval t1, t2;
        gettimeofday(&t1, NULL);
        if (use_halide) {
            halide_game_of_life(&input, &output);
        } else {
            c_game_of_life(&input, &output);
        }
        gettimeofday(&t2, NULL);        

        if (pipeline_barfed) return;

        int t = t2.tv_usec - t1.tv_usec;
        t += (t2.tv_sec - t1.tv_sec)*1000000;

        // Smooth it out so we can see a rolling average
        if (use_halide) {
            t = (halide_last_t * halide_time_weight + t) / (halide_time_weight + 1);
            halide_last_t = t;
            if (halide_time_weight < 100) {
                halide_time_weight++;
            }
        } else {
            t = (c_last_t * c_time_weight + t) / (c_time_weight + 1);
            c_last_t = t;
            if (c_time_weight < 100) {
                c_time_weight++;
            }
        }

        std::ostringstream oss;
        oss << "<table cellspacing=8><tr><td width=200 height=30>Halide routine takes:</td><td>";
        if (halide_time_weight < 10) {
            oss << "?";
        } else {
            if (use_halide) oss << "<b>";
            oss << halide_last_t;
            if (use_halide) oss << "</b>";
        }
        oss << " us</td></tr><tr><td width=200 height=30>Scalar C routine takes:</td><td>";
        if (c_time_weight < 10) {
            oss << "?";
        } else {
            if (!use_halide) oss << "<b>";
            oss << c_last_t;
            if (!use_halide) oss << "</b>";
        }
        oss << " us</td></tr></table>";

        PostMessage(oss.str());

        graphics.PaintImageData(im2, Point(0, 0));

        graphics.Flush(callback);         

        std::swap(im1, im2);
    }
};

/// The Module class.  The browser calls the CreateInstance() method to create
/// an instance of your NaCl module on the web page.  The browser creates a new
/// instance for each <embed> tag with type="application/x-nacl".
class HelloHalideModule : public Module {
public:
    HelloHalideModule() : Module() {}
    virtual ~HelloHalideModule() {}
    
    /// Create and return a HelloHalideInstance object.
    /// @param[in] instance The browser-side instance.
    /// @return the plugin-side instance.
    virtual Instance* CreateInstance(PP_Instance instance) {
        return new HelloHalideInstance(instance);
    }
};

namespace pp {
/// Factory function called by the browser when the module is first loaded.
/// The browser keeps a singleton of this module.  It calls the
/// CreateInstance() method on the object you return to make instances.  There
/// is one instance per <embed> tag on the page.  This is the main binding
/// point for your NaCl module with the browser.
Module* CreateModule() {
    return new HelloHalideModule();
}
}  // namespace pp
