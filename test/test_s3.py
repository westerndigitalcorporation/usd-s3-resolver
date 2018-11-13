"""
Test USD asset resolver

Author: Stefaan Ghysels
"""

import time
import sys

from pxr import Usd

def main():
    """Test USD asset resolver"""
    if len(sys.argv) < 2:
        print "Usage: test_s3.py asset.usd"
        sys.exit(0)
    elif len(sys.argv) == 2:
        delay = 0
    else:
        delay = float(sys.argv[2])

    print "Open stage"
    start = time.time()
    stage = Usd.Stage.Open(sys.argv[1]) #pylint: disable=no-member
    print "Opened stage in", time.time() - start

    time.sleep(delay)

    print "Reload stage"
    start = time.time()
    stage.Reload()
    print "Reloaded stage in", time.time() - start

if __name__ == "__main__":
    main()
