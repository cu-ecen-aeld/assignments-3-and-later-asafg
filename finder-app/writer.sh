#!/bin/bash

if [ $# != 2 ]
then
  echo "Parameter(s) are missing expected 2 parameters got $# parameters." 1>&2 
  exit 1
fi

writefile=$1
writestr=$2

basedir="$(dirname "${writefile}")"

if [ ! -d "${basedir}" ]
then
  mkdir -p "${basedir}" 2>/dev/null
  if [ $? != 0 ]
  then
    echo "Could not create directory \"${basedir}\"." 1>&2
    exit 1
  fi
fi
echo "${writestr}" > "${writefile}" 2>/dev/null
if [ $? != 0 ]
then
  echo "Could not create file \"${writefile}\"." 1>&2
  exit 1
fi
exit 0
