# HIPDelta

This tool can be used to copy assets that differ between two .HIP/.HOP files to a "merge" file for use with [Heavy Mod Manager](https://github.com/igorseabra4/HeavyModManager). This file is to be added to the "Merge HIP Files" section when editing your mod.

The output file will include "dummy" TEXT assets to replace any asset that was removed.

Currently, only GameCube files will be saved correctly.

## Usage
```
HIPDelta original.HIP modified.HIP [output.HIP]
```
