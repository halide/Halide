import boxblur
import sys

def filter_func(*args):
  return boxblur.boxblur_cumsum(*args)

if __name__ == '__main__':
  sys.argv = ['boxblur_cumsum.py', 'cumsum']
  boxblur.main()

