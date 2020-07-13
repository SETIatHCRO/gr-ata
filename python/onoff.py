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
import pmt
from gnuradio import gr

class onoff(gr.sync_block):
    """
    docstring for block onoff
    """
    
    def __init__(self, cfreq, ant_list, coord_type, src_list, dur_list, az_off, el_off): #src_list="no source", ra=14.00, dec=67.00, az=0.00, el=18.00):
    
        global command
    
        gr.sync_block.__init__(self,
                               name="onoff",
                               in_sig=None,
                               out_sig=None)
                               
        self.cfreq = cfreq #center frequency
        self.ant_list = ant_list #antennas to observe with
        self.src_list = src_list #list of source names
        self.dur_list = dur_list #list of scan durations, in seconds
        self.coord_type = coord_type #how coordinate of source is specified
        self.az_off = az_off
        self.el_off = el_off
        
        self.message_port_register_out(pmt.intern("command"))
        
        #set up dictionary of observing info which will be sent through
        #the message port  
                
        obs_key = pmt.intern("obs_type")
        obs_val = pmt.intern("onoff")
                
        ant_key = pmt.intern("antennas_list")
        ant_val = pmt.intern(self.ant_list)

        freq_key = pmt.intern("freq")
        freq_val = pmt.from_double(self.cfreq)
        
        coord_key = pmt.intern("coord_type")
        coord_val = pmt.intern(self.coord_type)
        
        dur_key = pmt.intern("durations_list")
        dur_val = pmt.to_pmt(self.dur_list)
        
        src_key = pmt.intern("source_list")
        src_val = pmt.intern(self.src_list)
        
        azoff_key = pmt.intern("az_off")
        azoff_val = pmt.from_double(self.az_off)
        
        eloff_key = pmt.intern("el_off")
        eloff_val = pmt.from_double(self.el_off)

        command = pmt.make_dict()
        command = pmt.dict_add(command, ant_key, ant_val)
        command = pmt.dict_add(command, freq_key, freq_val)
        command = pmt.dict_add(command, dur_key, dur_val)
        command = pmt.dict_add(command, obs_key, obs_val)
        command = pmt.dict_add(command, coord_key, coord_val)
        command = pmt.dict_add(command, src_key, src_val)
        command = pmt.dict_add(command, azoff_key, azoff_val)
        command = pmt.dict_add(command, eloff_key, eloff_val)
        
        self.command = command
        
    def start(self):
        ''' publish the observation info to the output message port '''
        #global command
        self.message_port_pub(pmt.intern("command"), self.command)
        return super().start()
