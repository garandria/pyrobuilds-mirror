import sys, os, re

re_NUMBER = re.compile(r'\.[0-9]+')

def getsizes(file, format):
    sym = {}
    with os.popen("nm --size-sort " + file) as f:
        for line in f:
            if line.startswith("\n") or ":" in line:
                continue
            size, type, name = line.split()
            if type in format:
                # strip generated symbols
                if name.startswith("__mod_"): continue
                if name.startswith("__se_sys"): continue
                if name.startswith("__se_compat_sys"): continue
                if name.startswith("__addressable_"): continue
                if name == "linux_banner": continue
                # statics and some other optimizations adds random .NUMBER
                name = re_NUMBER.sub('', name)
                sym[name] = sym.get(name, 0) + int(size, 16)
    return sym

def calc(oldfile, newfile, format):
    old = getsizes(oldfile, format)
    new = getsizes(newfile, format)
    grow, shrink, add, remove, up, down = 0, 0, 0, 0, 0, 0
    delta, common = [], {}
    otot, ntot = 0, 0

    for a in old:
        if a in new:
            common[a] = 1

    for name in old:
        otot += old[name]
        if name not in common:
            remove += 1
            down += old[name]
            delta.append((-old[name], name))

    for name in new:
        ntot += new[name]
        if name not in common:
            add += 1
            up += new[name]
            delta.append((new[name], name))

    for name in common:
        d = new.get(name, 0) - old.get(name, 0)
        if d>0: grow, up = grow+1, up+d
        if d<0: shrink, down = shrink+1, down-d
        delta.append((d, name))

    delta.sort()
    delta.reverse()
    return grow, shrink, add, remove, up, down, delta, old, new, otot, ntot
