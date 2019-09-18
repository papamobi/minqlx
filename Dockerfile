ARG PYTHON_VERSION

FROM python:$PYTHON_VERSION-stretch

COPY . /minqlx

WORKDIR /minqlx

RUN make clean

RUN make
