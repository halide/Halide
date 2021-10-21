#!/usr/bin/env python3

from http.server import HTTPServer, SimpleHTTPRequestHandler
import sys

class MyRequestHandler(SimpleHTTPRequestHandler):
    
    def end_headers(self):
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        return super(MyRequestHandler, self).end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()

host = sys.argv[1] if len(sys.argv) > 2 else '127.0.0.1'
port = int(sys.argv[len(sys.argv)-1]) if len(sys.argv) > 1 else 8080

print("Listening on {}:{}".format(host, port))
httpd = HTTPServer((host, port), MyRequestHandler)
httpd.serve_forever()
