/* -*- c++ -*- */

#define ATA_API

%include "gnuradio.i"			// the common stuff

//load generated python docstrings
%include "ata_swig_doc.i"

%{
#include "ata/ata_stream.h"
%}


%include "ata/ata_stream.h"
GR_SWIG_BLOCK_MAGIC2(ata, ata_stream);
