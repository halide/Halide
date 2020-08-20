import tensorflow as tf
import time

model = tf.keras.applications.MobileNetV2(input_shape=(224, 224, 3))
model = tf.keras.Model(model.input, [model.get_layer('block_1_project_BN').output])
x = tf.constant(0.5, shape=[256, 224, 224, 3])
y = model.predict(x)

num_iter = 20
min_time = 1e20
for i in range(num_iter):
    beg = time.time_ns()
    y = model.predict(x)
    end = time.time_ns()
    t = ((end - beg) / (10 ** 9))
    print('time:', t)
    min_time = min(t, min_time)
print('Tensorflow: {}s'.format(min_time))


