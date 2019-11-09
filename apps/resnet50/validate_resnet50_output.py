import torch
import torchvision.models.resnet as resnet
import torchvision.transforms as transforms
import torch.nn as nn
import numpy as np
import sys

if __name__ == "__main__":
    assert(len(sys.argv) == 3)
    halide_output_file = sys.argv[1]

    seed = int(sys.argv[2])
    np.random.seed(seed)
    input_data = np.random.rand(1, 224, 224, 3).astype(np.float32)
    input_data = input_data.transpose(0,3,1,2)
    image = torch.from_numpy(input_data)

    net = resnet.resnet50(pretrained=True) 
    net.eval()
    result = net(image)
    # Pytorch resnet50 model doesn't compute softmax of the output.
    softmax = nn.Softmax(dim=1)
    ref_output = softmax(result)

    halide_output = np.fromfile(halide_output_file, dtype=np.float32)
    
    ref_output = ref_output.cpu().detach().numpy().reshape(halide_output.shape)

    np.testing.assert_almost_equal(np.argmax(halide_output), np.argmax(ref_output))
    np.testing.assert_almost_equal(halide_output, ref_output, decimal=2)
    print('Success!')
