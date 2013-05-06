_status_str = '''
%(bars)s
%(status)s
%(bars)s'''

def status(s):
    linelength = max([len(l) for l in s.splitlines()])
    bars = ''.join(['-' for i in range(linelength)])
    print _status_str % {'bars': bars, 'status': s}
