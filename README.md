# gr-ata - GNURadio Modules for Interfacing with the Allen Telescope Array

## Overview
gr-ata is a project to provide both get and set capabilities to the telescope array.  Frequency, azimuth, and elevation are supported along with an alternative ephemeral file.


## Installation

gr-ata requires gr-grnet to be installed as the stream block is a convenience wrapper around a gr-grnet UDP Source block.

gr-ata the control block requires mprpc to be installed first.  However it is available via pip (pip install mprpc).  NOTE: There is an incompatibility between running a client on python2 and a server on python3 due to the way each handles bytes.  Therefore if you are running GNURadio 3.7 or below, pip install mprpc.  If you are running GNURadio 3.8, pip3 install mprpc and edit the mprpc server to run in python3.

Otherwise the build process is the standard:

mkdir build

cd build

cmake ..

make install

