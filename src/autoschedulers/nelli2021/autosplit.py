#!/usr/bin/env python
# coding: utf-8

# # AUTOSPLIT:  Rule-based autosplitting of an ONNX model
# ### Version 1.0 21-Oct-2021 by Nelli Fedorova (nellix.fedorova@intel.com)
# 
# Apply heuristic rules to split ONNX model so that each split contains one convolution layer at most. Return names or indices of nodes after which to split 
# 
# ## Dependencies
# 
# onnxread.py
# 
# ## autosplit_onnx
# - ### Inputs
#     - fmodel - full path to onnx file, e.g. '/home/nelli/onnx_model_zoo_cache/ bvlcalexnet-9/model.onnx'
#     - sfx - suffix to add to each node name in the output list, default: '_node'
# - ### Output
#     - list of nodes after which to split
# - ### Example
#     > print(autosplit_onnx('/home/nelli/onnx_model_zoo_cache/ bvlcalexnet-9/model.onnx'))
#     
#     > Relu_1_node MaxPool_3_node Relu_5_node MaxPool_7_node Relu_9_node Relu_11_node MaxPool_14_node Relu_17_node Relu_20_node Gemm_22_node Softmax_23_node
# 
# ## split
# - ### Inputs
#     - ops - list of op_types
#     - names - list of corresponding node names (should have the same length as ops)
#     - split_after_ops - True to split after ops in split_ops
#     - split_after_names - True to split after nodes with names in split_names
#     - split_ops - list of op_types after which to split (e.g. ['Relu','BatchNormalization']
#     - split_names - list of node names after which to split
#     - sfx - suffix to add to each node name to match names in split_ops and split_names, default: '_node'
# - ### Output
#     - 0-based indices of nodes after which to split
# - ### Example
#     > print(split(['Conv','BatchNormalization','Relu','Conv','BatchNormalization','Softmax'],['n0','n1','n2','n3','n4','n5'], True, True, ['Relu'], ['n4'], ''))
#     
#     > [2, 4, 5]

# In[9]:


def autosplit(nodes, num_out):   
    n = len(nodes)
    
    split_on_ops = [ 'GlobalAveragePool', 'AveragePool', 'MaxPool', 'Upsample', 'Add', 
                    'LeakyRelu', 'Relu', 'Concat', 'Gemm', 'Split', 'Slice', 'Conv' ] 
    ind = []
    ind_conv = []
    for i in range(n):
        if i < n-1 and num_out[ i ] > 1:
            continue # don't split on multiple outputs
        if nodes[ i ] in split_on_ops:
            if nodes[ i ] == 'Conv':
                ind_conv.append(i)
            else:
                ind.append(i)
                if len(ind_conv) > 1:
                    ii = ind_conv[ len(ind_conv)-1 ]-1
                    ind.append(ii)
                ind_conv = []
                
    for i in range(3,n):
        if  ' '.join(nodes[ i-3:i+1 ]) == 'Add Relu Conv Concat': 
            for j in range(i-3,i): 
                if j in ind: ind.remove(j)
            if i not in ind: ind.append(i) # split on Concat only
    for i in range(4,n):
        if  ' '.join(nodes[ i-4:i+1 ]) == 'Unsqueeze Add Relu Conv BatchNormalization': 
            for j in range(i-4,i+1): 
                if j in ind: ind.remove(j) # don't split 
    for i in range(1,n):
        if  ' '.join(nodes[ i-1:i+1 ]) == 'Split Conv': 
            if i in ind: ind.remove(i) # don't split on Conv
    for i in range(1,n):
        if  ' '.join(nodes[ i-1:i+1 ]) == 'Transpose Reshape': 
            if i-1 in ind: ind.remove(i-1) # don't split on Transpose
            if i not in ind: ind.append(i) # split on Reshape
    for i in ind:
        if i < n-1 and num_out[ i ] > 1:
            ind.remove(i) # don't split on multiple outputs
    # Add one last node and then sort
    if (n-1) not in ind_conv: ind.append(n-1) 
    ind.sort() 

    # Avoid splitting after every single node
    flag = True
    while flag:
        flag = False
        for i in range(1,len(ind)-1):
            if ind[ i ]-ind[ i-1 ] == 1:
                if nodes[ ind[ i-1 ] ] != 'Conv':
                    ind.pop(i-1)
                    flag = True
                    break 
    ind = list(set(ind))
    ind.sort()
    
    split_on_ops2 = ['Relu','BatchNormalization','Reshape','Shape',
                    'Clip','Transpose','Slice','Conv']
    for sp in split_on_ops2:
        i0 = 0
        ind_plus = []
        for i in range(0,len(ind)):
            i1 = ind[ i ]+1
            if nodes[ i0:i1 ].count('Conv') > 1:
                flag = False
                for j in range(i0, i1):
                    if nodes[ j ] == 'Conv':
                        flag = True
                    if nodes[ j ] == sp and flag:
                        ind_plus.append(j)
                        flag = False
            elif i1-i0 >= 15: # split very large splits
                for j in range(i0, i1):
                    if nodes[ j ] == sp: 
                        ind_plus.append(j)          
            i0 = i1
        ind = list(set(ind + ind_plus))
        ind.sort()
    
    return ind

def autosplit_onnx(fmodel, sfx='_node'):
    import onnxread
    nodes, inputs, outputs, values = onnxread.extract_model(fmodel)
    ind = autosplit(nodes[ 'op_type' ].tolist(), nodes[ 'num_out' ].tolist())
    nms = [ nodes.iloc[ i ][ 'name' ] + sfx for i in ind ]
    return ' '.join(nms)

def split(ops, names, split_after_ops, split_after_names, split_ops, split_names, sfx='_node'):
    ind = []    
    n = len(ops)
    for j in range(n):
        if split_after_ops:
            if ops[ j ]+sfx in split_ops:
                ind.append(j)
        if split_after_names:  
            if names[ j ]+sfx in split_names:
                ind.append(j)
    if n-1 not in ind:
        ind.append(n-1)
    return ind
        
if False: # self-test
    cwd = '/home/nelli/onnx_model_zoo_cache'
    
    name = 'bvlcalexnet-9'
    fmodel = cwd + '/' + name + '/model.onnx'
    fmeta = cwd + '/' + name + '/' + name + '_modelmeta.json'
    
    import onnxread  
    nod, inp, outp, val = onnxread.extract_model(fmodel)
    ind = split(nod[ 'op_type' ].tolist(), nod[ 'name' ].tolist(), False, True, [], 
        ['Relu_1','MaxPool_3','Relu_5','MaxPool_7','Relu_9','Relu_11','Relu_13','MaxPool_14','Relu_17','Relu_20','Gemm_22'],
        sfx='')
    print('ind_good=%s' % str(ind))
    print('ind_auto=%s' % str(autosplit(nod[ 'op_type' ].tolist(), nod[ 'num_out' ].tolist())))
    print('nms_auto=%s' % str(autosplit_onnx(fmodel)))
