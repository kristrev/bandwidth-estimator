import sys
import os

#Finds all packets that were received before interval_end, returns -1 if EOF
def readPkts(logfile, interval_end):
    interval_bytes = 0 

    while True:
        line = logfile.readline()
        line_len = len(line)
        line = line.strip().split(' ')

        if line[0] == '':
            return -1
        
        tstamp, numbytes = float(line[0]), int(line[1])

        if tstamp >= interval_end:
            logfile.seek(-1*line_len, os.SEEK_CUR)
            return interval_bytes
        else:
            interval_bytes += numbytes
        
    return interval_bytes


#What interval should be used on x-axis. I.e., how many ms should one tick
#express
interval_length = int(sys.argv[1])
logfile = open(sys.argv[2], "r")

#Read the initial timestamp, so I can calculate the first timestamp
line = logfile.readline().strip().split(' ')
initial_tstamp = float(line[0])

cur_interval_start = initial_tstamp
cur_interval_end = initial_tstamp + (interval_length / 1000.0)

#Reset file descriptor
logfile.seek(0, os.SEEK_SET)
x_val = 0

while True:
    interval_bytes = readPkts(logfile, cur_interval_end)

    if interval_bytes == -1:
        break

    cur_interval_start = cur_interval_end
    cur_interval_end = cur_interval_start + (interval_length / 1000.0)
    print x_val, interval_bytes
    x_val += 1
