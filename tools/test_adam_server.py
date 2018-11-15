import socket, numpy, sys, struct

addr = sys.argv[1]
port = int(sys.argv[2])

def make_request(req, payload_id, payload):
    print("Connecting to", addr, port)
    print("Payload id, size:", payload_id, payload.size)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((addr, port))

    header = struct.pack("iiii", 7582946, req, payload_id, payload.size * 4)
    sock.send(header)
    
    if req == 0:
        response = sock.recv(payload.size * 4)
        if len(response) != payload.size * 4:
            print("Didn't get enough bytes:", len(response))
            exit(1)
        buf = numpy.frombuffer(response, dtype=numpy.float32, count=payload.size)
        payload[:] = buf
    else:
        sock.send(payload.tobytes())
            
    sock.close()

def get_weights(payload_id, payload):
    print("Getting weights for", payload_id)
    make_request(0, payload_id, payload)    
    
def set_weights(payload_id, payload):
    print("Setting weights for", payload_id)
    make_request(1, payload_id, payload)

def send_gradient(payload_id, payload):
    print("Sending gradient for", payload_id)
    make_request(2, payload_id, payload)


ids = [0, 3, 1, 4, 2]

w = {}

for id in ids:
    w[id] = numpy.random.uniform(0, 1, 4).astype(numpy.float32)
    set_weights(id, numpy.array(w[id]))

storage = numpy.array([0, 0, 0, 0], dtype=numpy.float32)
for id in ids:
    get_weights(id, storage)
    if not numpy.all(storage == w[id]):
        print("Unexpected weights:", storage)
    else:
        print("Correct weights received back:", storage)

for id in ids:
    send_gradient(id, numpy.array([1, -1, 1, -1], dtype=numpy.float32))
    get_weights(id, storage)
    if storage[0] >= w[id][0] or storage[1] <= w[id][1] or storage[2] >= w[id][2] or storage[3] <= w[id][3]:
        print("Unexpected weights after gradient update:", storage)
    else:
        print("Weights moved in expected direction:", storage)


print("Success!")
