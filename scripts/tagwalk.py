#!/usr/bin/env python3
"""tagwalk.py Struct [filter] -- print field offsets of an Invader struct, recursing
into inherited structs, and check the total against the declared size."""
import json,glob,sys
import os
DEF=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','upstream')+'//invader/src/tag/hek/definition/'
S={}
for fn in glob.glob(DEF+'*.json'):
    for s in json.load(open(fn)): S[s['name']]=s
PRIM={'int8':1,'uint8':1,'int16':2,'uint16':2,'int32':4,'uint32':4,'float':4,'Angle':4,
 'Fraction':4,'Index':2,'TagID':4,'Pointer':4,'Point2D':8,'Point3D':12,'Vector2D':8,'Vector3D':12,
 'Euler2D':8,'Euler3D':12,'Quaternion':16,'Plane2D':12,'Plane3D':16,'ColorRGB':12,'ColorARGB':16,
 'ColorARGBInt':4,'Point2DInt':4,'Rectangle2D':8,'TagString':32,'TagFourCC':4,'TagReflexive':12,
 'TagDependency':16,'TagDataOffset':20,'Matrix':36,'ScenarioScriptNodeValue':4,'Vector2DInt':4}
def size(t):
    if t in PRIM: return PRIM[t]
    s=S[t]
    if s['type']=='enum': return 2
    if s['type']=='bitfield': return s['width']//8
    if s['type']=='struct': return s['size']
    raise Exception(t)
def walk(name,base,out,prefix=''):
    s=S[name]; off=base
    if 'inherits' in s: off=walk(s['inherits'],off,out,prefix)
    for f in s['fields']:
        t=f['type']
        if t=='pad': out.append((off,'pad',prefix+'pad',f['size'])); off+=f['size']; continue
        if t=='editor_section': continue
        n=size(t)*(f.get('count',1))*(2 if f.get('bounds') else 1)
        extra=' struct='+f['struct'] if t=='TagReflexive' else ''
        out.append((off,t,prefix+f.get('name','?')+extra,n)); off+=n
    return off
name=sys.argv[1]; flt=sys.argv[2] if len(sys.argv)>2 else ''
out=[]; end=walk(name,0,out)
for o,t,n,sz in out:
    if flt in n: print(f'{o:6d} {sz:4d} {t:28s} {n}')
print('total',end,'declared',S[name].get('size'))
