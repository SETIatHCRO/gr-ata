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
import socket

obs_info = {}

class control(gr.basic_block):
    """
    This block contains the necessary functions to initialize
    observations with the ATA, and point and track a subset of
    the antennas on a given source if commanded to do so.
    """
    def __init__(self, username, mode):
        gr.basic_block.__init__(self,
                                name="ATA Control",
                                in_sig=None,
                                out_sig=None)
        
        self.hat_creek = EarthLocation(lat='40d49.05m', lon='-121d28.40m', height=986.0*u.m)
        self.pos = ap.ATAPositions()
        self.mode = mode
        
        #run the flowgraph either online or offline
        if mode == 'online':
        
            if socket.gethostname() == 'gnuradio1':
                try: 
                    alarm = ac.get_alarm()
        
                    if alarm['user'] == username:
                        self.is_user = True
                        print("You are the primary user. You have full permissions.")
                        
                    else:
                        self.is_user = False
                        print("Another user, {0}, has the array locked out. \
                               \nYou do not have permission to change the LO \
                               frequency.".format(alarm['user']))
                except KeyError:
                    self.is_user = False
                    print("The array is not locked out under your username.\
                           \n You do not have permission to change the LO \
                           frequency.")
        
                self.message_port_register_in(pmt.intern("command"))
                self.set_msg_handler(pmt.intern("command"), self.handle_msg)
                
            else:
                exception_msg = '''Sorry, you must run this flowgraph on the ATA machine
                gnuradio1 if you want to observe. If you want to test the
                code on your local computer without observing, switch to
                Offline Mode.'''
                raise Exception(exception_msg)
                
        elif mode == 'offline':
            self.message_port_register_in(pmt.intern("command"))
            self.set_msg_handler(pmt.intern("command"), self.handle_msg_offline)
        else:
            print("Error: No Control Block Mode specified! Select mode and try again.")  
        
        
    def handle_msg(self, msg):
    
        ''' message handler function'''
        
        self.obs_info = pmt.to_python(msg)
        
        cfreq = self.obs_info['freq']
        ant_list = [a.strip() for a in self.obs_info['antennas_list'].split(',')]
        self.ant_list = ant_list
        
        ## Reserve your antennas ##
        
        if ant_list:
            my_ants = self.reserve(ant_list)
        else:
            print("No antennas specified. Provide an antenna list and try again.")
            
        ## Set the center frequency (if you have permission), and/or  ##
        ## set the feed focus frequency, and turn on the LNAs         ##
        
        if cfreq and my_ants:
            if self.is_user:
                ac.try_on_lnas(my_ants)
                ac.set_freq(cfreq, my_ants)
            else:
                ac.try_on_lnas(my_ants)
                ac.set_freq_focus(cfreq, my_ants)
                
        elif cfreq and not my_ants:
            print('No antenna list specified. You must specify \
                   one or more antennas in order to observe.')
                   
        elif my_ants and not cfreq:
            print('No center frequency specified. You must specify \
                   a center frequency in order to observe.')
                
        else:
            print("You have not specified an antennas list or \
                   a frequency setting. Please specify an antennas \
                   list and the center frequency, then try again.")
                   
        ## Run observation ##
        
        if 'source_id' in self.obs_info:
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:
                self.point_src_id(self.obs_info['source_id'], ant_list, True,  
                                  self.obs_info['az_off'], self.obs_info['el_off'])
            #if not, observe on-source
            else:
                self.point_src_id(self.obs_info['source_id'], ant_list)
            
        elif ('ra' and 'dec') in self.obs_info:
            #run ra / dec observation
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:
                self.point_src_id(self.obs_info['ra'], self.obs_info['dec'], 
                                  ant_list, True, self.obs_info['az_off'],  
                                  self.obs_info['el_off'])
            #if not, observe on-source
            else:
                ac.make_and_track_ra_dec(self.obs_info['ra'], self.obs_info['dec'], ant_list)
                #self.point_src_id(self.obs_info['ra'], self.obs_info['dec'], ant_list)
            
        elif ('az' and 'el') in self.obs_info:
            #run az / el observation
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:
                self.point_src_azel(self.obs_info['az'], self.obs_info['el'], 
                                  ant_list, True, self.obs_info['az_off'], 
                                  self.obs_info['el_off'])
            #if not, observe on-source
            else:
                self.point_src_azel(self.obs_info['az'], self.obs_info['el'], ant_list)
            
        else:
            print("You have not specified a source location. \
                   Please specify a target source and try again.")
        
        #self.run()
        #self.end_session()
        
    def handle_msg_offline(self, msg):
    
        ''' offline message handler function'''
        
        self.obs_info = pmt.to_python(msg)

        cfreq = self.obs_info['freq']
        ant_list = [a.strip() for a in self.obs_info['antennas_list'].split(',')]
        
        ## Reserve your antennas ##
        
        if ant_list:
            print("Antennas: {0} have been reserved".format(ant_list))
        else:
            print("No antennas specified. Provide an antenna list and try again.")
            
        ## Set the center frequency (if you have permission), and/or  ##
        ## set the feed focus frequency, and turn on the LNAs         ##
        
        if cfreq and ant_list:
            print("LNAs have been turned on.")
            print("LO and focus frequency has been set to {0}".format(cfreq))
                
        elif cfreq and not ant_list:
            print('No antenna list specified. You must specify \
                   one or more antennas in order to observe.')
                   
        elif ant_list and not cfreq:
            print('No center frequency specified. You must specify \
                   a center frequency in order to observe.')
                
        else:
            print("You have not specified an antennas list or \
                   a frequency setting. Please specify an antennas \
                   list and the center frequency, then try again.")
                   
        ## Run dummy observation ##
        
        if 'source_id' in self.obs_info:
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:
                print("Slewing off source {0} by offsets: \
                az_off = {1} and el_off = {2}.".format(self.obs_info['source_id'], 
                self.obs_info['az_off'], self.obs_info['el_off']))
                
            #if not, observe on-source
            else:
                print("Slewing on-source to {0}.".format(self.obs_info['source_id']))
            
        elif ('ra' and 'dec') in self.obs_info:
            #run ra / dec observation
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:      
                print("Slewing off source RA: {0}\tDec: {1} by offsets: \
                       az_off = {2} and el_off = {3}.".format(self.obs_info['ra'], 
                       self.obs_info['dec'], self.obs_info['az_off'],
                       self.obs_info['el_off']))
            #if not, observe on-source
            else:
                print("Slewing on-source to RA: {0}\tDec: {1}.".format(
                       self.obs_info['ra'], self.obs_info['dec']))
            
        elif ('az' and 'el') in self.obs_info:
            #run az / el observation
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:
                print("Slewing off source Az: {0}\tEl: {1} by offsets: \
                       az_off = {2} and el_off = {3}.".format(self.obs_info['az'], 
                       self.obs_info['el'], self.obs_info['az_off'],
                       self.obs_info['el_off']))
            #if not, observe on-source
            else:
                print("Slewing on-source to Az: {0}\tEl: {1}.".format(
                       self.obs_info['az'], self.obs_info['el']))
            
        else:
            print("You have not specified a source location. \
                   Please specify a target source and try again.")
        
        print("Observing complete")
        return
        
    def reserve(self, ant_list, force=True):
    
        ''' This function checks if the antennas you have requested are 
            available, and reserves them if so. If you set force=True, 
            (not recommended) then it releases and reserves antennas 
            without first checking that they are not reserved by someone
            else. '''
    
        if not force:
            my_ants = list(set(ant_list) & set(ac.list_released_antennas()))
        
            if my_ants:
                ac.reserve_antennas(my_ants)
                print("Antennas: {0} have been reserved.".format(my_ants))
                return my_ants
            else: 
                print("None of the antennas you requested are available. \
                       Please request non-reserved antennas and try again.") 
                return my_ants
                   
        else:
            try:
                ac.reserve_antennas(ant_list) 
                return ant_list              
            except RuntimeError:
                print("Antennas were not released after last run. Releasing and reserving.")
                ac.release_antennas(ant_list, False)
                ac.reserve_antennas(ant_list)
                return ant_list
                
    def point_src_id(self, src_id, ant_list, offsource=False, az_off=0, el_off=0):
    
        ''' This function points the antenna at the source
            designated by the given ID from the ATA catalog. '''
    
        now = datetime.now()
        
        src_ra, src_dec = ac.get_source_ra_dec(src_id)
                        
        if self.pos.isUp('radec', now, src_ra, src_dec):
            ac.create_ephems2(src_id, az_off, el_off)
            if not offsource:
                ac.point_ants2(src_id, 'on', ant_list)          
            else:
                ac.point_ants2(src_id, 'off', ant_list)
        else:
            print("Source {0} is not up yet. Update source list \
                 and try again.".format(src_id))
            return

    def point_src_radec(self, ra, dec, ant_list, offsource=False, az_off=0, el_off=0):
    
        ''' This function points the antenna at the source
            designated by the right ascension and declination
            provided by the user. '''
    
        now = datetime.now()
                                
        if self.pos.isUp('radec', now, ra, dec):
            ac.create_ephems2(src_id, az_off, el_off)
            if not offsource:
                ac.point_ants2(src_id, 'on', ant_list)          
            else:
                ac.point_ants2(src_id, 'off', ant_list)
        else:
            print("Source {0} is not up yet. Update source list \
                 and try again.".format(src_id))
            return
        
    def point_src_azel(self, az, el, ant_list, az_off=0, el_off=0):
    
        ''' This function points the antenna at the source
            designated by the azimuth and elevation specified
            by the user. '''
                                
        if el > ap.MIN_ELEV:
            ac.set_az_el(ant_list, az+az_off, el+el_off)
            return
        else:
            print("Sorry, {0} is below the minimum allowed elevation of {1}. Reset your coordinates and try again.".format(el, ap.MIN_ELEV))
            return
                   
    def stop(self):
        print("The session has ended.")
        if self.mode == 'online':
            ac.release_antennas(self.ant_list, True)
            print("Releasing and stowing antennas.")
        return True
                           
    #### Old functions ####    
        
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
        
        self.dur = self.obs_info["dur"]
        self.src = self.obs_info["source_id"]
        
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
       
        time.sleep(self.dur)
        print("Done tracking on-source. Moving off-source...")
       
        ac.point_ants2(self.src, 'off', self.ant_list)
       
        time.sleep(self.dur)
        print("Done tracking off-source.")
        
    def track(self):
    
        ''' run a sequence of tracking observations '''
        
        #self.src = self.obs_info["source_list"] #this should be moved to track fn --[s.strip() for s in self.obs_info["source_list"].split(',')]
        '''self.ra = self.obs_info["ra"]
        self.dec = self.obs_info["dec"]
        self.az = self.obs_info["az"]
        self.el = self.obs_info["el"]'''
        self.dur = self.obs_info["dur"]
        
        dt_now = datetime.now()

        #point at first source and autotune
        if self.coord_type == "id":
        
            self.src = self.obs_info["source_id"]
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
        time.sleep(self.dur)
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
        

