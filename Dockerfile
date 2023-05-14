FROM gcc:latest

RUN set -ex;                                                                    \
  apt-get update;                                                               \
  apt-get install -y cmake;                                                     \
  apt-get install -y gdb;                                                       \
  apt-get install vim