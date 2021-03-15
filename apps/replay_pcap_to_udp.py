#!/usr/bin/python3
from scapy.all import UDP,PcapReader
import socket
import argparse

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='UDP unicast to multicast bridge')
    parser.add_argument('--inputfile', '-i', type=str, help="PCAP recording input file",  required=True)
    parser.add_argument('--port', '-p', type=int, help="unicast UDP port to forward.",  required=True)
    parser.add_argument('--num-packets', '-n', type=int, help="Number of packets to send.  Default is all.",  default=0,  required=False)
    parser.add_argument('--destination', '-d', type=str, help="IP of target",  required=True)
    parser.add_argument('--destination-port', '-d', type=int, help="UDP port to send to",  required=True)
    parser.add_argument('-v',"--verbose",help="Stdout message when a packet is transmitted",  action='store_true')
    
    args = parser.parse_args()

    try:
        print("Reading pcap file...")
        pcapreader = PcapReader(args.inputfile)
    except KeyboardInterrupt:
        print("Read interrupted.  Exiting.")
        exit(0)
    except:
        print("Error reading pcap file " + args.inputfile)
        exit(1)

    # Set up multicast output
    mcast_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    mcast_dest = (args.destination, args.destination_port)
    # Limit multicast scope to local segment

    print("UDP port to monitor: " + str(args.port))
    
    i=0
    
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

            if args.verbose:
                print("Sending " + str(len(bytes)) + " bytes to " + str(mcast_dest) + " counter=" + str(i))
                
            mcast_socket.sendto(bytes, mcast_dest)
            
            i+= 1
            
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
    
