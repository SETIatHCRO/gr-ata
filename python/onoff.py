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
import time
from gnuradio import gr

class onoff(gr.sync_block):
    """
    docstring for block onoff
    """
    
    def __init__(self, cfreq, ant_list, coord_type, dur, az_off, el_off): #src_list="no source", ra=14.00, dec=67.00, az=0.00, el=18.00):
    
        global command
    
        gr.sync_block.__init__(self,
                               name="onoff",
                               in_sig=None,
                               out_sig=None)
        
        self.message_port_register_out(pmt.intern("command"))
        self.dur = dur
        self.az_off = az_off
        self.el_off = el_off
        
        #set up dictionary of observing info which will be sent through
        #the message port  
                
        obs_key = pmt.intern("obs_type")
        obs_val = pmt.intern("onoff")
                
        ant_key = pmt.intern("antennas_list")
        ant_val = pmt.intern(ant_list)

        freq_key = pmt.intern("freq")
        freq_val = pmt.from_double(cfreq)
        
        coord_key = pmt.intern("coord_type")
        coord_val = pmt.intern(coord_type)
        
        dur_key = pmt.intern("dur")
        dur_val = pmt.to_pmt(dur)
        
        azoff_key = pmt.intern("az_off")
        azoff_val = pmt.from_double(az_off)
        
        eloff_key = pmt.intern("el_off")
        eloff_val = pmt.from_double(el_off)

        command = pmt.make_dict()
        command = pmt.dict_add(command, ant_key, ant_val)
        command = pmt.dict_add(command, freq_key, freq_val)
        command = pmt.dict_add(command, dur_key, dur_val)
        command = pmt.dict_add(command, obs_key, obs_val)
        command = pmt.dict_add(command, coord_key, coord_val)
        
        self.command = command
        
    def set_source(self, src):

        ''' This function sets the source's 
            identifier string '''

        src_key = pmt.intern("source_id")
        src_val = pmt.intern(src)
        self.command = pmt.dict_add(self.command, src_key, src_val)
            
    def set_src_radec(self, ra, dec):
    
        ''' This function sets the target source's 
            right ascension and declination '''
    
        ra_key = pmt.intern("ra")
        ra_val = pmt.from_double(ra)
            
        dec_key = pmt.intern("dec")
        dec_val = pmt.from_double(dec)
            
        self.command = pmt.dict_add(self.command, ra_key, ra_val)
        self.command = pmt.dict_add(self.command, dec_key, dec_val)
        
    def set_src_azel(self, az, el):
    
        ''' This function sets the target source's 
            azimuth and elevation '''
    
        az_key = pmt.intern("az")
        az_val = pmt.from_double(az)
            
        el_key = pmt.intern("el")
        el_val = pmt.from_double(el)
            
        self.command = pmt.dict_add(self.command, az_key, az_val)
        self.command = pmt.dict_add(self.command, el_key, el_val)
            
    def start(self):
        ''' publish the observation info to the output message port '''
        
        #this command tells the control block to point
        # on-source for the given duration
        self.message_port_pub(pmt.intern("command"), self.command)
        
        time.sleep(self.dur)
        
        #this command will instruct the command block to point off 
        #source by the given offsets
        self.command = pmt.dict_add(self.command, azoff_key, azoff_val)
        self.command = pmt.dict_add(self.command, eloff_key, eloff_val)
        
        self.message_port_pub(pmt.intern("command"), self.command)
        
        return super().start()
