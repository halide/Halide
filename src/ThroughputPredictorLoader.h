#ifndef HALIDE_INTERNAL_THROUGHPUT_PREDICTOR_LOADER_H
#define HALIDE_INTERNAL_THROUGHPUT_PREDICTOR_LOADER_H

#include "Buffer.h"

#include <iostream>
#include <assert.h>

namespace {
    using namespace Halide;
    struct StatsShapes {
      int pipeline_stats[2] = {56,7};
      int schedule_stats[1] = {18};
    };
    
    struct Stats {
      Buffer<float> pipeline_mean{56, 7};
      Buffer<float> pipeline_std{56,7};
      Buffer<float> schedule_mean{18};
      Buffer<float> schedule_std{18};
    };

    Stats load_stats() { 
      StatsShapes shapes;
      Stats stats;
      FILE* fp;
      size_t size, readSize;
      float* buffer;

      fp = fopen("../stats/pipeline_mean.data", "rb");
      size = shapes.pipeline_stats[0] * shapes.pipeline_stats[1];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);

      for (int k = 0; k < shapes.pipeline_stats[0]; k++) {
          for (int c = 0; c < shapes.pipeline_stats[1]; c++) {
              int index = k * shapes.pipeline_stats[1] + c;
              stats.pipeline_mean(k, c) = buffer[index]; 
              std::cout << stats.pipeline_mean(k, c) << " ";
              if ( stats.pipeline_mean(k, c)  !=  stats.pipeline_mean(k, c) )
                std::cout <<"pipe mean NAN" << std::endl;
          }
      }
      free(buffer);
      std::cout << std::endl;
      std::cout << " pipeline std" << std::endl;

