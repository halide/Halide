import tensorflow as tf
import time

with tf.device('/GPU:0'):

    img = tf.random.uniform([4, 112, 112, 32])
    depthwise_filter = tf.random.uniform([3, 3, 32, 1])
    pointwise_filter = tf.random.uniform([1, 1, 32 * 1, 16])
    
    best = None
    num_trials = 10
    num_iter = 10
    for j in range(num_trials):
        start = time.time()
        for i in range(num_iter):
            out = tf.nn.separable_conv2d(
                img, depthwise_filter, pointwise_filter,
                strides = (1, 1, 1, 1), padding = 'VALID')
        end = time.time()
        t = (end - start) / num_iter
        if not best or t < best: best = t
    print('time: {} ms'.format(1000 * best))
