#!/bin/bash

# This script copies the binaries (partition table, bootloader and application) to the binaries folder.

cd $(dirname $0)

scriptDir=$(pwd)
binariesFolder=$scriptDir/binaries

rm -f $binariesFolder/*.bin
cp $scriptDir/build/bootloader/bootloader.bin $binariesFolder
cp $scriptDir/build/partition_table/partition-table.bin $binariesFolder
cp $scriptDir/build/esp32-assembler.bin $binariesFolder
