import tensorflow as tf
import time

with tf.device('/device:cpu:0'):
    img = tf.random.uniform([4, 112, 112, 32])
    depthwise_filter = tf.random.uniform([3, 3, 32, 1])
    pointwise_filter = tf.random.uniform([1, 1, 32 * 1, 16])

    out = tf.nn.separable_conv2d(
        img, depthwise_filter, pointwise_filter,
        strides = (1, 1, 1, 1), padding = 'VALID')

    start = time.time()
    num_iter = 20
    for i in range(num_iter):
        out = tf.nn.separable_conv2d(
            img, depthwise_filter, pointwise_filter,
            strides = (1, 1, 1, 1), padding = 'VALID')
    end = time.time()
    print('time: {} ms'.format(1000 * (end - start) / num_iter))