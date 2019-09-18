#!/usr/bin/env bash
if [ "$1" == "" ]; then
  echo "usage: $0 PYTHON_VERSION"
  exit 1
fi
cd "$(dirname "$0")"
V=$1
TAG=minqlx:python$V
OUT_PATH=./docker/python$V
docker build . -t $TAG --build-arg=PYTHON_VERSION=$V
CID=$(docker create $TAG)
mkdir -p $OUT_PATH/bin
docker cp $CID:/minqlx/bin $OUT_PATH
docker rm -v $CID
