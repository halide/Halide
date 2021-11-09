#!/usr/bin/env python
# coding: utf-8

# In[ ]:


#   SAMMON  Perform Sammon mapping on a dataset, with weighted map axises. 
#
#                x = sammon(p) applies the Sammon nonlinear mapping algorithm on
#                multivariate data p, where each row represents a pattern and each 
#                column represents a feature.  On completion, x contains the 
#                corresponding coordinates of each point on the low-dimensional map.  
#                By default, a two-dimensional Sammon's map is created.  
#
#                Since Sammon's algorithm relies on pairwise distances between 
#                data points, preliminary feature scaling e.g., through normalization 
#                or standartization might be helpful. Also, even though RProp 
#                implementation works well with duplicate data samples it might 
#                be useful to remove them from p before applying Sammon's algorithm 
#                to avoid unnecessary computation.           
#
#   Function: 
#                x = sammon(p, alpha=[1,1], epochs=100, dnorm=1e-6, prnt=0, 
#                           x=None, dist_p=None)
#
#   Inputs:
#                p - npatterns-by-ndim data matrix, each row represents a pattern 
#                   (data sample) and each column represents a feature
#
#                alpha - list of nmap axis weights, 
#                   default: [1,1]
#
#                epochs - maximum number of RProp training epochs, 
#                   default: 100
#  
#                dnorm - Frobenius norm of dx / sqrt(npatterns*nmap) for stopping, 
#                   default: 1e-6
#
#                prnt - output frequency, in increments, prnt=0 suppresses print output,
#                   default: 0
#
#                x - initial Sammon projection of data e.g., obtained using PCA
#                    or in the previous run of Sammon's algorithm, default: None
#
#                dist_p - npatterns-by-npatterns matrix of pre-computed 
#                    pairwise distances between data samples in p, default: None
#
#   Outputs:
#                x - npatterns-by-nmap matrix with Sammon's embeddings of p
#
#   File         : sammon.py
#   Date         : 01 October 2021
#   Authors      : Nelli Fedorova (nellix.fedorova@intel.com)
#                : Based on MATLAB sammonsa.m, 21-Aug-1999, by S.A.^2, S.D., and T.M. 
#                  (Serge A. Terekhov, Serge A. Shumsky, 
#                  Svetlana Diyankova, Tatyana Muhamadieva)
#
#    Description : Python implementation of Sammon's non-linear mapping algorithm [1] 
#                  using RProp [2].
#
#    References  : [1] Sammon, John W. Jr., "A Nonlinear Mapping for Data
#                  Structure Analysis", IEEE Transactions on Computers,
#                  vol. C-18, no. 5, pp 401-409, May 1969.
#
#                  [2] Martin Riedmiller und Heinrich Braun: 
#                  Rprop - A Fast Adaptive Learning Algorithm. 
#                  Proceedings of the International Symposium on 
#                  Computer and Information Science VII, 1992

import numpy as np 
from scipy.spatial.distance import cdist

