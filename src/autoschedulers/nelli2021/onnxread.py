#!/usr/bin/env python
# coding: utf-8

# # Read model.onnx and modelmeta.json to dataframes
# 
# ONNX operators https://github.com/onnx/onnx/blob/master/docs/Operators.md
# 
# Metadata file contains the details about model input and output dimensions.
# 
# value_info stores input and output type and shape information of the inner-nodes. It is populated after inferring shapes.
# 
# Shape inference is not guaranteed to be complete. It works only with constants and simple variables. Reshape to a dynamically-provide shape and arithmetic expressions containing variables block shape inference.

# In[ ]:


import pandas as pd 
import json
import onnx 
from google.protobuf.json_format import MessageToDict

def extract_nodes(graph):  
    df = pd.DataFrame({'name':[''],'op_type':[''],'op_id':[0],
                'num_in':[0],'num_out':[0],'attr':['']}).drop(0)  

    for op_id, op in enumerate(graph.node):  
        name = op.name if op.name else op.op_type + "_" + str(op_id)
        d = {'name':name,'op_type':op.op_type,'op_id':op_id, 
            'num_in':len(op.input),'num_out':len(op.output),'attr':''}
        
        #attr = {'name','type','i','f','s','g','t','ints','floats','strings','graphs','tensors'}
        # Note that if you try to convert a field from op rather than entire op message,
        # you will get object has no attribute 'DESCRIPTOR exception':
        # da = proto.Message.to_json(op.attribute)
        da = MessageToDict(op)
        if 'attribute' in da.keys():
            for dd in da['attribute']: 
                # TODO: implement processing 'TENSOR', 'GRAPH', 'INTS', 'STRINGS'
                if dd['type'] == 'TENSOR': dd["t"]["rawData"] = ''
                if dd['type'] == 'GRAPH': dd["g"] = ''
                if dd['type'] == 'INTS' and dd['name'] == "cats_int64s":
                    dd["ints"] = [len(dd["ints"])] 
                if dd['type'] == 'STRINGS' and dd['name'] == "cats_strings":
                    dd["strings"] = [str(len(dd["strings"]))]  
                # Convert INT and INTS from strings to int
                if dd['type'] == 'INT':
                    dd["i"] = int(dd["i"])
                if dd['type'] == 'INTS':
                    dd["ints"] = [int(x) for x in dd["ints"]] 
    
            if op.op_type == 'Constant':
                dd = da['attribute'][0]
                dnew = [{'name': 'dataType', 'i': dd['t']['dataType'], 'type': 'INT'}]
                if 'dims' in dd['t']:
                    dnew += [{'name': 'dims', 'ints': [int(n) for n in dd['t']['dims']], 'type': 'INTS'}]
                da['attribute'] = dnew
                
            d['attr'] = da['attribute'] 
                
        for i, n in enumerate(op.input):
            d['in'+str(i)] = n
        for i, n in enumerate(op.output):
            d['out'+str(i)] = n
            
        df = df.append(d, ignore_index=True)
    return df

def extract_io(inouts):     
    df = pd.DataFrame({'name':[''],'tt':[0],'ts':[[]]}).drop(0)  
    for n in inouts:  
        dims = []
        for d in n.type.tensor_type.shape.dim:
            dims.append(d.dim_value)
        d = {'name':n.name, 
             'tt': n.type.tensor_type.elem_type,
             'ts': dims}
        df = df.append(d, ignore_index=True)
    return df

def extract_model(fmodel):
    # Read the model, check it and infer shapes
    model = onnx.load(fmodel)
    try: 
        onnx.checker.check_model(model)
    except onnx.checker.ValidationError as e:
        print('The model is invalid: %s' % e)
    model = onnx.shape_inference.infer_shapes(model)

    # Extract info
    nodes = extract_nodes(model.graph)
    inputs = extract_io(model.graph.input)
    outputs = extract_io(model.graph.output)
    values = extract_io(model.graph.value_info)   
         
    return nodes, inputs, outputs, values

def concat_string_columns(model, prefix='in'):
    cols_to_join = []
    for col in model.columns:
        if col.startswith(prefix):
            cols_to_join.append(col)
    if len(cols_to_join) == 0: return
    
    cols = model[cols_to_join].aggregate(lambda x: [x.tolist()], axis=0).map(lambda x:x[0]) 
    
    res = cols[0]
    for i in range(len(res)):
        v = res[i]
        if pd.notna(v):
            res[i] = [str(v)]
        else:
            res[i] = []
    
    for j in range(1,len(cols)):
        for i in range(len(res)):
            v = cols[j][i]
            if pd.notna(v):
                res[i].extend([str(v)])
    model[prefix] = res
    model.drop(columns=cols_to_join, inplace=True) 

