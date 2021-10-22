#!/bin/bash

###
# This script resets modification timestamps of committed files within the repo
# to the last time at which those files were modified in git's history.
# 
# Since gitlab's cached files are timestamped with the cache's creation date
# this allows Makefile's dependency checks (based on file modification time)
# to function properly even across pipelines if build files are cached
#
# Script to set modification times on files taken from this SO post:
# https://stackoverflow.com/a/30143117/1091729
###

git ls-files --recurse-submodules --full-name | while read FILE; do
  # cd into the subdirectory so we are within the relevant sub-repostiory
  cd "$(dirname "$FILE")"
  LOG_TIME="$(git log --pretty=format:%cd -n 1 --date=iso "$(basename "$FILE")")"
  cd - > /dev/null
  # Format the timestamp for use with 'touch'
  MTIME=`echo $LOG_TIME | sed 's/-//g;s/ //;s/://;s/:/\./;s/ .*//'`;
  touch -m -t $MTIME "$FILE"
  echo $FILE
done
