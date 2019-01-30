from PIL import Image
import torch
from torch_resnet import resnet50
import torchvision.transforms as transforms
import torch.nn as nn
import numpy as np
import sys


def normalize(x):
    imagenet_mean = [0.485, 0.456, 0.406]
    imagenet_std = [0.229, 0.224, 0.225]

    for c in range(0,3):
        x[c,:,:] = (x[c,:,:] - imagenet_mean[c]) / imagenet_std[c]

    return x

def image_loader(image_name, size):
    loader = transforms.Compose([transforms.Scale(size), transforms.ToTensor()])
    image = Image.open(image_name)
    image = loader(image).float()
    image = normalize(image)
    return image


if __name__ == "__main__":
    image_size = 224
    image = image_loader("cropped_panda.jpg", image_size)
    # add dimension for batch size
    image = torch.unsqueeze(image, 0)
    net = resnet50(pretrained=True)
    result = net(image)

    halide_output_file = sys.argv[1]

    # Pytorch resnet50 model doesn't compute softmax of the output.
    softmax = nn.Softmax()
    ref_output = softmax(result)

    halide_output = np.fromfile(halide_output_file, dtype=np.float32)
    
    ref_output = ref_output.cpu().detach().numpy().reshape(halide_output.shape)

    np.testing.assert_almost_equal(halide_output, ref_output, decimal=4)
