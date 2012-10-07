import boxblur_cumsum as boxblur
import sys

def filter_func(*args):
  return boxblur.boxblur_mode(*args, is_sat=True)

if __name__ == '__main__':
  boxblur.main(is_sat=True)