def concat_columns(model, prefix='tt_in', num='num_in', bflatten=False):
    cols_to_join = []
    for col in model.columns:
        if col.startswith(prefix):
            cols_to_join.append(col)
    if len(cols_to_join) == 0: return
    
    cols = model[cols_to_join].aggregate(lambda x: [x.tolist()], axis=0).map(lambda x:x[0]) 
    
    res = cols[0]
    for i in range(len(res)):
        v = res[i]
        if not isinstance(v,(list,tuple)):
            if pd.notna(v):
                res[i] = [int(v)]
            else:
                res[i] = []
        else:
            v = [ int(x) for x in v ]
            res[i] = [v]
    
    nums = model[num].tolist()
    for j in range(1,len(cols)):
        for i in range(len(res)):
            v = cols[j][i]
            if not isinstance(v,(list,tuple)):
                if pd.notna(v):
                    res[i].extend([int(v)])
                elif j < nums[i]: # TODO: why some tt and ts are missing?
                    print('Warning: for row %d number of non-NaNs in columns %s* is less than %d defined in column %s' 
                          % (i, prefix, nums[i], num))
                    if bflatten: res[i] += [v] # append v=NaN
                    else: res[i].extend([[]]) # append []
            else:
                if bflatten: res[i] += list(v)
                else: res[i].extend([list(v)])  
    model[prefix] = res
    model.drop(columns=cols_to_join, inplace=True) 

def merge_model(nodes, inputs, outputs, values, drop_names='io', join_io=True):
    # Merge on names of inputs and outputs
    dfio = pd.concat([values, inputs, outputs], axis=0, sort=False)
    num_in = nodes["num_in"].max()
    for i in range(num_in):
        c, s = 'in'+str(i), '_in'+str(i)
        nodes = pd.merge(nodes, dfio, left_on=c, right_on='name',
                         suffixes=('',s),how='left')
    num_out = nodes["num_out"].max()
    for i in range(num_out):
        c, s = 'out'+str(i), '_out'+str(i)
        nodes = pd.merge(nodes, dfio, left_on=c, right_on='name',
                         suffixes=('',s),how='left')
    # Rename 1st merge for uniformity of names
    nodes.rename(columns={"tt": "tt_in0", "ts": "ts_in0"}, inplace=True)
    #print(nodes[['num_in','name_in0','in0','tt_in0','ts_in0']].head(5))

    # Drop node names
    if drop_names == 'all' or drop_names == 'io':
        cols_to_drop = ['op_id','name'] if drop_names == 'all' else []
        if drop_names == 'all':
            cols_to_drop.extend(['in'+str(i) for i in range(num_in)])
            cols_to_drop.extend(['out'+str(i) for i in range(num_out)])
        cols_to_drop.extend(['name_in'+str(i) for i in range(num_in)])
        cols_to_drop.extend(['name_out'+str(i) for i in range(num_out)])
                            
        nodes.drop(columns=cols_to_drop, inplace=True) 
    
    # Concatenate tt and ts columns and get rid of NaNs and floats
    if join_io:
        concat_columns(nodes, prefix='tt_in',  num="num_in",  bflatten=True)
        concat_columns(nodes, prefix='tt_out', num="num_out", bflatten=True)
        
        concat_columns(nodes, prefix='ts_in',  num="num_in",  bflatten=False)
        concat_columns(nodes, prefix='ts_out', num="num_out", bflatten=False)
        
        concat_string_columns(nodes, prefix='in')
        concat_string_columns(nodes, prefix='out')
       
    return nodes
  
if False: # self-test on one onnx file
    #fmodel = '/home/nelli/onnx_model_zoo_cache/bvlcalexnet-9/model.onnx'
    #fmeta = '/home/nelli/onnx_model_zoo_cache/bvlcalexnet-9/bvlcalexnet-9_modelmeta.json'
    fmodel = '/home/nelli/onnx_model_zoo_cache/MaskRCNN-10/mask_rcnn_R_50_FPN_1x.onnx'
    fmeta = '/home/nelli/onnx_model_zoo_cache/MaskRCNN-10/MaskRCNN-10_modelmeta.json'
    
    print('\nNODES -------------------')
    nodes, inputs, outputs, values = extract_model(fmodel)
    print(nodes.head(5))
    print(nodes.columns)

    print('\nMERGED -------------------')
    nodes = merge_model(nodes, inputs, outputs, values)
    print(nodes.head(5))
    print(nodes.columns)
    
    print('\nMERGED, NAMES DPROPPED -------------------')
    nodes, inputs, outputs, values = extract_model(fmodel)
    nodes = merge_model(nodes, inputs, outputs, values, 'all')
    print(nodes.head(5))
    print(nodes.columns)
    from hashable_df import hashable_df
    print('unique rows = %d' % len(hashable_df(nodes).drop_duplicates())) 
    
if False:
    cwd = '/home/nelli/Zoo/Workspace'

    # Read saved data from pickle files
    import pickle_data
    nodes, inputs, outputs, values, docs, df_meta = pickle_data.read_all(cwd=cwd)

    # Merge all
    merges = []
    for ind in [63]: #range(len(nodes)):
        if ind % 10 == 0:
            print('ind=%d' % ind)
        merge = merge_model(nodes[ind], inputs[ind], outputs[ind], values[ind], 'io')
        merges.append(merge)
    print(merges[0])
    
    #pickle_data.pickle_write(cwd+'/model_merges.data', merges)


