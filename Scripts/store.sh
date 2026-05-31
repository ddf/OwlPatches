#!/bin/bash -li
patch_path="$(realpath -s "../Source")"
echo "Building with PATCHSOURCE=$patch_path"
cd "$OWL_SDK_PATH" || exit
make PLATFORM="$PLATFORM" PATCHSOURCE="$patch_path" PATCHNAME="$1" MODULE="$DEVICE" SLOT="$2" clean store