#! /usr/bin/python

import os, sys
from subprocess import Popen

dname = os.path.dirname(__file__)

def RaidSim(outname, server, names, withStatScaling=True, *args, **kwargs):
    exe = os.path.join(dname, "engine", "simc")
    lname = "%s.log" % outname
    logfile = open(lname, 'w')

    args = [exe]
    for name in names:
        args.append("armory=us,%s,%s" % (server, name))

    args.append("optimal_raid=0")
    args.append("threads=12")
    args.append("html=%s.html" % outname)
    if withStatScaling:
        args.append("iterations=15000")
        args.append("calculate_scale_factors=1")
        
    for item in kwargs.items():
        args.append("%s=%s" % item)

    print args

    proc = Popen(args, stdout=logfile)
    proc.wait()
    logfile.close()

    log = open(lname)
    lines = log.readlines()
    bgnidx = None
    endidx = None
    for lineidx, line in enumerate(lines):
        if line.startswith("DPS Ranking:"):
            bgnidx = lineidx + 2
        if bgnidx is not None:
            if not line.strip():
                endidx = lineidx
                break

    odps = {}
    for line in lines[bgnidx:endidx]:
        fields = line.strip().split()
        dps, percent, name = fields
        odps[name] = float(dps)

    return odps
