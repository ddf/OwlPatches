#!/bin/bash -li
patch_path="$(realpath -s "../Source")"
echo "Building with PATCHSOURCE=$patch_path"
cd "$OWL_SDK_PATH" || exit
make PLATFORM=OWL3 PATCHSOURCE="$patch_path" PATCHNAME="$1" clean patch