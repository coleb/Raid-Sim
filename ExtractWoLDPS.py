#! /usr/bin/python
import sys, re
from urllib2 import urlopen

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
        return self.amount/self.active, self.amount/duration, self.active/duration

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
        assert masterId in self.players
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

    for dps in dpschart.GetDPS():
        print dps
    
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
