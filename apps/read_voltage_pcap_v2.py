#!/usr/bin/python3
from scapy.all import UDP,rdpcap, PcapReader
import numpy as np
import argparse

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='ATA SNAP Voltage PCAP Tool')
    parser.add_argument('--inputfile', '-i', type=str, help="SNAP PCAP recording file",  required=True)
    parser.add_argument('--port', '-p', type=int, help="SNAP PCAP recording file",  required=True)
    parser.add_argument('--num-packets', '-n', type=int, help="Number of packets to print (use 0 for all), default=32",  default=32,  required=False)
    parser.add_argument('--start-channel', '-s', type=int, help="Starting channel number.  If this is set, channels will be validated. default is to only print packets.",  default = -1,  required=False)
    parser.add_argument('--num-channels', '-c', type=int, help="Number of frame channels",  default=-1, required=False)
    parser.add_argument('--errors-only', '-e', help="Disable rewriting packet data and only print errors.  Requires start-channel and num-channels", action='store_true', required=False)
    
    args = parser.parse_args()

    if args.errors_only and (args.start_channel == -1 or args.num_channels == -1):
        print("ERROR: If printing errors only,  start channel and num channels is required.")
        exit(1)
        
    if args.start_channel >= 0:
        channel_validation= True
        if args.num_channels == -1:
            print("ERROR: If start-channel is specified, num-channels is also required.")
            exit(1)
            
        last_channel_block = args.start_channel + args.num_channels - 256
    else:
        channel_validation = False
        last_channel_block = 0
            
    try:
        print("Reading pcap file...")
        if args.num_packets == 0:
            print("WARNING: Reading the entire file may take considerable time and memory if the file is large.")
            # packets = rdpcap(args.inputfile)
            pcapreader = PcapReader(args.inputfile)
        else:
            # packets = rdpcap(args.inputfile,  count=args.num_packets)
            pcapreader = PcapReader(args.inputfile)
    except KeyboardInterrupt:
        print("Read interrupted.  Exiting.")
        exit(0)
    except:
        print("Error reading pcap file " + args.inputfile)
        exit(1)

    # v2 (November 2020) header format
    header_len = 16  # bytes

    print("UDP port to monitor: " + str(args.port))
    
    if args.errors_only:
        print("INFO: Only printing errors.")
        
    i=0
    
    # Sequence numbers are timestamps.  They seem to increase by 16 (matching the timestamps/block)
    last_seq_num = 0
    found_start_channel = False
    expected_next_channel_block = 0

    try:
        # for curPacket in packets[0:max_packets]:
        for curPacket in pcapreader:
            try:
                bytes=curPacket[UDP].payload.load
            except:
                # Ignore non-UDP packets
                continue
                
            if curPacket[UDP].dport != args.port:
                # Ignore packets that aren't destined for the correct port
                continue
                
            # Load the header bytes
            header_bytes=bytes[0:header_len]

            # Estract the fields
            fw_version = int(header_bytes[0])
            type = int(header_bytes[1])
            n_chans = int(np.ndarray(shape=(1,),dtype='>u2',buffer=header_bytes[2:4])[0])
            channel = int(np.ndarray(shape=(1,),dtype='>u2',buffer=header_bytes[4:6])[0])
            antenna = int(np.ndarray(shape=(1,),dtype='>u2',buffer=header_bytes[6:8])[0])
            sample_number = int(np.ndarray(shape=(1,),dtype='>u8',buffer=header_bytes[8:16])[0])

            if channel_validation and not found_start_channel:
                if channel == args.start_channel:
                    found_start_channel = True
                    expected_next_channel_block = args.start_channel 
                    
            # Setup for first iteration
            if last_seq_num == 0:
                last_seq_num = sample_number
                
            delta = int((sample_number - last_seq_num) // 16)
            
            if delta > 1:
                print("ERROR: Missed " + str(delta) + " frames going from sequence number " + str(last_seq_num) + " to " + str(sample_number))

            if channel_validation and found_start_channel:
                if channel != expected_next_channel_block:
                    if channel > expected_next_channel_block:
                        delta = int((channel - expected_next_channel_block) // 256)
                        print("ERROR: Missed " + str(delta) + " subpackets for sequence number " + str(sample_number))
                    else:
                        delta = int((last_channel_block - expected_next_channel_block + channel - args.start_channel)//256) + 1
                        print("ERROR: Missed " + str(delta) + " subpackets going from " + str(last_seq_num) + " to " + str(sample_number))
                        
                expected_next_channel_block += 256
                if expected_next_channel_block > last_channel_block:
                    expected_next_channel_block = args.start_channel
                        
            # Print out what we got
            if not args.errors_only:
                print("Packet #: " + str(i) + ", Sample #/timestamp: " + str(sample_number) + ", Starting channel: " + str(channel) + ", num channels: " + str(n_chans) +", antenna: " + str(antenna) + ", fw_version: " + str(fw_version))
            
            i+= 1
            last_seq_num = sample_number
            
            if args.num_packets > 0:
                if i >= args.num_packets:
                    break
    except KeyboardInterrupt:
        # Can do cleanup here if necessary
        pass
    except Exception as e:
        print("Unhandled exception occurred: " + str(e))
        print("Exiting.")
        exit(2)
        
    print("Done.")
    