def sammon(p, alpha=[1,1], epochs=100, dnorm=1e-6, prnt=0, x=None, dist_p=None):
    # -- RProp constants
    delta_0 = 0.01
    delta_max = 50
    delta_min = 1e-6
    delta_inc = 1.2
    delta_dec = 0.5

    npatterns, ndim = p.shape
    alpha = np.array([alpha]) # -- row vector
    nmap = alpha.shape[1]

    # -- Initial mapping
    if x is None:
        if dist_p is None:
            if (nmap <= ndim):
                x = p[:,:nmap] 
            else: 
                x = np.dot(np.asmatrix(p[:,1]),np.ones((1,nmap)))
        else:
            from cmdscale import cmdscale
            x, e = cmdscale(dist_p)
            x = x[:,:nmap]     

    if npatterns > 1:
        # -- compute inter-point distances in high dimensions
        if dist_p is None:
            dist_p = cdist(p, p)         
        dist_p_norm = np.sqrt(np.sum(np.square(dist_p)))
        
        # -- alpha and alpha**2
        m_alpha1 = np.tile(alpha,(npatterns,1))
        m_alpha2 = np.tile(np.square(alpha),(npatterns,1))
        
        # -- compute inter-point distances in low dimensions
        ax = np.multiply(m_alpha1, x)
        dist_x = cdist(ax, ax) # -- weighted distance
                     
        # -- initialize deltas and gradients
        delta_x = np.full((npatterns, nmap), delta_0)
        grad_x = np.zeros((npatterns, nmap))
        grad_sign = np.zeros((npatterns, nmap))

        alldone = 0
        epoch = 0
        while not alldone:
            # -- Compute gradient of distance discrepancy 
            grad_x.fill(0)
            tmp = (dist_p - dist_x) / (np.multiply(dist_p, dist_x)+np.finfo(float).eps)   
            for ip in range(npatterns):   
                grad_x[ip,:] = np.dot(np.asmatrix(tmp[ip,:]), 
                                      np.multiply(m_alpha2, x[ip,:] - x)).flatten()
            grad_x = grad_x * (-4.0 / dist_p_norm)

            # -- Apply RProp algorithm
            wrk = np.multiply(grad_x, grad_sign)
            
            delta_x = ((wrk>0)*delta_inc + (wrk<0)*delta_dec + (wrk==0)) * delta_x
            delta_x = np.clip(delta_x, delta_min, delta_max)
            
            grad_sign.fill(0)
            grad_sign = grad_sign + ( (wrk>=0)&(grad_x>0) ) - ( (wrk>=0)&(grad_x<0) )

            dx = - np.multiply(delta_x, np.sign(grad_x))
            x = x + dx

            ax = np.multiply(m_alpha1, x)
            dist_x = cdist(ax, ax); # -- weighted distance

            # -- Stop iterations
            error = np.linalg.norm(dx) / np.sqrt(nmap*npatterns)
            # error = np.linalg.norm(dx) / (np.linalg.norm(x)+np.finfo(float).eps)
            epoch = epoch + 1
            if( prnt > 0):
                if epoch % prnt == 0:
                    print('SAMMON: %d F = %10.5g dX = %10.5g' % 
                          (epoch, 
                           np.sum(np.divide(np.square(dist_p - dist_x), 
                                            dist_p+np.finfo(float).eps)) / dist_p_norm, 
                           error))
            alldone = (epoch >= epochs) | (error <= dnorm);
        x = x - np.mean(x,axis=0)
    return x

if False: # self-test using iris data
    precompute_distances = True
    remove_duplicates = False
    
    # Load iris data
    from sklearn.datasets import load_iris
    iris = load_iris()

    if remove_duplicates: # remove duplicates
        (iris_x, iris_index) = np.unique(iris.data,axis=0,return_index=True)
        iris_target = iris.target[iris_index]
    else: # keep duplicates
        iris_x = iris.data
        iris_target = iris.target
    iris_names = iris.target_names
    
    # Build the Sammon projection
    if not precompute_distances: # let Sammon compute the distances 
        y = sammon(iris_x, [1,1], epochs=100, dnorm=1e-5, prnt=10, dist_p=None)
    else: # use pre-computed distances
        y = sammon(iris_x, [1,1], epochs=100, dnorm=1e-5, prnt=10, 
                   dist_p=cdist(iris_x,iris_x))

    # Plot
    import matplotlib.pyplot as plt
    get_ipython().run_line_magic('matplotlib', 'inline')
    plt.scatter(y[iris_target == 0, 0], y[iris_target == 0, 1], s=20, c='r', marker='o',label=iris_names[0])
    plt.scatter(y[iris_target == 1, 0], y[iris_target == 1, 1], s=20, c='b', marker='D',label=iris_names[1])
    plt.scatter(y[iris_target == 2, 0], y[iris_target == 2, 1], s=20, c='y', marker='v',label=iris_names[2])
    plt.title('Sammon projection of iris flower data')
    plt.legend(loc=2)
    plt.show()

