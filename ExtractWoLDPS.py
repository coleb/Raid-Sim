#! /usr/bin/python
import sys, re
import os
import os.path
from urllib2 import urlopen
from RaidSim import RaidSim

vname = r"rawTableData = "
scriptRegEx = re.compile(vname)
dataRegEx   = re.compile(vname + r"(.*)")

class Player:
    def __init__(self, name, amount, active):
        self.name = name
        self.amount = float(amount)
        self.active = float(active)

    def AddPetDamage(self, amount):
        self.amount += amount

    def GetDPS(self, duration):
        try:
            dps = 1000*self.amount/self.active
        except ZeroDivisionError:
            dps = 0
        try:
            edps = 1000*self.amount/duration
            act  = 100*self.active/duration
        except ZeroDivisionError:
            edps = 0
            act  = 0
        return dps, edps, act

    def GetName(self):
        return self.name

class DPSChart:
    def __init__(self, duration):
        self.duration = duration
        self.players = {}

    def AddPlayer(self, uid, name, amount, active):
        assert uid not in self.players
        self.players[uid] = Player(name, amount, active)

    def AddPet(self, masterId, amount):
        if masterId not in self.players:
            print "Being forced to skip a pet", masterId
            return
        self.players[masterId].AddPetDamage(amount)

    def GetDPS(self):
        for p in self.players.values():
            ret = [p.GetName()] 
            ret.extend(p.GetDPS(self.duration))
            yield ret

def main(argv=[__name__]):
    url = argv[1]

    data = urlopen(url).read()
    
    m = dataRegEx.search(data)
    rawData = m.group(0)[len(vname):].strip().rstrip(";")

    lvars={"null":None, "true":True, "false":False}
    gvars={} 
    rawTableData = eval(rawData, gvars, lvars)

    duration = rawTableData["duration"]
    dpschart = DPSChart(duration)
    rows = rawTableData["rows"]
    for r in rows:
        del r["nameLink"]
        
        typename = r["typeName"]
        if typename == "player":
            uid = r["uid"]
            name = r["name"]
            amount = r["amount"]
            active = r["activeTime"]
            dpschart.AddPlayer(uid, name, amount, active)
            
        elif typename == "pet":
            masterId = r["masterId"]
            amount = r["amount"]
            if masterId is not None:
                dpschart.AddPet(masterId, amount)

    seconds = int(float(duration) / 1000)

    data = []
    names = []
    for name, dps, edps, active in dpschart.GetDPS():
        names.append(name)
        
    withStatScaling = True
    args = {"max_time":"%i" % seconds,
            "vary_combat_length": "0.001"}
    reportName = os.path.split(os.getcwd())[-1]
    optdps, ilvls = RaidSim(reportName, "cenarion-circle", names, withStatScaling, **args)
    
    for name, dps, edps, active in dpschart.GetDPS():
        if name not in optdps:
            continue
        opt = optdps[name]
        
        percentOfOpt = 0.0
        if opt > 0.0:
            percentOfOpt = 100*edps/opt

        data.append((percentOfOpt, opt, edps, dps, active, name))

    data.sort()
    print "<table>"
    print "<tr><th>eDPS/oDPS</th><th>oDPS</th><th>eDPS</th><th>DPS</th><th>Activity</th><th>Name</th></tr>"
    for percentOfOpt, opt, edps, dps, active, name in data:
        print "<tr>",
        fmt = "<td>%6.1f</td><td>%6.0f</td><td>%6.0f</td><td>%6.0f</td><td>%6.1f</td><td>%s</td>"
        print fmt % (percentOfOpt, opt, edps, dps, active, name)
        print "</tr>"

    print "</table>"

    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
