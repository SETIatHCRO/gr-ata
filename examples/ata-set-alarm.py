#ata-set-alarm.py

''' This program allows you to reserve the ATA under your username,
    and thus grants you permission to control the antennas and the 
    LO frequency. 

    Please DO NOT lock out the array without permission from staff. 

    Happy observing! :) 

    E. White, 11 Aug. 2020

'''

import sys
from ATATools import ata_control as ac

def main():

    #username = input("Please enter your username >> ")
    arg_len = len(sys.argv) - 1
    args = sys.argv

    if arg_len < 4:
        print("Missing arguments -- you must indicate a username \n"
              "and an observation description following this format: \n"
              "python ata-set-alarm.py -u username -m 'obs description'")
        exit()
    
    i = args.index('-u')
    username = args[i+1]

    j = args.index('-m')
    message = args[j+1]

    ac.set_alarm(message, username)
    print("You have reserved the array. Happy observing!")
    exit()

if __name__ == "__main__":
    main()
