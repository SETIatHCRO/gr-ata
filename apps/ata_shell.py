#!/usr/bin/env python3
#
# Copyright 2017 Ettus Research, a National Instruments Company
# Copyright 2019 Derek Kozel <derek@bitstovolts.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

# Code adapted from UHD's MPM Shell

import cmd
import time
import argparse
import threading
from mprpc import RPCClient
from mprpc.exceptions import RPCError

DEFAULT_ATA_RPC_PORT = 49601

def parse_args():
    """
    Parse command line args.
    """
    parser = argparse.ArgumentParser(
        description="ATA Control Shell",
    )
    parser.add_argument(
        'host',
        help="Specify host to connect to.", default=None,
    )
    parser.add_argument(
        '-p', '--port', type=int,
        help="Specify port to connect to.", default=DEFAULT_ATA_RPC_PORT,
    )
#    parser.add_argument(
#        '-c', '--claim',
#        action='store_true',
#        help="Claim device after connecting."
#    )
#    parser.add_argument(
#        '-j', '--hijack', type=str,
#        help="Hijack running session (excludes --claim)."
#    )
    return parser.parse_args()


def split_args(args, *default_args):
    " Returns an array of args, space-separated "
    args = args.split()
    return [
        arg_val if arg_idx < len(args) else default_args[arg_idx]
        for arg_idx, arg_val in enumerate(args)
    ]

class ATAShell(cmd.Cmd):
    """
    RPC Shell class. See cmd module.
    """
    def __init__(self, host, port):
        cmd.Cmd.__init__(self)
        self.prompt = "> "
        self.client = None
        self.remote_methods = []
        self._host = host
        self._port = port
        if host is not None:
            self.connect(host, port)
        self.update_prompt()

    def _add_command(self, command, docs, requires_token=False):
        """
        Add a command to the current session
        """
        cmd_name = 'do_' + command
        if not hasattr(self, cmd_name):
            new_command = lambda args: self.rpc_template(
                str(command), requires_token, args
            )
            new_command.__doc__ = docs
            setattr(self, cmd_name, new_command)
            self.remote_methods.append(command)

    def rpc_template(self, command, requires_token, args=None):
        """
        Template function to create new RPC shell commands
        """
        try:
            if args:
                expanded_args = self.expand_args(args)
                response = self.client.call(command, *expanded_args)
            else:
                response = self.client.call(command)
        except RPCError as ex:
            print("RPC Command failed!")
            print("Error: {}".format(ex))
            return
        except Exception as ex:
            print("Unexpected exception!")
            print("Error: {}".format(ex))
            return
        if isinstance(response, bool):
            if response:
                print("Command executed successfully!")
            else:
                print("Command failed!")
        else:
            print("==> " + str(response))
        return response

    def get_names(self):
        " We need this for tab completion. "
        return dir(self)

    ###########################################################################
    # Cmd module specific
    ###########################################################################
    def run(self):
        " Go, go, go! "
        try:
            self.cmdloop()
        except KeyboardInterrupt:
            self.do_disconnect(None)
            exit(0)

    def postcmd(self, stop, line):
        """
        Is run after every command executes. Does:
        - Update prompt
        """
        self.update_prompt()

    ###########################################################################
    # Internal methods
    ###########################################################################
    def connect(self, host, port):
        """
        Launch a connection.
        """
        print("Attempting to connect to {host}:{port}...".format(
            host=host, port=port
        ))
        try:
            self.client = RPCClient(host, port, pack_params={'use_bin_type': True})
            print("Connection successful.")
        except Exception as ex:
            print("Connection refused")
            print("Error: {}".format(ex))
            return False
        self._host = host
        self._port = port
        print("Getting methods...")
        methods = self.client.call('list_methods')
        for method in methods:
            self._add_command(*method)
        print("Added {} methods.".format(len(methods)))
        return True

    def disconnect(self):
        """
        Clean up after a connection was closed.
        """
        if self.client:
            try:
                self.client.close()
            except RPCError as ex:
                print("Error while closing the connection")
                print("Error: {}".format(ex))
        for method in self.remote_methods:
            delattr(self, "do_" + method)
        self.remote_methods = []
        self.client = None
        self._host = None
        self._port = None

    def update_prompt(self):
        """
        Update prompt
        """
        self.prompt = '> '
#        else:
#            if self._claimer is None:
#                claim_status = ''
#            elif isinstance(self._claimer, ATAClaimer):
#                claim_status = ' [C]'
#            elif isinstance(self._claimer, ATAHijacker):
#                claim_status = ' [H]'
#            self.prompt = '{dev_id}{claim_status}> '.format(
#                dev_id=self._device_info.get(
#                    'name', self._device_info.get('serial', '?')
#                ),
#                claim_status=claim_status,
#            )

    def expand_args(self, args):
        """
        Takes a string and returns a list
        """
#        if self._claimer is not None and self._claimer.token is not None:
#            args = args.replace('$T', str(self._claimer.token))
        eval_preamble = '='
        args = args.strip()
        if args.startswith(eval_preamble):
            parsed_args = eval(args.lstrip(eval_preamble))
            if not isinstance(parsed_args, list):
                parsed_args = [parsed_args]
        else:
            parsed_args = []
            for arg in args.split():
                try:
                    parsed_args.append(int(arg, 0))
                    continue
                except ValueError:
                    pass
                try:
                    parsed_args.append(float(arg))
                    continue
                except ValueError:
                    pass
                parsed_args.append(arg)
        return parsed_args

    ###########################################################################
    # Predefined commands
    ###########################################################################
    def do_connect(self, args):
        """
        Connect to a remote ATA server. See connect()
        """
        host, port = split_args(args, 'localhost', DEFAULT_ATA_RPC_PORT)
        port = int(port)
        self.connect(host, port)

    def do_disconnect(self, _):
        """
        disconnect from the RPC server
        """
        self.disconnect()

    def do_import(self, args):
        """import a python module into the global namespace"""
        globals()[args] = import_module(args)

    def do_EOF(self, _):
        " When catching EOF, exit the program. "
        print("Exiting...")
        self.disconnect()
        exit(0)

def main():
    " Go, go, go! "
    args = parse_args()
    my_shell = ATAShell(args.host, args.port)

    try:
        return my_shell.run()
    except KeyboardInterrupt:
        my_shell.disconnect()
    except Exception as ex:
        print("Uncaught exception: " + str(ex))
        my_shell.disconnect()
    return True

if __name__ == "__main__":
    exit(not main())

