import numpy as np
import torch.utils.model_zoo as model_zoo
import struct
import os
import sys

def load_weights(dir):
    if not os.path.isdir(dir):
        print("Path %s is not a dir" % dir)
        sys.exit(1)

    dic = model_zoo.load_url('https://download.pytorch.org/models/resnet50-19c8e357.pth')
    print("-----------loading weights------------")
    print(dic.keys())

    # numpy stores weights in output channel, input channel, h, w order
    # we want to transpose to output channel, h, w, input channel order
    for k in dic.keys():
        weight = dic[k].cpu().detach().numpy()
        weight = weight.astype(np.float32)
        print("weight shape before transpose")
        print("%s,%s,%d" % (k, str(weight.shape), len(weight.shape)))
        if len(weight.shape) == 4:
            weight = np.transpose(weight, (1, 2, 3, 0))
        if len(weight.shape) == 2:
            weight = np.transpose(weight, (1, 0))
        print("weight shape after transpose")
        print("%s,%s,%d" % (k, str(weight.shape), len(weight.shape)))

        data = weight.tobytes()
        path = os.path.join(dir, k.replace('.','_'))
        with open(path + ".data", "wb") as f:
            f.write(data)
        with open(path + '_shape.data', 'wb') as f:
            f.write(struct.pack('i', len(weight.shape)))
            for i in list(reversed(range(len(weight.shape)))):
                f.write(struct.pack('i', weight.shape[i]))

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: load_weights destdir")
        sys.exit(1)
    load_weights(sys.argv[1])
