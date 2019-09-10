#!/usr/bin/env python
# -*- coding: utf-8 -*-
# 
# Copyright 2019 ghostop14.
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

from gnuradio import gr
import threading
import pmt
from time import sleep
from mprpc import RPCClient
import sys

class ata_poller(threading.Thread):
  def __init__(self, caller, host, port, polling_sec, tuning, antennaname, rpcclient, verbose):
    threading.Thread.__init__(self)
    self.host = host
    self.port = port
    self.verbose = verbose
    self.polling_sec = polling_sec
    self.caller = caller
    self.tuning = tuning
    self.rpcclient = rpcclient
    self.antennaname = antennaname
    
    self.stopThread = False

  def run(self):
    while not self.stopThread:
      # Insert get status code here
      # -----------------------------
      try:
        freq = self.rpcclient.call('get_sky_freq',self.tuning)
      except Exception as e:
        print("[ATA Control] Error getting frequency: %s" % str(e))
          
      # This is the thread sleep code.
      # breaking it down prevents the thread from seemingly 'hanging' on stopThread signal until sleep period expires.
      i=0
      maxCycles = self.polling_sec /0.2
      while (not self.stopThread) and (i<maxCycles):
        sleep(0.2)
        i += 1
        sleep(0.2)

class atacontrol(gr.basic_block):
  """
  docstring for block atacontrol
  """
  def __init__(self, host, port, polling_sec, tuning, antennaname, verbose):
    gr.basic_block.__init__(self, name="ATA Control", in_sig=None, out_sig=None)

    # Init block variables
    self.host = host
    self.port = port
    self.polling_sec = polling_sec
    self.verbose = verbose
    self.tuning = tuning
    self.antennaname = antennaname
    
    # Init RPC client
    try:
      params={'use_bin_type':True}
      self.client = RPCClient(host, port, pack_params=params)
    except Exception as e:
      print("[ATA Control] Error starting RPC client: %s" % str(e))
      sys.exit(1)
    
    # Init thread
    self.thread = ata_poller(self, host, port, polling_sec, tuning, antennaname, self.client, verbose)
    self.thread.start()

    self.message_port_register_in(pmt.intern("control"))
    self.set_msg_handler(pmt.intern("control"), self.controlHandler)   
    
    self.message_port_register_out(pmt.intern("freq"))
    self.message_port_register_out(pmt.intern("status"))

  def controlHandler(self, pdu):
    meta = pmt.to_python(pmt.car(pdu))
    
    try:
      az = float(meta['az'])
    except:
      az = None
      
    try:
      el = float(meta['el'])
    except:
      el = None
      
    try:
      freq = float(meta['freq'])
    except:
      freq = None
      
    try:
      ephemname = float(meta['ephem'])
    except:
      ephemname = None
     
     
    # Insert send command code here
    # -----------------------------
    
  def stop(self):
    self.thread.stopThread = True

    return True

  def setFreq(self,freq):
    try:
      freq = self.rpcclient.call('set_sky_freq',[self.tuning,freq])
    except Exception as e:
      print("[ATA Control] Error setting frequency: %s" % str(e))
      
  def setAzEl(self,antennaname,az,el):
    try:
      freq = self.rpcclient.call('set_sky_freq',[antennaname,az,el])
    except Exception as e:
      print("[ATA Control] Error setting frequency: %s" % str(e))
      
  def sendFreq(self,freq):
    p = pmt.from_float(freq)
    self.message_port_pub(pmt.intern("freq"),pmt.cons(pmt.intern("freq"),p))
    
  def sendState(self,freq,az,el):
    meta = {}  
    meta['freq'] = freq
    meta['az'] = az
    meta['el'] = el
    meta['antenna'] = self.antenna
    
    self.message_port_pub(pmt.intern("status"),pmt.cons( pmt.to_pmt(meta), pmt.PMT_NIL ))
    
  def forecast(self, noutput_items, ninput_items_required):
    #setup size of input_items[i] for work call
    for i in range(len(ninput_items_required)):
      ninput_items_required[i] = noutput_items

  def general_work(self, input_items, output_items):
    output_items[0][:] = input_items[0]
    consume(0, len(input_items[0]))
    #self.consume_each(len(input_items[0]))
    return len(output_items[0])
