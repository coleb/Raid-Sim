#! /usr/bin/python

import os, sys
from subprocess import Popen

dname = os.path.dirname(__file__)

def ToonSim(server, name, withStatScaling=True, *args, **kwargs):
    exe = os.path.join(dname, "engine", "simc")
    lname = "%s.log" % name
    logfile = open(lname, 'w')
    args = [exe, 
            "armory=us,%s,%s" % (server, name),
            "threads=12",
            "html=%s.html" % name]
    if withStatScaling:
        args.append("iterations=10000")
        args.append("calculate_scale_factors=1")

    for item in kwargs.items():
        args.append("%s=%s" % item)

    proc = Popen(args, stdout=logfile)
    proc.wait()
    logfile.close()

    log = open(lname)
    for line in log:
        if line.startswith("  DPS: "):
            fields = line.split()
            return float(fields[1])

def main(argv=[__name__]):
    if len(argv) != 3:
        print "Usage: %s <server> <name>" % argv[0]
        return 1

    server = argv[1]
    name   = argv[2]

    print ToonSim(server, name, True)

    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
