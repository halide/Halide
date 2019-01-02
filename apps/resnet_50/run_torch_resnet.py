from PIL import Image
import torch
from torch_resnet import resnet50
import torchvision.transforms as transforms



def image_loader(image_name, size):
    loader = transforms.Compose([transforms.Scale(size), transforms.ToTensor()])
    image = Image.open(image_name)
    image = loader(image).float()
    return image


if __name__ == "__main__":
    image_size = 224
    image = image_loader("cropped_panda.jpg", image_size)
    # add dimension for batch size
    image = torch.unsqueeze(image, 0)
    print(image.size())
    net = resnet50(pretrained=True)
    result = net(image)
    print(result)



