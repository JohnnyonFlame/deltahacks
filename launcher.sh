#!/bin/bash

echo "/" > /tmp/deltarune-launch-hack.lock
echo "-game data.win" >> /tmp/deltarune-launch-hack.lock
export RUNNER=$(realpath runner)
export DELTAHACKS=$(realpath deltahack.so)
export LIBS=$(realpath lib64)
chmod +x "$RUNNER"  

# Continue spawning runners while we have active spawn requests
while true; do
  {
    IFS=$'\n' read -d '' -r -a CMDS < /tmp/deltarune-launch-hack.lock
    rm -f /tmp/deltarune-launch-hack.lock
    cd ."${CMDS[0]}"
    [ -e runner ] || cp "$RUNNER" runner

    env \
      LD_PRELOAD="$LD_PRELOAD":"$DELTAHACKS" \
      LD_LIBRARY_PATH="$LD_LIBRARY_PATH":"$LIBS":/usr/lib/x86_64-linux-gnu/:/usr/lib64:"$HOME"/.local/share/Steam/ubuntu12_32/steam-runtime/lib/x86_64-linux-gnu/:"$HOME"/.local/share/Steam/ubuntu12_32/steam-runtime/usr/lib/x86_64-linux-gnu/ \
      ./runner ${CMDS[1]}
      # strace -e trace=openat,open,stat ./runner ${CMDS[1]}
  }

  if ! [ -e /tmp/deltarune-launch-hack.lock ]; then
    break
  fi
done
