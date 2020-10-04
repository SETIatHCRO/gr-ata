/* -*- c++ -*- */

#define ATA_API

%include "gnuradio.i"           // the common stuff

//load generated python docstrings
%include "ata_swig_doc.i"

%{
#include "ata/snap_source.h"
%}

%include "ata/snap_source.h"
GR_SWIG_BLOCK_MAGIC2(ata, snap_source);
