# gr-ata - GNURadio Modules for Interfacing with the Allen Telescope Array

## Overview
gr-ata is a project to provide both get and set capabilities to the telescope array.  Frequency, azimuth, and elevation are supported along with an alternative ephemeral file.


## Installation

gr-ata requires mprpc to be installed first.  However it is available via pip (pip install mprpc).

Otherwise the build process is the standard:

mkdir build

cd build

cmake ..

make install

