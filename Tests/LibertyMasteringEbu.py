#!/usr/bin/env python3
"""Measure a clearly scoped set of official EBU stereo reference files.
No certification claim; the original archive is not committed or redistributed.
Sources: EBU Tech 3341, 3342, https://tech.ebu.ch/publications/ebu_loudness_test_set.
"""
import sys, zipfile, wave, subprocess, struct, os, re, math
archive, exe = sys.argv[1:3]
# (file identifier, metric index, reference value, lower tolerance, upper tolerance)
cases = [
    ("3341-1", 0, -23., .1, .1),
    ("3341-3", 0, -23., .1, .1),
    ("3341-4", 0, -23., .1, .1),
    ("3341-5", 0, -23., .1, .1),
    ("3341-7", 0, -23., .1, .1),
    ("3341-8", 0, -23., .1, .1),
    ("3342-1", 1, 10., 1., 1.),
    ("3342-2", 1, 5., 1., 1.),
    ("3342-3", 1, 20., 1., 1.),
    ("3342-4", 1, 15., 1., 1.),
    ("3342-5", 1, 5., 1., 1.),
    ("3342-6", 1, 15., 1., 1.),
] + [(f"3341-{i}", 2, -6. if i<19 else 3. if i==19 else 0., .4, .2) for i in range(15,24)]
def measure(z, name):
    with z.open(name) as audio, wave.open(audio, "rb") as w:
        channels,width,rate = w.getnchannels(),w.getsampwidth(),w.getframerate()
        if channels not in (1,2) or width not in (2,3,4):
            raise ValueError(f"unsupported PCM layout: {channels} ch, {width} bytes")
        with subprocess.Popen([exe,str(rate)],stdin=subprocess.PIPE,stdout=subprocess.PIPE) as p:
            while True:
                raw=w.readframes(8192)
                if not raw: break
                values=[]
                scale=2.**(8*width-1)
                for offset in range(0,len(raw),channels*width):
                    l=int.from_bytes(raw[offset:offset+width],"little",signed=True)/scale
                    r=int.from_bytes(raw[offset+width:offset+2*width],"little",signed=True)/scale if channels==2 else 0.
                    values.extend((l,r))
                p.stdin.write(struct.pack("<"+"f"*len(values),*values))
            p.stdin.close()
            output=p.stdout.read().decode()
            if p.wait(): raise ValueError("measurement failed")
            return list(map(float,output.split()))
errors=0; tested=0
with zipfile.ZipFile(archive) as z:
    for key,metric,ref,lo,hi in cases:
        regex=re.compile(r"(?<!\d)"+key.replace("3341-","3341-(?:2011-)?")+r"(?!\d)",re.I)
        files=sorted(n for n in z.namelist() if n.lower().endswith(".wav") and regex.search(os.path.basename(n)) and "5channels" not in n and "6channels" not in n)
        if not files:
            print(f"NOT RUN {key}: fixture missing",flush=True);errors+=1;continue
        try:
            actual=measure(z,files[0])[metric]
            ok=math.isfinite(actual) and ref-lo-1e-8 <= actual <= ref+hi+1e-8
            print(f"{'PASS' if ok else 'FAIL'} {os.path.basename(files[0])}: {actual:.3f} expected {ref:.1f} (-{lo}/+{hi})",flush=True)
            tested+=1; errors+=not ok
        except Exception as exc:
            print(f"ERROR {key}: {exc}",flush=True);errors+=1
print(f"Reference subset: {tested}/{len(cases)} measured, {errors} failed or not run. Not a certification.",flush=True)
sys.exit(bool(errors))
