#!/bin/sh
set -eu
"$1" --version | grep -F 'NBL 0.4.0 (GNU nano 9.3)'
"$1" --help | grep -F 'Usage: nbl'
