#!/usr/bin/env python
#
# Copyright 2017 Ettus Research, a National Instruments Company
# Copyright 2019 Derek Kozel <derek@bitstovolts.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

import copy
import logging
from multiprocessing import Process
from gevent.server import StreamServer
from gevent.pool import Pool
from gevent import signal
from mprpc import RPCServer
import ATA_Interface 

ATA_COMPAT_NUM = (1, 0)
DEFAULT_ATA_RPC_PORT = 49601

class ATAServer(RPCServer):
    """
    Main ATA RPC Class which translates RPC class to approprate calls to the native command tools
    """
    
    # This is a list of methods in this class which require a claim
    default_methods = ['init']
    
    def __init__(self, default_args):
        logging.basicConfig(level=logging.DEBUG)
        logging.info("Launching RPC server with compat num %d.%d",
                ATA_COMPAT_NUM[0], ATA_COMPAT_NUM[1])
     
        self.methods = copy.copy(self.default_methods)
        self._init_rpc_calls()
        super(ATAServer, self).__init__(
                pack_params={'use_bin_type': True},
                )

        logging.info("RPC server ready!")

    def _init_rpc_calls(self):
        logging.info("Initializing RPC functions")

    def _add_command(self, function, command):
        """
        Add a command to the RPC interface.
        """
        logging.debug("Adding command %s pointing to %s", command, function)
        def new_function(*args):
                " Define a function"
                try:
                    return function(*args)
                except Exception as ex:
                    logging.error(
                        "Uncaught exception in method %s :%s\n %s ",
                        command, str(ex), traceback.format_exc()
                        )
                    self._last_error = str(ex)
                    raise
        new_function.__doc__ = function.__doc__
        setattr(self, command, new_function)


    ###########################################################################
    # Diagnostics and introspection
    ###########################################################################
    def list_methods(self):
        """
        Returns a list of tuples: (method_name, docstring, is claim required)

        Every tuple represents one call that's available over RPC.
        """
        return [
            (
                method,
                getattr(self, method).__doc__,
                method in self.methods
            )
            for method in dir(self)
            if not method.startswith('_') \
                    and callable(getattr(self, method))
        ]

    def ping(self, data=None):
        """
        Take in data as argument and send it back
        This is a safe method which can be called without a claim on the device
        """
        self.log.debug("I was pinged from: %s:%s", self.client_host, self.client_port)
        return data


    ###########################################################################
    # Status queries
    ###########################################################################
    def get_ata_compat_num(self):
        """Get the ATA compatibility number"""
        return ATA_COMPAT_NUM

    def get_last_error(self):
        """
        Return the 'last error' string, which gets set when RPC calls fail.
        """
        return self._last_error
    
    ###########################################################################
    # Session initialization
    ###########################################################################
    def init(self, token, args):
        """
        Initialize control system. 
        """
        logging.debug("Initialized session")
        return 

###############################################################################
# Process control
###############################################################################
def _rpc_server_process(port, default_args):
    """
    This is the actual process that's running the RPC server.
    """
    connections = Pool(1000)
    server = StreamServer(
        ('0.0.0.0', port),
        handle=ATAServer(default_args),
        spawn=connections)
    # catch signals and stop the stream server
    signal(signal.SIGTERM, lambda *args: server.stop())
    signal(signal.SIGINT, lambda *args: server.stop())
    server.serve_forever()


def spawn_rpc_process(udp_port, default_args):
    """
    Returns a process that contains the RPC server
    """
    proc = Process(
        target=_rpc_server_process,
        args=[udp_port, default_args],
    )
    proc.start()
    return proc

if __name__ == "__main__":
    spawn_rpc_process(DEFAULT_ATA_RPC_PORT, [])

