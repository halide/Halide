import re

def split_doublequote(s):
    L = re.compile(r'''((?:"[^"]*")+)''').split(s)
    ans = []
    for x in L:
        if x.startswith('"'):
            ans.append(x)
        else:
            ans.extend(x.split())
    return ans
 