# # Read ONNX Models from tar.gz files
# 
# Input parameter <b>tar_fnames</b>_ is the list of .tar.gz files to unzip and parse e.g. ['bvlcalexnet-9.tar.gz', 'candy-9.tar.gz']. If this list is empty <b>read_onnx_models</b> reads all ONNX models currently available: 66 ONNX Zoo models from <i>onnx_model_zoo_cache</i> and 26 converted models from <i>build_halogen/converted_models</i> (look for <i>tar_dirs</i> list in the code).
# 
# If some tar.gz file has not been unzipped yet, <b>read_onnx_models</b> unzips it to a temporary subdirectory of the current working directory specified by parameter <b>cwd</b>. This temporary subdirectory is then deleted.

# In[ ]:


import tarfile
import os, fnmatch, shutil

def read_onnx_models(tar_fnames=[], cwd='/home/nelli/Zoo/Workspace'):
    nodes = []
    inputs = []
    outputs = []
    values = []
    docs = []
    df_meta = pd.DataFrame({'name':[''],'version':[''],'timestamp':[''],'numpos':[3], 
                             'inshapes':[[]],'intypes':[[]],
                            'outshapes':[[]],'outtypes':[[]]}).drop(0)  

    tar_dirs = ['/home/nelli/onnx_model_zoo_cache', 
                '/home/nelli/build_halogen/converted_models']
    for tar_dir in tar_dirs:

        tar_files = fnmatch.filter(os.listdir(tar_dir), '*.tar.gz')
        tar_files.sort()
        for tar_cnt, tar_file in enumerate(tar_files):
            if len(tar_fnames) > 0 and (tar_file not in tar_fnames):
                continue
            
            sz = os.path.getsize(tar_dir + '/' + tar_file)
            if sz == 0: continue
            print("{: >16} {: <80}".format(*[sz,tar_file]))

            # Unzip tar and extract *.onnx and *modelmeta.json files
            # if they are not unpacked already
            tar_was_unpacked_already = True
            t = tarfile.open(tar_dir + '/' + tar_file, 'r')
            for member in t.getmembers():
                if ".onnx" in member.name:
                    fmodel = tar_dir + '/' + member.name # onnx file name
                    if not os.path.isfile(fmodel):
                        tar_was_unpacked_already = False
                        t.extract(member, cwd)
                        fmodel = cwd + '/' + member.name 
                    tsmodel = os.path.getmtime(fmodel)
                    dname = os.path.dirname(os.path.abspath(fmodel)) # onnx file directory
                if "modelmeta.json" in member.name:
                    fmeta = tar_dir + '/' + member.name # meta file name
                    if not os.path.isfile(fmeta):
                        tar_was_unpacked_already = False
                        t.extract(member, cwd)
                        fmeta = cwd + '/' + member.name
                    tsmeta = os.path.getmtime(fmeta)

            # Read onnx file
            nod, inp, outp, val = extract_model(fmodel)
            doc = ' '.join(nod['op_type'].tolist())

            nodes.append(nod)
            inputs.append(inp)
            values.append(val)  
            outputs.append(outp)
            docs.append(doc)

            # Read modelmeta.json file to df_meta
            with open(fmeta, 'rt') as fid:
                mf = json.load(fid)
            nm = list(mf["models"].keys())[0] 
            m = mf["models"][nm]
            # Extract substring between last / and .onnx
            # Ex: extract "mxnet_resnet50_v1_gluon" from
            # "./staged_models_work_tmp/mxnet_resnet50_v1_gluon/mxnet_resnet50_v1_gluon.onnx"
            name = nm.rsplit('/', 1)[-1][:-5]
            ts = int(tar_cnt + (tsmodel+tsmeta)/2) # unique timestamp
            df_meta = df_meta.append({'name':name,'version':'RAND','timestamp':ts,'numpos':m['numops'],
                'inshapes':m['inshapes'],'intypes':m['intypes'],
                'outshapes':m['outshapes'],'outtypes':m['outtypes']}, 
                ignore_index=True, sort=False)

            # Delete *.onnx and *modelmeta.json and their temporary parent directory
            if not tar_was_unpacked_already:
                try:
                    shutil.rmtree(dname)
                except OSError as e:
                    print("Error: %s : %s" % (dname, e.strerror))  
                    continue
    return nodes, inputs, outputs, values, docs, df_meta


if False: # self-test
    cwd = '/home/nelli/Zoo/Workspace'

    nodes, inputs, outputs, values, docs, df_meta = read_onnx_models(['bvlcalexnet-9.tar.gz'], cwd)
    
if False: # self-test
    import pickle_data
    cwd = '/home/nelli/Zoo/Workspace'
    
    nodes, inputs, outputs, values, docs, df_meta = read_onnx_models([], cwd)
    pickle_data.write_all(nodes, inputs, outputs, values, docs, df_meta, cwd=cwd)
    
    merges = []
    for ind in range(len(nodes)):
        if ind % 10 == 0:
            print('ind=%d' % ind)
        merge = merge_model(nodes[ind], inputs[ind], outputs[ind], values[ind], 'io')
        merges.append(merge)
    pickle_data.pickle_write(cwd+'/model_merges.data', merges)

