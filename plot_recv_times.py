import sys
import os

#What interval should be used on x-axis. I.e., how many ms should one tick
#express
interval_length = int(sys.argv[1])
logfile = open(sys.argv[2], "r")

#Read the initial timestamp, so I can calculate the first timestamp
line = logfile.readline().strip().split(' ')
initial_tstamp = float(line[0])

cur_interval_start = initial_tstamp

#Timestamp in file is in seconds
cur_interval_end = initial_tstamp + (interval_length / 1000.0)

#Reset file descriptor
logfile.seek(0, os.SEEK_SET)
x_val = 0
read = True
interval_bytes = 0

while True:
    #To avoid the additional function, have a check here for read. This is set
    #to false when a new interval is started
    if read:
        line = logfile.readline()

        if line == '':
            break

        line = line.strip().split(' ')
    
    tstamp, numbytes = float(line[0]), int(line[1])

    if tstamp > cur_interval_end:
        print cur_interval_start, cur_interval_end

        print x_val, interval_bytes
        x_val += 1
        interval_bytes = 0

        read = False
        cur_interval_start = cur_interval_end
        cur_interval_end = cur_interval_start + (interval_length / 1000.0)
    else:
        read = True
        interval_bytes += numbytes

logfile.close()
