#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2020 ewhite42.
#
# This is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# This software is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this software; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.
#


import numpy
from gnuradio import gr
import pmt

class trackscan(gr.sync_block):
    """
    This block provides necessary parameters to the
    ATA control block to run a sequence of simple track
    scans.
    """
    def __init__(self, cfreq, ant_list, src_list, dur_list):
        gr.sync_block.__init__(self,
                               name="trackscan",
                               in_sig=None,
                               out_sig=None)
                               
        self.cfreq = cfreq #center frequency
        self.ant_list = [a.strip() for a in ant_list.split(',')] #antennas to observe with
        self.src_list = [s.strip() for s in src_list.split(',')] #list of source names
        self.dur_list = dur_list #list of scan durations, in seconds
        
        self.message_port_register_out(pmt.intern("command"))
        
        #set up dictionary of observing info which will be sent through
        #the message port  
                
        ant_key = pmt.intern("antennas_list")
        ant_val = pmt.intern(self.ant_list)

        freq_key = pmt.intern("freq")
        freq_val = pmt.from_double(self.cfreq)

        src_key = pmt.intern("source_list")
        src_val = pmt.intern(self.src_list)

        dur_key = pmt.intern("durations_list")
        dur_val = pmt.to_pmt(self.dur_list)

        command = pmt.make_dict()
        command = pmt.dict_add(command, ant_key, ant_val)
        command = pmt.dict_add(command, freq_key, freq_val)
        command = pmt.dict_add(command, src_key, src_val)
        command = pmt.dict_add(command, dur_key, dur_val)

        self.message_port_pub(pmt.intern("command"), command)

    def work(self, input_items, output_items):
        in0 = input_items[0]
        out = output_items[0]
        # <+signal processing here+>
        out[:] = in0
        return len(output_items[0])

