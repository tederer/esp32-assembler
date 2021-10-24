#!/bin/bash

projectRootInHost=/home/developer/esp32/esp32-assembler/
projectRootInContainer=/root/esp/esp32-assembler/
deviceForFlashing=/dev/ttyUSB0

if [ -e $deviceForFlashing ]; then
   device=--device=$deviceForFlashing
else
   device=""
   echo
   echo "WARNING: $deviceForFlashing does not exist -> starting development environment but flashing will not be possible!"
   echo
fi

sudo docker run -it --env="PROJECT_ROOT=$projectRootInContainer" --volume=$projectRootInHost:$projectRootInContainer:rw $device tederer/esp32dev
