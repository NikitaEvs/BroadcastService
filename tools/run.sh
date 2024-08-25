#!/bin/bash

# Change the current working directory to the location of the present file
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )" 

ret=0
exec 3>&1; $("$DIR"/../build/da_proc "$@" >&3); ret=$?; exec 3>&-

exit $ret
