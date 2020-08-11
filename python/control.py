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

import subprocess
from datetime import datetime
from gnuradio import gr
import pmt
from astropy import units as u
from astropy.coordinates import EarthLocation
from ATATools import ata_control as ac
from ATATools import ata_positions as ap

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

        self.hat_creek = EarthLocation(lat='40d49.05m', lon='-121d28.40m', height=986.0*u.meter)
        self.pos = ap.ATAPositions()
        self.mode = mode
        self.obs_info = {}
        self.ant_list = []
        self.my_ants = []
        self.is_configured = False

        #run the flowgraph either online or offline
        if mode == 'online':

            try_ping = subprocess.Popen(['ping', '-c', '1', 'control'], 
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT)
            stdout, stderr = try_ping.communicate()
            ping_output = stdout.decode('utf-8')

            ping_success = "1 packets transmitted, 1 received"

            if ping_success in ping_output:
                try:
                    alarm = ac.get_alarm()

                    if alarm['user'] == username:
                        self.is_user = True
                        print("You are the primary user. You have full permissions.")

                    else:
                        self.is_user = False
                        raise Exception("Another user, {0}, has the array locked out. \n"
                                        "You do not have permission to observe.".format(alarm['user']))
                except KeyError:
                    self.is_user = False
                    raise Exception("The array is not locked out under your username.\n"
                                    "You do not have permission to observe.")

                self.message_port_register_in(pmt.intern("command"))
                self.set_msg_handler(pmt.intern("command"), self.handle_msg)

            else:
                exception_msg = "Sorry, you must be able to connect to the ATA\n"\
                                "machine. if you want to observe. If you want to test\n"\
                                "the code on your local computer without observing, \n"\
                                "switch to Offline Mode."
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
        self.my_ants = ant_list

        ## Set the center frequency (if you have permission), and/or  ##
        ## set the feed focus frequency, and turn on the LNAs         ##
        if not self.is_configured:
            if cfreq and self.my_ants:
                if self.is_user:
                    ac.try_on_lnas(self.my_ants)
                    ac.set_freq(cfreq, self.my_ants)
                else:
                    ac.try_on_lnas(self.my_ants)
                    ac.set_freq_focus(cfreq, self.my_ants)

            elif cfreq and not self.my_ants:
                print("No antenna list specified. You must specify \n"
                      "one or more antennas in order to observe.")

            elif self.my_ants and not cfreq:
                print("No center frequency specified. You must specify \n"
                      "a center frequency in order to observe.")

            else:
                print("You have not specified an antennas list or \n"
                      "a frequency setting. Please specify an antennas \n"
                      "list and the center frequency, then try again.")

        ## Run observation ##

        if 'source_id' in self.obs_info:
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:
                self.point_src_id(self.obs_info['source_id'], self.my_ants, True,
                                  self.obs_info['az_off'], self.obs_info['el_off'])
            #if not, observe on-source
            else:
                self.point_src_id(self.obs_info['source_id'], self.my_ants)

        elif ('ra' and 'dec') in self.obs_info:
            #run ra / dec observation
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:
                self.point_src_radec(self.obs_info['ra'], self.obs_info['dec'], \
                                     self.my_ants, True, self.obs_info['az_off'], \
                                     self.obs_info['el_off'])
            #if not, observe on-source
            else:
                self.point_src_radec(self.obs_info['ra'], self.obs_info['dec'], \
                                     self.my_ants)

        elif ('az' and 'el') in self.obs_info:
            #run az / el observation
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:
                self.point_src_azel(self.obs_info['az'], self.obs_info['el'], \
                                    self.my_ants, self.obs_info['az_off'], \
                                    self.obs_info['el_off'])
            #if not, observe on-source
            else:
                self.point_src_azel(self.obs_info['az'], self.obs_info['el'], self.my_ants)

        else:
            print("You have not specified a source location. \n"
                   "Please specify a target source and try again.")

        self.is_configured = True

    def handle_msg_offline(self, msg):

        ''' offline message handler function'''

        self.obs_info = pmt.to_python(msg)

        cfreq = self.obs_info['freq']
        ant_list = [a.strip() for a in self.obs_info['antennas_list'].split(',')]

        if not self.is_configured:
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
                print("Autotune has been run.")

            elif cfreq and not ant_list:
                print("No antenna list specified. You must specify \n"
                      "one or more antennas in order to observe.")

            elif ant_list and not cfreq:
                print("No center frequency specified. You must specify\n"
                      "a center frequency in order to observe.")

            else:
                print("You have not specified an antennas list or\n"
                      "a frequency setting. Please specify an antennas\n"
                      "list and the center frequency, then try again.")

        ## Run dummy observation ##

        if 'source_id' in self.obs_info:
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:
                print("Slewing off source {0} by offsets: \n"
                       "az_off = {1} and el_off = {2}.\n".format(self.obs_info['source_id'], \
                       self.obs_info['az_off'], self.obs_info['el_off']))

            #if not, observe on-source
            else:
                print("\nSlewing on-source to {0}.\n".format(self.obs_info['source_id']))

        elif ('ra' and 'dec') in self.obs_info:
            #run ra / dec observation
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:
                print("Slewing off source RA: {0}, Dec: {1} by offsets: \n"
                       "az_off = {2} and el_off = {3}.\n".format(self.obs_info['ra'], \
                       self.obs_info['dec'], self.obs_info['az_off'], \
                       self.obs_info['el_off']))
            #if not, observe on-source
            else:
                print("\nSlewing on-source to RA: {0}, Dec: {1}.\n".format( \
                       self.obs_info['ra'], self.obs_info['dec']))

        elif ('az' and 'el') in self.obs_info:
            #run az / el observation
            #First, check if this is an off-source scan
            if ('az_off' and 'el_off') in self.obs_info:
                print("Slewing off source Az: {0}, El: {1} by offsets: \n"
                       "az_off = {2} and el_off = {3}.\n".format(self.obs_info['az'], \
                       self.obs_info['el'], self.obs_info['az_off'], \
                       self.obs_info['el_off']))
            #if not, observe on-source
            else:
                print("\nSlewing on-source to Az: {0}\tEl: {1}.\n".format( \
                       self.obs_info['az'], self.obs_info['el']))

        else:
            print("You have not specified a source location. \n"
                  "Please specify a target source and try again.")

        self.is_configured = True

    def point_src_id(self, src_id, ant_list, offsource=False, az_off=0, el_off=0):

        ''' This function points the antenna at the source
            designated by the given ID from the ATA catalog. '''

        now = datetime.now()
        src_ra, src_dec = ac.get_source_ra_dec(src_id)

        if self.pos.isUp('radec', now, src_ra, src_dec):
            ac.create_ephems2(src_id, az_off, el_off)
            if not offsource:
                ac.point_ants2(src_id, 'on', ant_list)
                #if not self.is_configured:
                    #ac.autotune(ant_list)
                    #print("\nAutotuned.\n")
            else:
                ac.point_ants2(src_id, 'off', ant_list)
                #if not self.is_configured: 
                    #ac.autotune(ant_list)
                    #print("\nAutotuned.\n")
        else:
            print("Source {0} is not up yet. Update source list and try again.".format(src_id))
            return

    def point_src_radec(self, ra, dec, ant_list, offsource=False, az_off=0, el_off=0):

        ''' This function points the antenna at the source
            designated by the right ascension and declination
            provided by the user. '''

        now = datetime.now()

        if self.pos.isUp('radec', now, ra, dec):
            ac.create_ephems2_radec(ra, dec, az_off, el_off)
            if not offsource:
                ac.point_ants2_radec(ra, dec, 'on', ant_list)
                #if not self.is_configured:
                    #ac.autotune(ant_list)
                    #print("\nAutotuned.\n")
            else:
                ac.point_ants2_radec(ra, dec, 'off', ant_list)
                #if not self.is_configured:
                    #ac.autotune(ant_list)
                    #print("\nAutotuned.\n")
        else:
            print("RA: {0}, Dec: {1} is not up yet. Update source list and try again.".format(ra, dec))
            return

    def point_src_azel(self, az, el, ant_list, az_off=0, el_off=0):

        ''' This function points the antenna at the source
            designated by the azimuth and elevation specified
            by the user. '''

        if el > ap.MIN_ELEV:
            ac.set_az_el(ant_list, az+az_off, el+el_off)
            #if not self.is_configured:
                    #ac.autotune(ant_list)
                    #print("\nAutotuned.\n")
            return

        print("Sorry, {0} is below the minimum allowed elevation \n"
               "of {1}. Reset your coordinates and try again.".format( \
               el, ap.MIN_ELEV))
        return

    def stop(self):
        print("The session has ended. Stowing antenna(s)")
        ac.set_az_el(self.my_ants, 0.00, 18.00)
        return True