      fp = fopen("../stats/pipeline_std.data", "rb");
      size = shapes.pipeline_stats[0] * shapes.pipeline_stats[1];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);

      for (int k = 0; k < shapes.pipeline_stats[0]; k++) {
          for (int c = 0; c < shapes.pipeline_stats[1]; c++) {
              int index = k * shapes.pipeline_stats[1] + c;
              stats.pipeline_std(k, c) = buffer[index]; 
              std::cout << stats.pipeline_std(k, c) << " ";
              if (stats.pipeline_std(k, c) != stats.pipeline_std(k, c))
                std::cout  << "pipe std NAN" << std::endl;
          }
      }
      free(buffer);
      std::cout << std::endl;
      std::cout << "scheduele mean " << std::endl;
      
      fp = fopen("../stats/schedule_mean.data", "rb");
      size = shapes.schedule_stats[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);

      for (int k = 0; k < shapes.schedule_stats[0]; k++) {
          stats.schedule_mean(k) = buffer[k]; 
          std::cout << stats.schedule_mean(k) << " ";
          if ( stats.schedule_mean(k) !=  stats.schedule_mean(k) )
            std::cout << "Sched mean NAN" << std::endl;
      }
      std::cout << std::endl;
      free(buffer);
      
      fp = fopen("../stats/schedule_std.data", "rb");
      size = shapes.schedule_stats[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);

      for (int k = 0; k < shapes.schedule_stats[0]; k++) {
          stats.schedule_std(k) = buffer[k]; 
          std::cout << stats.schedule_std(k) << " ";
          if ( stats.schedule_std(k) !=  stats.schedule_std(k)) 
            std::cout << "sched std NAN" << std::endl;
      }
      free(buffer);
      std::cout << std::endl;
      return stats;
    }
    
    
    struct WeightShapes {
      int head1_filter[3] = {20, 56, 7};
      int head1_bias[1] = {20};

      int head2_filter[2] = {20, 18};
      int head2_bias[1] = {20};

      int conv1_filter[3] = {40, 40, 3};
      int conv1_bias[1] = {40};

      int conv2_filter[3] = {40, 40, 3};
      int conv2_bias[1] = {40};

      int conv3_filter[3] = {80, 40, 3};
      int conv3_bias[1] = {80};

      int conv4_filter[3] = {120, 80, 3};
      int conv4_bias[1] = {120};
      
      int conv5_filter[3] = {160, 120, 3};
      int conv5_bias[1] = {160};

      int fc1_filter[2] = {80, 160};
      int fc1_bias[1] = {80};

      int fc2_filter[2] = {40, 80};
      int fc2_bias[1] = {40};

      int fc3_filter[2] = {1, 40};
      int fc3_bias[1] = {1};
    };

    struct Weights {
      Buffer<float> head1_filter{20, 56, 7};
      Buffer<float> head1_bias{20};

      Buffer<float> head2_filter{20, 18};
      Buffer<float> head2_bias{20};

      Buffer<float> conv1_filter{40, 40, 3};
      Buffer<float> conv1_bias{40};
      
      Buffer<float> conv2_filter{40, 40, 3};
      Buffer<float> conv2_bias{40};
      
      Buffer<float> conv3_filter{80, 40,3};
      Buffer<float> conv3_bias{80};
      
      Buffer<float> conv4_filter{120, 80,3};
      Buffer<float> conv4_bias{120};
      
      Buffer<float> conv5_filter{160, 120, 3};
      Buffer<float> conv5_bias{160};
      
      Buffer<float> fc1_filter{80,160};
      Buffer<float> fc1_bias{80};
      
      Buffer<float> fc2_filter{40, 80};
      Buffer<float> fc2_bias{40};
      
      Buffer<float> fc3_filter{1,40};
      Buffer<float> fc3_bias{1};
    };

    Weights load_weights() { 
      WeightShapes shapes;
      Weights W;
      FILE* fp;
      size_t size, readSize;
      float* buffer;

      // load head1_filter
      fp = fopen("../weights/head1_conv1.weight.data", "rb");
      size = shapes.head1_filter[0] * shapes.head1_filter[1] * shapes.head1_filter[2];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);

      for (int k = 0; k < shapes.head1_filter[0]; k++) {
          for (int c = 0; c < shapes.head1_filter[1]; c++) {
            for (int x = 0; x < shapes.head1_filter[2]; x++) {
              int index = k * shapes.head1_filter[1] * shapes.head1_filter[2] + c * shapes.head1_filter[2] + x;
              W.head1_filter(k, c, x) = buffer[index]; 
            }
          }
      }
      std::cout << std::endl;
      free(buffer);
      
      // load head1_bias
      fp = fopen("../weights/head1_conv1.bias.data", "rb");
      size = shapes.head1_bias[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int c = 0; c < shapes.head1_bias[0]; c++) {
          W.head1_bias(c) = buffer[c]; 
      }
      free(buffer);

      // load head2_filter
      fp = fopen("../weights/head2_conv1.weight.data", "rb");
      size = shapes.head2_filter[0] * shapes.head2_filter[1];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);

      for (int k = 0; k < shapes.head2_filter[0]; k++) {
          for (int c = 0; c < shapes.head2_filter[1]; c++) {
            int index = k * shapes.head2_filter[1] + c;
            W.head2_filter(k, c) = buffer[index]; 
          }
      }
      free(buffer);

      //load head2_bias
      fp = fopen("../weights/head2_conv1.bias.data", "rb");
      size = shapes.head2_bias[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int c = 0; c < shapes.head2_bias[0]; c++) {
          W.head2_bias(c) = buffer[c]; 
          if (W.head2_bias(c) != W.head2_bias(c)) std::cout << "NAN in head2 bias";
      }
      free(buffer);

      // load conv1
      fp = fopen("../weights/trunk_conv1.weight.data", "rb");
      size = shapes.conv1_filter[0] * shapes.conv1_filter[1] * shapes.conv1_filter[2];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);

      for (int k = 0; k < shapes.conv1_filter[0]; k++) {
          for (int c = 0; c < shapes.conv1_filter[1]; c++) {
              for (int x = 0; x < shapes.conv1_filter[2]; x++) {
                  int index = k * shapes.conv1_filter[1] * shapes.conv1_filter[2] + c * shapes.conv1_filter[2] + x;
                  W.conv1_filter(k,c,x) = buffer[index]; 
                  if (W.conv1_filter(k,c,x) != W.conv1_filter(k,c,x)) std::cout << "NAN in conv1 filter";
              }
          }
      }
      free(buffer);
      
      
      fp = fopen("../weights/trunk_conv1.bias.data", "rb");
      size = shapes.conv1_bias[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int c = 0; c < shapes.conv1_bias[0]; c++) {
          W.conv1_bias(c) = buffer[c]; 
          if (W.conv1_bias(c) != W.conv1_bias(c)) std::cout << "NAN in conv1 bias";
      }
      free(buffer);

      // load trunk_conv2
      fp = fopen("../weights/trunk_conv2.weight.data", "rb");
      size = shapes.conv2_filter[0] * shapes.conv2_filter[1] * shapes.conv2_filter[2];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int k = 0; k < shapes.conv2_filter[0]; k++) {
          for (int c = 0; c < shapes.conv2_filter[1]; c++) {
              for (int x = 0; x < shapes.conv2_filter[2]; x++) {
                  int index = k * (shapes.conv2_filter[1]*shapes.conv2_filter[2]) + c * shapes.conv2_filter[2] + x;
                  W.conv2_filter(k,c,x) =buffer[index]; 
              }
          }
      }
      free(buffer);

      fp = fopen("../weights/trunk_conv2.bias.data", "rb");
      size = shapes.conv2_bias[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int c = 0; c < shapes.conv2_bias[0]; c++) {
          W.conv2_bias(c) = buffer[c]; 
      }
      free(buffer);

      std::cout << 4 << std::endl;      
      
      // load trunk_conv3
      fp = fopen("../weights/trunk_conv3.weight.data", "rb");
      size = shapes.conv3_filter[0] * shapes.conv3_filter[1] * shapes.conv3_filter[2];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int k = 0; k < shapes.conv3_filter[0]; k++) {
          for (int c = 0; c < shapes.conv3_filter[1]; c++) {
              for (int x = 0; x < shapes.conv3_filter[2]; x++) {
                  int index = k * (shapes.conv3_filter[1]*shapes.conv3_filter[2]) + c * shapes.conv3_filter[2] + x;
                  W.conv3_filter(k,c,x) =buffer[index]; 
              }
          }
      }
      free(buffer);

      fp = fopen("../weights/trunk_conv3.bias.data", "rb");
      size = shapes.conv3_bias[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      
      for (int c = 0; c < shapes.conv3_bias[0]; c++) {
          W.conv3_bias(c) = buffer[c]; 
      }
      free(buffer);

      // load trunk_conv4
      fp = fopen("../weights/trunk_conv4.weight.data", "rb");
      size = shapes.conv4_filter[0] * shapes.conv4_filter[1] * shapes.conv4_filter[2];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int k = 0; k < shapes.conv4_filter[0]; k++) {
          for (int c = 0; c < shapes.conv4_filter[1]; c++) {
              for (int x = 0; x < shapes.conv4_filter[2]; x++) {
                  int index = k * (shapes.conv4_filter[1]*shapes.conv4_filter[2]) + c * shapes.conv4_filter[2] + x;
                  W.conv4_filter(k,c,x) =buffer[index]; 
              }
          }
      }
      free(buffer);

      fp = fopen("../weights/trunk_conv4.bias.data", "rb");
      size = shapes.conv4_bias[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int c = 0; c < shapes.conv4_bias[0]; c++) {
          W.conv4_bias(c) = buffer[c]; 
      }
      free(buffer);		

      // load trunk_conv5
      fp = fopen("../weights/trunk_conv5.weight.data", "rb");
      size = shapes.conv5_filter[0] * shapes.conv5_filter[1] * shapes.conv5_filter[2];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int k = 0; k < shapes.conv5_filter[0]; k++) {
          for (int c = 0; c < shapes.conv5_filter[1]; c++) {
              for (int x = 0; x < shapes.conv5_filter[2]; x++) {
                  int index = k * (shapes.conv5_filter[1]*shapes.conv5_filter[2]) + c * shapes.conv5_filter[2] + x;
                  W.conv5_filter(k,c,x) =buffer[index]; 
              }
          }
      }
      free(buffer);

      fp = fopen("../weights/trunk_conv5.bias.data", "rb");
      size = shapes.conv5_bias[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int c = 0; c < shapes.conv5_bias[0]; c++) {
          W.conv5_bias(c) = buffer[c]; 
      }
      free(buffer);
      
      // load trunk_fc1
      fp = fopen("../weights/trunk_fc1.weight.data", "rb");
      size = shapes.fc1_filter[0] * shapes.fc1_filter[1];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int k = 0; k < shapes.fc1_filter[0]; k++) {
          for (int c = 0; c < shapes.fc1_filter[1]; c++) {
              int index = k * (shapes.fc1_filter[1]) + c;
              W.fc1_filter(k,c) =buffer[index]; 
          }
      }
      free(buffer);

      fp = fopen("../weights/trunk_fc1.bias.data", "rb");
      size = shapes.fc1_bias[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int c = 0; c < shapes.fc1_bias[0]; c++) {
          W.fc1_bias(c) = buffer[c]; 
      }
      free(buffer);		
      
      // load trunk_fc2
      fp = fopen("../weights/trunk_fc2.weight.data", "rb");
      size = shapes.fc2_filter[0] * shapes.fc2_filter[1];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int k = 0; k < shapes.fc2_filter[0]; k++) {
          for (int c = 0; c < shapes.fc2_filter[1]; c++) {
              int index = k * (shapes.fc2_filter[1]) + c;
              W.fc2_filter(k,c) =buffer[index]; 
          }
      }
      free(buffer);

      fp = fopen("../weights/trunk_fc2.bias.data", "rb");
      size = shapes.fc2_bias[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int c = 0; c < shapes.fc2_bias[0]; c++) {
          W.fc2_bias(c) = buffer[c]; 
      }
      free(buffer);		
      
      // load trunk_fc3
      fp = fopen("../weights/trunk_fc3.weight.data", "rb");
      size = shapes.fc3_filter[0] * shapes.fc3_filter[1];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp);
      assert(readSize == size);
      
      for (int k = 0; k < shapes.fc3_filter[0]; k++) {
          for (int c = 0; c < shapes.fc3_filter[1]; c++) {
              int index = k * (shapes.fc3_filter[1]) + c;
              W.fc3_filter(k,c) = buffer[index]; 
          }
      }
      free(buffer);

      fp = fopen("../weights/trunk_fc3.bias.data", "rb");
      size = shapes.fc3_bias[0];
      buffer = (float*) malloc (sizeof(float)*size);
      readSize = fread(buffer, sizeof(float), size, fp); 
      assert(readSize == size);

      for (int c = 0; c < shapes.fc3_bias[0]; c++) {
          W.fc3_bias(c) = buffer[c]; 
      }
      free(buffer);

      return W;		
    }
}
#endif
