#!/bin/sh
# A stand-in for the `claude` program, for cli_test.cc. It speaks just enough
# of the protocol to show that it was started and can be talked to:
#
#  - It begins by reporting the arguments it was given and the directory it
#    is in, where the real program would wait for a prompt in silence.
#  - It answers every line it reads with a result that counts the lines so
#    far.

arguments=""
for argument in "$@"; do
  arguments="$arguments\"$argument\","
done
printf '{"type":"fake_start","arguments":[%s],"directory":"%s"}\n' \
  "${arguments%,}" "$(pwd -P)"

heard=0
while read -r line; do
  heard=$((heard + 1))
  printf '{"type":"result","subtype":"success","result":"heard %d"}\n' "$heard"
done
