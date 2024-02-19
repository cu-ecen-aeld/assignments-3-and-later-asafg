#!/bin/bash

if [ $# != 2 ]
then
  echo "Parameter(s) are missing expected 2 parameters got $# parameters." 1>&2 
  exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d "${filesdir}" ]
then
  echo "Directory (\"${filesdir}\") does not exist." 1>&2 
  exit 1
fi

num_files="$(grep -rl "${searchstr}" "${filesdir}" | wc -l)"
matched_lines="$(grep -rch "${searchstr}" "${filesdir}" | awk '{s+=$1}END{print s}')"

echo "The number of files are ${num_files} and the number of matching lines are ${matched_lines}"
