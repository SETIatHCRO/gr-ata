#ata-unset-atarm.py

''' Please run this program when you are finished observing so that
    others know you are finished and you no longer have control over 
    the array. 

    Thank you!

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
              "and a sign-off message following this format: \n"
              "python ata-unset-alarm.py -u username -m 'exit message'")
        exit()
    
    i = args.index('-u')
    username = args[i+1]

    j = args.index('-m')
    message = args[j+1]

    ac.unset_alarm(message, username)
    print("You have released the array.")
    exit()

if __name__ == "__main__":
    main()
