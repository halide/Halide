#!/usr/bin/env python
# coding: utf-8

# # Save/load data to/from pickle files

import pickle
import pandas as pd
        
def pickle_dump(fpath, data):
    with open(fpath, 'wb') as fid:
        pickle.dump(data, fid)

def pickle_load(fpath):
    with open(fpath, 'rb') as fid:
        data = pickle.load(fid)
    return data

def pickle_write(fpath, data):
    with open(fpath, 'wb') as fid:
        pickle.dump(data, fid)
    print('%d items saved to %s' % (len(data), fpath))

def pickle_read(fpath):
    with open(fpath, 'rb') as fid:
        data = pickle.load(fid)
    print('%d items loaded from %s' % (len(data), fpath))
    return data

def write_all(nodes, inputs, outputs, values, docs, df_meta, cwd='/home/nelli/Zoo/Workspace'):
    pickle_write(cwd+'/model_nodes.data', nodes)
    pickle_write(cwd+'/model_inputs.data', inputs)
    pickle_write(cwd+'/model_outputs.data', outputs)
    pickle_write(cwd+'/model_values.data', values)
    pickle_write(cwd+'/model_docs.data', docs)

    df_meta.to_pickle(cwd+'/model_meta.data')
    df_meta.to_csv(cwd+'/model_meta.csv', index=False)

def read_all(cwd='/home/nelli/Zoo/Workspace'):
    nodes = pickle_read(cwd+'/model_nodes.data')
    inputs = pickle_read(cwd+'/model_inputs.data')
    outputs = pickle_read(cwd+'/model_outputs.data')
    values = pickle_read(cwd+'/model_values.data')
    docs = pickle_read(cwd+'/model_docs.data')

    df_meta = pd.read_pickle(cwd+'/model_meta.data')
    
    return nodes, inputs, outputs, values, docs, df_meta

