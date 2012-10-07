import boxblur
import sys

def filter_func(*args):
  return boxblur.boxblur_sat(*args)

if __name__ == '__main__':
  sys.argv = ['boxblur_sat.py', 'sat']
  boxblur.main()

