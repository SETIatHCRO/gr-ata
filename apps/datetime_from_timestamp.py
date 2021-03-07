#!/usr/bin/env python3

import argparse
from datetime import datetime

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Timestamp to datetime converter')
    parser.add_argument('--synctime', type=int, help="Integer timestamp timestamp to convert.  Format will look something like this: 1612923335", required=True)

    args = parser.parse_args()

    t = datetime.fromtimestamp(args.synctime)
    print(str(t))
