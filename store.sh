#!/bin/bash
cd ../OwlProgram
mingw32-make.exe PLATFORM=OWL3 PATCHSOURCE=../OwlPatches/OwlPatches PATCHNAME=$1 MODULE=$2 SLOT=$3 clean store