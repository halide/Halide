
export void upsample_x(uniform int width, 
                       uniform int height,
                       uniform const float input[],
                       uniform float output[]) {
    // edges
    foreach (y = 0 ... height) {
        output[y*width*2 + 0] = input[y*width + 0];
        output[(y+1)*width*2 - 1] = input[(y+1)*width - 1];
    }

    // middle
    foreach (y = 0 ... height, x = 0 ... width-1) {
        float l = input[y*width + x];
        float r = input[y*width + x + 1];
        output[y*width*2 + 2*x + 1] = 0.75f * l + 0.25f * r;
        output[y*width*2 + 2*x + 2] = 0.25f * l + 0.75f * r;
    }
}

export void upsample_y(uniform int width, 
                       uniform int height,
                       uniform const float input[],
                       uniform float output[]) {
    // upsample in y

    // top row
    foreach (x = 0 ... width) {
        output[x] = input[x];
    }

    foreach (y = 0 ... height-1, x = 0 ... width) {
        float u = input[y*width + x];
        float d = input[(y+1)*width + x];
        output[(2*y+1)*width + x] = 0.75f * u + 0.25f * d;
        output[(2*y+2)*width + x] = 0.25f * u + 0.75f * d;
    }

    // bottom row
    foreach (x = 0 ... width) {
        output[(height*2-1)*width + x] = input[(height-1)*width + x];
    }
}

export void downsample_x(uniform int width,
                         uniform int height,
                         uniform const float input[],
                         uniform float output[]) {
    // edges
    foreach (y = 0 ... height) {
        output[y*(width/2)] = (4*input[y*width] +
                               3*input[y*width+1] + 
                               input[y*width+2]) / 8.0f;
        output[(y+1)*(width/2)-1] = (4*input[(y+1)*width-1] +
                                     3*input[(y+1)*width-2] +
                                     input[(y+1)*width-3])/8;
    }

    foreach (y = 0 ... height, x = 1 ... (width/2)-1) {
        output[y*(width/2) + x] = (input[y*width + 2*x-1] +
                                   3*input[y*width + 2*x] + 
                                   3*input[y*width + 2*x+1] + 
                                   input[y*width + 2*x+2])/8;
    }
}

export void downsample_y(uniform int width,
                         uniform int height,
                         uniform const float input[],
                         uniform float output[]) {
    foreach (x = 0 ... width) {
        output[x] = (4*input[x] + 3*input[width + x] + input[2*width + x])/8;
        output[(height/2-1)*width + x] = (4*input[(height-1)*width + x] + 
                                          3*input[(height-2)*width + x] + 
                                          input[(height-3)*width + x])/8;
    }

    foreach(y = 1 ... (height/2)-1, x = 0 ... width) {
        output[y*width + x] = (input[(2*y-1)*width + x] + 
                               3*input[(2*y)*width + x] + 
                               3*input[(2*y+1)*width + x] + 
                               input[(2*y+2)*width + x])/8;
    }    
}

export void rev_subtract_in_place(uniform int width, 
                                  uniform int height,
                                  uniform const float input[],
                                  uniform float output[]) {
    foreach (y = 0 ... height, x = 0 ... width) {
        output[y*width + x] = input[y*width + x] - output[y*width + x];
    }
}

export void add_in_place(uniform int width, 
                         uniform int height,
                         uniform const float input[],
                         uniform float output[]) {
    foreach (y = 0 ... height, x = 0 ... width) {
        output[y*width + x] += input[y*width + x];
    }
}

export void make_output_pyramid(uniform int width,
                                uniform int height,
                                uniform int levels,
                                uniform const float inGPyramid[],
                                uniform const float lPyramid[],
                                uniform float olPyramid[]) {

    foreach (y = 0 ... height, x = 0 ... width) {
        float level = inGPyramid[y*width + x] * (levels-1);
        int li = (int)(level);
        if (li < 0) li = 0;
        if (li > levels-2) li = levels-2;
        float lf = level - li;
        olPyramid[y*width+x] = ((1.0f - lf) * lPyramid[(li*height + y)*width + x] + 
                                lf * lPyramid[((li+1)*height + y)*width + x]);
    }
}



export void make_remap_lut(uniform int levels,
                           uniform float alpha,
                           uniform float output[]) {
    foreach (x = 0 ... (levels*256*2)) {
        float fx = (x - levels*256) / 256.0f;
        output[x] = alpha * fx * exp(-fx*fx/2.0f);
    }
}

export void remap(uniform int width,
                  uniform int height,
                  uniform int levels,
                  uniform int k,
                  uniform float beta,
                  const uniform float input[],
                  const uniform float lut[],
                  uniform float output[]) {

    foreach (y = 0 ... height, x = 0 ... width) {
        int idx = (int)(input[y*width + x]*(levels-1)*256.0f);
        if (idx < 0) idx = 0;
        if (idx > (levels-1)*256) idx = (levels-1)*256;
        output[(k*height + y)*width + x] = beta * input[y*width + x] + lut[idx - 256*k + levels*256];
    }
}

export void rgb2gray(uniform int width,
                     uniform int height,
                     const uniform float input[],
                     uniform float output[]) {

    foreach (px = 0 ... width*height) {
        output[px] = 0.299f * input[px+0] + 0.587f * input[px+1*width*height] + 0.114f * input[px+2*width*height];
    }
}

export void reintroduce_color(uniform int width, 
                              uniform int height,
                              const uniform float new_luma[],
                              const uniform float old_luma[],
                              const uniform float chroma[],
                              uniform float output[]) {
    foreach (px = 0 ... width*height) {
        float scale = new_luma[px] / old_luma[px];
        
        output[px+0] = chroma[px+0] * scale;
        output[px+1*width*height] = chroma[px+1*width*height] * scale;
        output[px+2*width*height] = chroma[px+2*width*height] * scale;       
    }
}

export void float_to_uint16(uniform int width,
                            uniform int height,
                            const uniform float input[],
                            uniform unsigned int16 output[]) {
    foreach (px = 0 ... width*height*3) {
        float val = input[px];
        if (val > 1.0f) val = 1.0f;
        if (val < 0.0f) val = 0.0f;
        output[px] = (unsigned int16)(val * 65535.0f);
    }
}

export void uint16_to_float(uniform int width,
                            uniform int height,
                            const uniform unsigned int16 input[],
                            uniform float output[]) {
    foreach (px = 0 ... width*height*3) {
        output[px] = input[px] / 65535.0f;
    }
}
