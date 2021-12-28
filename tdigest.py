#!/usr/bin/env python-mr

import ctypes
import os
import sys

from _.command_line.app import APP
from _.command_line.flags import Flag
from _.repo.python.location import REPO_LOCATION

FLAG_compress = Flag.int("compression", short="c", default=100,
                        description="Compression factor to use.")

__lib = None
def _lib():
  global __lib
  if not __lib:
    # Use pydll because the tdigest library is very small.
    # and never queues io or anything (as we use it).
    __lib = ctypes.CDLL(os.path.join(REPO_LOCATION, "sketch", "t-digest", "t-digest.so"))
    __lib.td_alloc.restype = ctypes.c_bool
    __lib.td_alloc.argtypes = (ctypes.c_ulong, ctypes.c_void_p)

    __lib.td_free.restype = ctypes.c_bool
    __lib.td_free.argtypes = (ctypes.c_void_p,)

    __lib.td_addw.restype = ctypes.c_bool
    __lib.td_addw.argtypes = (ctypes.c_void_p, ctypes.c_double, ctypes.c_ulonglong)

    __lib.td_count.restype = ctypes.c_ulonglong
    __lib.td_count.argtypes = (ctypes.c_void_p,)

    __lib.td_percentile.restype = ctypes.c_double
    __lib.td_percentile.argtypes = (ctypes.c_void_p, ctypes.c_double)
  return __lib


class TDigest:
  __slots__ = ["__td_pointer"]
  def __init__(self, compression: int = 100):
    self.__td_pointer = ctypes.c_void_p(0)
    address = ctypes.c_void_p(ctypes.addressof(self.__td_pointer))
    result = _lib().td_alloc(compression, address)
    if not result:
      raise Exception("Unable to allocate tdigest object")
    elif not self.__td():
      raise Exception("tdigest library didn't set pointer")

  def __del__(self):
    if self.__td_pointer:
      _lib().td_free(self.__td())
      self.__td_pointer = None

  def __td(self):
    if self.__td_pointer:
      return self.__td_pointer
    else:
      raise Exception("underlying pointer was deallocated")

  def add(self, value: float, count:int=1) -> bool:
    return _lib().td_addw(self.__td(), value, count)

  def count(self) -> int:
    return _lib().td_count(self.__td())

  def percentile(self, p: float) -> float:
    if p < 0 or 1 < p:
      raise ValueError("Percentiles must be between 0 and 1.")
    return _lib().td_percentile(self.__td(), p)


def main(*percentiles):
  percentiles = [float(p) for p in percentiles]
  percentiles.sort()

  def _floats():
    for l in sys.stdin:
      try:
        yield float(l.strip())
      except ValueError:
        print("Bad line:", l, file=sys.stderr)


  t = TDigest(FLAG_compress.value)

  for f in _floats():
    t.add(f)

  count = t.count()
  for p in percentiles:
    print(p, "=", t.percentile(p / 100.0), "(", count * p / 100.0, ")")


def m():
  APP.run(main)


if __name__ == "__main__": m()
