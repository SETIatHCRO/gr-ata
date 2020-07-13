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

#command = pmt.make_dict()

class trackscan(gr.sync_block):
    """
    This block provides necessary parameters to the
    ATA control block to run a sequence of simple track
    scans.
    """
    def __init__(self, cfreq, ant_list, coord_type, dur_list): #src_list="no source", ra=14.00, dec=67.00, az=0.00, el=18.00):
    
        global command
    
        gr.sync_block.__init__(self,
                               name="trackscan",
                               in_sig=None,
                               out_sig=None)
                               
        self.cfreq = cfreq #center frequency
        self.ant_list = ant_list #antennas to observe with
        #self.src_list = src_list #list of source names
        self.dur_list = dur_list #list of scan durations, in seconds
        self.coord_type = coord_type #how coordinate of source is specified
        '''self.ra = ra
        self.dec = dec
        self.az = az
        self.el = el'''
        
        self.message_port_register_out(pmt.intern("command"))
        
        #set up dictionary of observing info which will be sent through
        #the message port  
                
        obs_key = pmt.intern("obs_type")
        obs_val = pmt.intern("track")
                
        ant_key = pmt.intern("antennas_list")
        ant_val = pmt.intern(self.ant_list)

        freq_key = pmt.intern("freq")
        freq_val = pmt.from_double(self.cfreq)
        
        coord_key = pmt.intern("coord_type")
        coord_val = pmt.intern(self.coord_type)
        
        dur_key = pmt.intern("durations_list")
        dur_val = pmt.to_pmt(self.dur_list)

        command = pmt.make_dict()
        command = pmt.dict_add(command, ant_key, ant_val)
        command = pmt.dict_add(command, freq_key, freq_val)
        command = pmt.dict_add(command, dur_key, dur_val)
        command = pmt.dict_add(command, obs_key, obs_val)
        command = pmt.dict_add(command, coord_key, coord_val)
        
        self.command = command
        
    def set_sources(self, src_list):

        src_key = pmt.intern("source_list")
        src_val = pmt.intern(self.src_list)
        self.command = pmt.dict_add(self.command, src_key, src_val)
        print(command)
            
    def set_src_radec(self, ra, dec):
        ra_key = pmt.intern("ra")
        ra_val = pmt.from_double(self.ra)
            
        dec_key = pmt.intern("dec")
        dec_val = pmt.from_double(self.dec)
            
        self.command = pmt.dict_add(self.command, ra_key, ra_val)
        self.command = pmt.dict_add(self.command, dec_key, dec_val)
        
    def set_src_azel(self, az, el):
        az_key = pmt.intern("az")
        az_val = pmt.from_double(self.az)
            
        el_key = pmt.intern("el")
        el_val = pmt.from_double(self.el)
            
        self.command = pmt.dict_add(self.command, az_key, az_val)
        self.command = pmt.dict_add(self.command, el_key, el_val)
        
        #print(command)
        
    def start(self):
        ''' publish the observation info to the output message port '''
        #global command
        self.message_port_pub(pmt.intern("command"), self.command)
        return super().start()
