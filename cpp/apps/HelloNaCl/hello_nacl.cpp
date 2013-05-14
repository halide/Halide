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
#include <sys/time.h>
#include <string.h>

#include "halide_game_of_life.h"

using namespace pp;

bool busy;
void completion_callback(void *data, int32_t flags) {
    fprintf(stderr, "Got a completion callback with data %p flags %d\n", data, flags);
    busy = false;
}

buffer_t ImageToBuffer(const ImageData &im) {
    buffer_t buf = {0};
    buf.host = (uint8_t *)im.data();
    buf.extent[0] = 4;
    buf.stride[0] = 1;
    buf.min[0] = 0;
    buf.extent[1] = im.size().width();
    buf.stride[1] = 4;
    buf.min[1] = 0;
    buf.extent[2] = im.size().height();
    buf.stride[2] = im.stride();
    buf.min[2] = 0;
    buf.extent[3] = 0;
    buf.stride[3] = 0;
    buf.min[3] = 0;
    buf.elem_size = 1;
    return buf;
}

extern "C" void halide_shutdown_thread_pool();

/// The Instance class.  One of these exists for each instance of your NaCl
/// module on the web page.  The browser will ask the Module object to create
/// a new Instance for each occurence of the <embed> tag that has these
/// attributes:
///     type="application/x-nacl"
///     src="hello_nacl.nmf"
/// To communicate with the browser, you must override HandleMessage() for
/// receiving messages from the browser, and use PostMessage() to send messages
/// back to the browser.  Note that this interface is asynchronous.
class HelloTutorialInstance : public Instance {
public:
    Graphics2D::Graphics2D graphics;
    ImageData im1, im2;
    CompletionCallback callback;

    /// The constructor creates the plugin-side instance.
    /// @param[in] instance the handle to the browser-side plugin instance.
    explicit HelloTutorialInstance(PP_Instance instance) : 
        Instance(instance),
        graphics(this, Size(1280, 960), false),
        im1(this, PP_IMAGEDATAFORMAT_BGRA_PREMUL, Size(1280, 960), false),
        im2(this, PP_IMAGEDATAFORMAT_BGRA_PREMUL, Size(1280, 960), false),
        callback(completion_callback, this) {
        BindGraphics(graphics);
    }
    virtual ~HelloTutorialInstance() {}
    
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

        static int thread_pool_size = 8;
        static int last_t = 0;
        static int time_weight = 0;

        if (var_message.is_string()) {
            std::string msg = var_message.AsString();
            int threads = atoi(msg.c_str());
            if (threads < 1) threads = 1;
            if (threads > 32) threads = 32;
            if (threads > 0 && threads <= 32 && thread_pool_size != threads) {
                halide_shutdown_thread_pool();
                thread_pool_size = threads;
                char buf[256];
                snprintf(buf, 256, "%d", threads);
                setenv("HL_NUMTHREADS", buf, 1);
                last_t = 0;
                time_weight = 0;
            }
        }

        if (busy) return;
        busy = true;

        buffer_t input = ImageToBuffer(im1);
        buffer_t output = ImageToBuffer(im2);

        // Initialize the input with noise
        static bool first_run = true;
        if (first_run) {
            first_run = false; 

            // Override the number of threads to use
            setenv("HL_NUMTHREADS", "8", 0);
            
            for (int y = 0; y < im1.size().height(); y++) {
                uint8_t *ptr = ((uint8_t *)im1.data()) + im1.stride() * y;
                for (int x = 0; x < im1.size().width(); x++) {
                    ptr[x*4] = ((rand() & 3) == 0) ? 0xff : 0;
                    ptr[x*4+1] = ((rand() & 3) == 0) ? 0xff : 0;
                    ptr[x*4+2] = ((rand() & 3) == 0) ? 0xff : 0;                
                    ptr[x*4+3] = 0xff;
                }                
            }
        }
        
        timeval t1, t2;
        gettimeofday(&t1, NULL);
        halide_game_of_life(&input, &output);
        gettimeofday(&t2, NULL);        

        int t = t2.tv_usec - t1.tv_usec;
        t += (t2.tv_sec - t1.tv_sec)*1000000;

        // Smooth it out so we can see a rolling average
        t = (last_t * time_weight + t) / (time_weight + 1);
        last_t = t;
        if (time_weight < 100) {
            time_weight++;
        }

        char buf[1024];
        snprintf(buf, 1024, 
                 "Halide routine takes %d us<br>"
                 "Currently using %d threads\n", time_weight > 10 ? t : 0, thread_pool_size);
        PostMessage(buf);

        graphics.PaintImageData(im2, Point(0, 0));

        graphics.Flush(callback);         

        std::swap(im1, im2);
    }
};

/// The Module class.  The browser calls the CreateInstance() method to create
/// an instance of your NaCl module on the web page.  The browser creates a new
/// instance for each <embed> tag with type="application/x-nacl".
class HelloTutorialModule : public Module {
public:
    HelloTutorialModule() : Module() {}
    virtual ~HelloTutorialModule() {}
    
    /// Create and return a HelloTutorialInstance object.
    /// @param[in] instance The browser-side instance.
    /// @return the plugin-side instance.
    virtual Instance* CreateInstance(PP_Instance instance) {
        return new HelloTutorialInstance(instance);
    }
};

namespace pp {
/// Factory function called by the browser when the module is first loaded.
/// The browser keeps a singleton of this module.  It calls the
/// CreateInstance() method on the object you return to make instances.  There
/// is one instance per <embed> tag on the page.  This is the main binding
/// point for your NaCl module with the browser.
Module* CreateModule() {
    return new HelloTutorialModule();
}
}  // namespace pp
