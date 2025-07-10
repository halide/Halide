import numpy as np
import torchvision.models.resnet as resnet
import struct
import os
import sys


def load_weights(dir):
    if not os.path.isdir(dir):
        print(f"Path {dir} is not a dir")
        sys.exit(1)

    print("-----------loading weights------------")
    resnet50 = resnet.resnet50(pretrained=True)
    resnet50.eval()
    dic = resnet50.state_dict()
    # numpy stores weights in output channel, input channel, h, w order
    # we want to transpose to output channel, h, w, input channel order
    for k in dic:
        weight = dic[k].cpu().detach().numpy()
        weight = weight.astype(np.float32)
        print("weight shape before transpose")
        print(f"{k},{weight.shape},{len(weight.shape)}")
        if len(weight.shape) == 4:
            weight = np.transpose(weight, (1, 2, 3, 0))
        if len(weight.shape) == 2:
            weight = np.transpose(weight, (1, 0))
        print("weight shape after transpose")
        print(f"{k},{weight.shape},{len(weight.shape)}")

        data = weight.tobytes()
        path = os.path.join(dir, k.replace(".", "_"))
        with open(path + ".data", "wb") as f:
            f.write(data)
        with open(path + "_shape.data", "wb") as f:
            f.write(struct.pack("i", len(weight.shape)))
            f.writelines(
                struct.pack("i", weight.shape[i])
                for i in reversed(range(len(weight.shape)))
            )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: load_weights destdir")
        sys.exit(1)
    load_weights(sys.argv[1])
