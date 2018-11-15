# Listens on a port, accepts weights and/or gradients, and sends weights.
# Serializes its state to disk periodically in case of crashes.
import socket, sys, numpy, os, os.path, struct, time

# Recover state from disk
if len(sys.argv) != 3:
    print("Usage: python adam_server.py port state_directory")
    exit(1)

port = int(sys.argv[1])
state_dir = sys.argv[2]

state = {}

class State:
    def __init__(self, w):
        self.smoothed_grad = numpy.zeros(w.size, dtype=numpy.float32)
        self.smoothed_grad_w = 0
        self.smoothed_second_moment = numpy.zeros(w.size, dtype=numpy.float32)
        self.smoothed_second_moment_w = 0
        self.weights = w
        
    def accept_grad(self, grad):
        self.smoothed_grad = 0.9 * self.smoothed_grad + 0.1 * grad
        self.smoothed_grad_w = 0.9 * self.smoothed_grad_w + 0.1 
        self.smoothed_second_moment = 0.999 * self.smoothed_second_moment + 0.001 * grad * grad
        self.smoothed_second_moment_w = 0.999 * self.smoothed_second_moment_w + 0.001
        alpha = 1.0 / self.smoothed_grad_w
        beta = 1.0 / self.smoothed_second_moment_w
        denom = numpy.sqrt(beta * self.smoothed_second_moment) + 1e-8
        step = (alpha * self.smoothed_grad) / denom                
        self.weights = self.weights - 0.001 * step


def recv(conn, sz):
    total = 0
    chunks = []
    while total < sz:
        next = conn.recv(sz - total)
        total += len(next)
        chunks.append(next)
    return ''.join(chunks)
        
MAGIC = 7582946

assert(os.path.isdir(state_dir))
print("Retrieving state from", state_dir)
for f in os.listdir(state_dir):
    # We expect a name of the form "some_path/state_%d"
    if "state_" not in f:
        print("Ignoring", f)
        continue
    id = int(f.split('_')[-1])
    with open(f, "rb") as fd:
        state[id] = State(numpy.frombuffer(fd.read(), dtype=numpy.float32))
    print("Found object of length", len(state[id].weights))
    
print("Binding to port ", port)
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.bind(('', port))
sock.listen(5)
bad_requests = 0
dirty = set()
last_serialization_time = time.time()
while True:
    (conn, addr) = sock.accept()

    raw_header = recv(conn, 16)
    if len(raw_header) != 16:
        print("Didn't get the right number of header bytes. Ignoring request")
        bad_requests += 1
        conn.close()
        continue
    
    (ver, req, payload_id, payload_len) = struct.unpack("iiii", raw_header)
    
    if ver != MAGIC + 0:
        print("Bad protocol version. Ignoring request")
        bad_requests += 1
        conn.close()
        continue

    print(addr, ver, req, payload_id, payload_len)
    
    if req == 0:
        # Request the weights        
        if payload_id not in state:
            print("Unknown payload id", payload_id)
            exit(1)

        conn.send(state[payload_id].weights.tobytes())
        
    elif req == 1:
        # Set the weights if they don't already exist
        if payload_len > 1 * 1024 * 1024:
            print("I refuse to track objects larger than 1MB")
            exit(1)

        if payload_id not in state and len(state) > 1000:
            print("I refuse to track more than 1000 objects")
            exit(1)

        if payload_len % 4 != 0:
            print("Payload lengths should be a multiple of 4")
            exit(1)
            
        if not payload_id in state:
            state[payload_id] = State(numpy.frombuffer(recv(conn, payload_len), dtype=numpy.float32))
            dirty.add(payload_id)
        else:
            # Thank you, but I don't need this data
            recv(conn, payload_len)
    elif req == 2:
        # Tell us about a gradient
        if payload_id not in state:
            print("Can't update gradients before setting weights for", payload_id)
            exit(1)

        if payload_len != state[payload_id].weights.size * 4:
            print("Bad gradient size", payload_len, "expected", state[payload_id].weights.size * 4)
            exit(1)

        grad = numpy.frombuffer(recv(conn, payload_len), dtype=numpy.float32)

        state[payload_id].accept_grad(grad)
        dirty.add(payload_id)
        
    else:
        print("Ignoring unknown request", req)
        bad_requests += 1

    if bad_requests >= 100:
        print("Over 100 bad requests. What is going on? Shutting down.")
        exit(1)
    
    conn.close()

    # Serialize dirty weights if it has been a while
    current_time = time.time()
    if current_time > last_serialization_time + 10 and dirty:
        print("Saving state to disk")
        for p in dirty:
            f = os.path.join(state_dir, "state_" + str(p))
            print("Saving", p, "to", f)
            with open(f, "wb") as fd:
                fd.write(state[p].weights.tobytes())
        last_serialization_time = current_time
        dirty = set()

