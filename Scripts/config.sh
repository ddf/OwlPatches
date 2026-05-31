#!/bin/bash -li
export DEVICE="$1"
PLATFORM=""
if [[ $DEVICE == "GENIUS" ]]; then
  export PLATFORM="OWL3"
elif [[ $DEVICE == "LICH" ]]; then
  export PLATFORM="OWL3"
elif [[ $DEVICE == "WITCH" ]]; then
  export PLATFORM="OWL2"
fi
echo "Config set to $DEVICE for $PLATFORM"


  
   