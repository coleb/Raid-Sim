#! /usr/bin/python

import os, sys
from subprocess import Popen
from datetime import date

dname = os.path.dirname(__file__)

def GetAverageItemLevel(simcdata):
    items = []
    for line in simcdata.split('\n'):
        if "ilevel" not in line:
            continue

        if line.startswith("shirt"):
            continue

        if line.startswith("tabard"):
            continue
        
        for field in line.split(','):    
            name, ilvl = tuple(field.split('='))
            if name == "ilevel":
                items.append(int(ilvl))
                continue

    print items
    return sum(items)/len(items)
    

def RaidSim(outname, server, names, withStatScaling=True, *args, **kwargs):
    exe = os.path.join(dname, "engine", "simc")
    lname = "%s.log" % outname
    logfile = open(lname, 'w')

    simc = []
    args = [exe]
    ilvls = {}
    for name in names:
        fname = "%s-%s.simc" % (date.today().isoformat(), name)
        while not os.path.exists(fname):
            armory = list(args)
            armory.append("armory=us,%s,%s" % (server, name))
            armory.append("save=%s" % fname)
            proc = Popen(armory, stdout=logfile)
            proc.wait()

        simcdata = open(fname).read()
        simc.append(simcdata)
        print name, 
        ilvls[name] = GetAverageItemLevel(simcdata)

    simcfname = outname + ".simc"
    tmp = open(simcfname, 'w')
    tmp.write('\n'.join(simc))
    tmp.close()

    args.append(simcfname)
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

    return odps, ilvls
