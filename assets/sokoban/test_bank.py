# SPDX-License-Identifier: Apache-2.0
"""独立检查题包、分档和最优推数，所有证明另由C核心回放。 / Check packing, tiers and optimal push counts; C separately replays all proofs."""
import json,pathlib,struct,zlib
from generate import replay,solve
root=pathlib.Path(__file__).resolve().parent
meta=json.loads((root/'levels.json').read_text());levels=meta['levels'];raw=(root/'levels.pnsk').read_bytes()
assert raw[:4]==b'PNSK' and struct.unpack_from('<HHII',raw,4)==(1,160,90,zlib.crc32(raw[32:]))
assert len(raw)==14432 and not any(raw[16:32]) and len(levels)==90
keys=set()
for i,l in enumerate(levels):
    r=raw[32+i*160:32+(i+1)*160];tier=i//30
    assert l['id']==i and l['tier']==tier and r[:8]==bytes((l['w'],l['h'],tier,len(l['boxes']),l['player'],len(l['solution']),l['pushes'],0))
    assert struct.unpack_from('<HBB',r,8)==(i,l['switches'],l['off_goal'])
    free=int(l['free'],16);assert l['w']<=10 and l['h']<=12 and l['w']*l['h']<=128
    for off,mask in ((12,((1<<128)-1)^free),(28,sum(1<<g for g in l['goals'])),(44,sum(1<<b for b in l['boxes']))):assert r[off:off+16]==mask.to_bytes(16,'little')
    assert r[60:60+2*len(l['solution'])]==bytes(c for pair in l['solution'] for c in pair)
    assert not any(r[60+2*len(l['solution']):])
    assert replay(l)==(l['switches'],l['off_goal'])
    lo,hi=(2,6) if i<10 else (3,10) if tier==0 else (8,22) if tier==1 else (16,40)
    assert lo<=l['pushes']<=hi
    assert tier!=0 or l['off_goal']==0 and l['switches']<=3
    assert tier!=1 or l['switches']>=1
    assert tier!=2 or l['switches']>=4 and l['expanded']>=50
    key=(l['w'],l['h'],l['free'],tuple(l['goals']),tuple(l['boxes']));assert key not in keys;keys.add(key)
    # 重新求最优，避免把超时误判成高难。/ Solve again rather than treating a timeout as high difficulty.
    solution,expanded=solve(l['w'],l['h'],free,l['goals'],l['boxes'],l['player'],16000)
    assert solution is not None and len(solution)==l['pushes']
print('90 original levels: packed CRC, bounds, uniqueness, complete witnesses and optimal push grading passed')
