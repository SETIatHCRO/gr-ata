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

''' This script contains the functionality necessary to
    create a GNU Radio block to control subsets of the
    Allen Telescope Array.

    Ellie White 2 July 2020.
'''

import time
from datetime import datetime
#import numpy
from gnuradio import gr
import pmt
from astropy import units as u
from astropy.coordinates import EarthLocation
from astropy.coordinates import AltAz
from astropy.coordinates import SkyCoord
from astropy.time import Time as astrotime
from ATATools import ata_control as ac
from ATATools import ata_positions as ap

obs_info = {}

class control(gr.basic_block):
    """
    This block contains the necessary functions to initialize
    observations with the ATA, and point and track a subset of
    the antennas on a given source if commanded to do so.
    """
    def __init__(self, username):
        gr.basic_block.__init__(self,
                                name="control",
                                in_sig=None,
                                out_sig=None)
        
        self.hat_creek = EarthLocation(lat='40d49.05m', lon='-121d28.40m', height=986.0*u.m)
        self.pos = ap.ATAPositions()
        self.username = username
        alarm = ac.get_alarm()
        
        if alarm['user'] == self.username:
            self.message_port_register_in(pmt.intern("command"))
            self.set_msg_handler(pmt.intern("command"), self.handle_msg)
            
        else:
            raise Exception("Wrong username, you do not have permission to observe")

        
    def handle_msg(self, msg):
        ''' message handler function'''
        print("in message handler")
        self.obs_info = pmt.to_python(msg)
        
        self.cfreq = self.obs_info['freq']
        self.ant_list = [a.strip() for a in self.obs_info['antennas_list'].split(',')]
        self.coord_type = self.obs_info['coord_type']
         # this should be moved to track fn --list of scan durations, in seconds
        self.obs_type = self.obs_info["obs_type"]
        
        '''print("Source ID: {0}".format(self.src_list))
        print("RA: {0}, Dec: {1}".format(self.ra, self.dec))
        print("Az: {0}, El: {1}".format(self.az, self.el))'''
        
        self.begin()
        self.run()
        self.end_session()
        #self.work()
        
    def begin(self):
        ''' initialize the observation '''
        #try to reserve the antennas you want to observe with;
        #if it doesn't work you need to release the antennas,
        #then re-reserve them.
        try:
            ac.reserve_antennas(self.ant_list)

        except RuntimeError:
            print("Antennas were not released after last run. Releasing and reserving.")
            ac.release_antennas(self.ant_list, False)
            ac.reserve_antennas(self.ant_list)

        #check if the LNAs are on -- if not, turn them on
        ac.try_on_lnas(self.ant_list)

        #set the center frequency
        ac.set_freq(self.cfreq, self.ant_list)
        
        
    def run(self):
        ''' this function identifies what observation
            you are running and calls the appropriate
            observing function '''
        
        if self.obs_type == "track":
            self.track()
        
        elif self.obs_type == "onoff":
            self.on_off()
            
        else:
            print("Sorry, you didn't select an observation type. Ending session.")
            return
            
    def on_off(self):
        
        ''' run on-off observation '''
        
        self.az_off = self.obs_info["az_off"]       
        self.el_off = self.obs_info["el_off"]
        
        self.dur_list = self.obs_info["durations_list"]
        self.src = self.obs_info["source_list"]
        
        now = datetime.now()
        
        if self.coord_type == "id":
            src_ra, src_dec = ac.get_source_ra_dec(self.src)
                        
            if self.pos.isUp('radec', now, src_ra, src_dec):
                ac.create_ephems2(self.src, self.az_off, self.el_off)
                ac.point_ants2(self.src, 'on', self.ant_list)
            else:
                print("Source {0} is not up yet. Update source list and try again.".format(self.src))
                return
               
        else:
            print("Coordinates other than source ID haven't been implemented for on-off observations yet.")
            return
           
        ac.autotune(self.ant_list)
       
        time.sleep(self.dur_list[0])
        print("Done tracking on-source. Moving off-source...")
       
        ac.point_ants2(self.src, 'off', self.ant_list)
       
        time.sleep(self.dur_list[1])
        print("Done tracking off-source.")
        
    def track(self):
    
        ''' run a sequence of tracking observations '''
        
        #self.src = self.obs_info["source_list"] #this should be moved to track fn --[s.strip() for s in self.obs_info["source_list"].split(',')]
        '''self.ra = self.obs_info["ra"]
        self.dec = self.obs_info["dec"]
        self.az = self.obs_info["az"]
        self.el = self.obs_info["el"]'''
        self.dur_list = self.obs_info["durations_list"]
        
        dt_now = datetime.now()

        #point at first source and autotune
        if self.coord_type == "id":
        
            self.src = self.obs_info["source_list"]
            src_ra, src_dec = ac.get_source_ra_dec(self.src)
            if self.pos.isUp('radec', dt_now, src_ra, src_dec):
                ac.make_and_track_source(self.src, self.ant_list)
            else: 
                print("{0} is not up yet. Adjust your source list and try again.".format(self.src))
                return
            
        elif self.coord_type == "azel":
        
            self.az = self.obs_info["az"]
            self.el = self.obs_info["el"]
        
            #insert call to ata control fn
            if self.el > 23.0: #this is the min. elevation indicated in ata_positions
                ac.set_az_el(self.ant_list, self.az, self.el)
            else:
                print("Az: {0} and El: {1} is not up yet. Adjust your source list and try again.".format(self.az, self.el))
                return
                
        elif self.coord_type == "radec":
        
            self.ra = self.obs_info["ra"]
            self.dec = self.obs_info["dec"]
        
            now = astrotime.now()
            hcro_azel = AltAz(location=self.hat_creek, time=now)
            src = SkyCoord(ra=self.ra*u.deg, dec=self.dec*u.deg, frame='icrs')
            src_el = src.transform_to(hcro_azel).alt
            
            if src_el > 23.0:
                ac.make_and_track_ra_dec(self.ra, self.dec, self.ant_list)
            else:
                print("RA: {0} and Dec: {1} is not up yet. Adjust your source list and try again.".format(self.ra, self.dec))
                return
             
        else:
            print("No coordinate type specified!")
            return
            
        #run setup with Autotune
        ac.autotune(self.ant_list)
        
        #track on the first source for a given duration
        time.sleep(self.dur_list[0])
        print("Done tracking on {0}".format(self.src_list[0]))
        
        #track on remaining sources
        
        '''num = len(self.src_list) - 1

        for i in range(num):
        
            if self.coord_type == "id":
                ac.make_and_track_ephems(self.src_list[i+1], self.ant_list)
            
            elif self.coord_type == "azel":
                #insert call to ata control fn
                print("azel not yet implemented")
            
            elif self.coord_type == "radec":
                #insert fn call
                print("radec not implemented")
            
            else:
                print("No coordinate type specified!")
                return
                
            time.sleep(self.dur_list[i+1])
            print("Done tracking on {0}".format(self.src_list[i+1]))'''

    def set_freq(self, new_freq):
        '''reset the center frequency '''
        ac.set_freq(new_freq, self.ant_list)

    def get_eq_coords(self):
        ''' return current RA and Dec coordinates '''
        curr_radec = ac.getRaDec(self.ant_list)
        return curr_radec
        
    def end_session(self):
        ''' release antennas at the end of a session '''
        ac.release_antennas(self.ant_list, True)
        print("Antennas have been released and stowed.")


    def work(self):
        '''run this function to cleanly exit the session'''
        
        print("Exiting session")
        return -1
