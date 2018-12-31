import argparse
import numpy as np
import torch
import torch.nn
import scipy.io
import scipy.io.matlab as ml
import torch.utils.model_zoo as model_zoo
import struct 
import os

def load_weights():
    if not os.path.exists("./weights"):
        os.mkdir("./weights")

    dic = model_zoo.load_url('https://download.pytorch.org/models/resnet50-19c8e357.pth')
    print("-----------loading weights------------")
    print(dic.keys())

    # numpy stores weights in output channel, input channel, h, w order
    # we want to transpose to output channel, h, w, input channel order 
    for k in dic.keys():
        weight = dic[k].cpu().detach().numpy()
        weight = weight.astype(np.float32)
        if len(weight.shape) == 4:
            weight = np.transpose(weight, (0,2,3,1))
        print("%s,%s,%d" % (k, str(weight.shape), len(weight.shape)))

        data = weight.tobytes('C')
        if k == "bn1.running_mean":
            print(weight[0])
            print(weight[1])
            print(weight[2])
            print(data)
            reread = np.frombuffer(data, dtype=np.float32)
            print(reread[0])
            print(reread[1])
            print(reread[2])
            print(reread.shape)
        with open("weights/" + k.replace('.','_') + ".data", "wb") as f:
            f.write(data)
        with open("weights/" + k.replace('.', '_') + '_shape.data', 'wb') as f:
            f.write(struct.pack('i', len(weight.shape)))
            for i in range(len(weight.shape)):
                f.write(struct.pack('i', weight.shape[i]))

if __name__ == "__main__":
    load_weights()
