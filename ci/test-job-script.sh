#!/usr/bin/env bash

if [ -z ${CI+x} ]; then
  echo "this script is only intended to run in CI" >&2
  exit 1
fi

set -e
set -o pipefail
set -u
set -x

ci/install-packages.sh

if [ -f /etc/os-release ]; then
  cat /etc/os-release
  . /etc/os-release
else
  ID=$( uname -s )
  # remove trailing text after actual version
  VERSION_ID=$( uname -r | sed "s/\([0-9\.]*\).*/\1/")
fi

# teach TCL how to find Graphviz packages
if [ "${build_system}" = "cmake" ]; then
  if [ "${ID}" = "Darwin" ]; then
    export TCLLIBPATH=/usr/local/lib/graphviz/tcl
  elif [ "${ID_LIKE:-}" = "debian" ]; then
    export TCLLIBPATH=/usr/lib/graphviz/tcl
  elif [ "${ID}" = "fedora" -o "${ID}" = "rocky" ]; then
    export TCLLIBPATH=/usr/lib64/graphviz/tcl
  fi
elif [ "${ID_LIKE:-}" = "debian" ]; then
  export TCLLIBPATH=/usr/lib/tcltk/graphviz/tcl
fi

python3 -m pytest -m "not slow" --junit-xml=report.xml ci/tests.py tests
