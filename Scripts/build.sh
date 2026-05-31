#!/bin/bash -li
patch_path="$(realpath -s "../Source")"
echo "Building with SDK=$OWL_SDK_PATH and PATCHSOURCE=$patch_path for $DEVICE running $PLATFORM"
cd "$OWL_SDK_PATH" || exit
make PLATFORM="$PLATFORM" PATCHSOURCE="$patch_path" PATCHNAME="$1" MODULE="$DEVICE" clean patch