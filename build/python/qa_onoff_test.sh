#!/usr/bin/sh
export VOLK_GENERIC=1
export GR_DONT_LOAD_PREFS=1
export srcdir="/home/ewhite/src/gr-ata/python"
export GR_CONF_CONTROLPORT_ON=False
export PATH="/home/ewhite/src/gr-ata/build/python":"$PATH"
export LD_LIBRARY_PATH="":$LD_LIBRARY_PATH
export PYTHONPATH=/home/ewhite/src/gr-ata/build/swig:$PYTHONPATH
/usr/bin/python3 /home/ewhite/src/gr-ata/python/qa_onoff.py 
