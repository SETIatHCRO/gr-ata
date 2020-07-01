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
from ATATools import ata_control as ac
import time


class control(gr.basic_block):
    """
    This block contains the necessary functions to initialize 
    observations with the ATA, and point and track a subset of 
    the antennas on a given source if commanded to do so. 
    """
    def __init__(self, cfreq, ant_list, src_list, dur_list):
        gr.basic_block.__init__(self,
            name="control",
            in_sig=None,
            out_sig=None)
            
        self.cfreq = cfreq #center frequency
        self.ant_list = ant_list #list of antennas to observe with
        self.src_list = src_list #list of source names
        self.dur_list = dur_list #list of scan durations, in seconds
        
        #try to reserve the antennas you want to observe with;
        #if it doesn't work you need to release the antennas, 
        #then re-reserve them.
        try:
            ac.reserve_antennas(self.ant_list)
            
        except:
            ac.release_antennas(self.ant_list)
            ac.reserve_antennas(self.ant_list)
            
        #check if the LNAs are on -- if not, turn them on
        ac.try_on_lnas(self.ant_list)
       
        #run setup with Autotune
        ac.autotune(self.ant_list)    
       
        #set the center frequency
        ac.set_freq(self.cfreq, self.ant_list) 
       
    def point_and_track(self, src, dur):
        
        ''' Tells the antenna to point and track on a given target '''
        
        #create ephemeris file that tells the antenna
        #where it should be pointing at each timestamp
        ac.make_and_track_ephems(src, self.ant_list)
        
        #stay on source for given duration
        time.sleep(dur)
        
    def setFreq(self, new_freq):
    
        '''reset the center frequency '''
        
        ac. set_freq(new_freq, self.ant_list)
        
    def getEqCoords(self):
    
        ''' return current RA and Dec coordinates '''
        curr_radec = ac.getRaDec(self.ant_list)
        return curr_radec
        
    def endSession(self):
    
        ''' release antennas at the end of a session '''
    
        ac.release_antennas(self.ant_list, True)

    def forecast(self, noutput_items, ninput_items_required):
        #setup size of input_items[i] for work call
        for i in range(len(ninput_items_required)):
            ninput_items_required[i] = noutput_items

    def general_work(self, input_items, output_items):
        output_items[0][:] = input_items[0]
        consume(0, len(input_items[0]))        #self.consume_each(len(input_items[0]))
        return len(output_items[0])
