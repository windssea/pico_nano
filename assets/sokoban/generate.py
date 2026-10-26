# SPDX-License-Identifier: Apache-2.0
"""原创推箱关卡逆向生成、最少推数求解及独立重放。 / Original reverse generation, optimal-push solving and independent replay."""
import argparse,heapq,itertools,json,pathlib,random,struct,zlib

def positions(mask):
 while mask:
  b=mask&-mask;yield b.bit_length()-1;mask-=b

def reach(p,boxes,free,ns):
 seen=1<<p;todo=[p];blocked=sum(1<<b for b in boxes)
 for c in todo:
  for n in ns[c]:
   if n>=0 and free>>n&1 and not (blocked|seen)>>n&1:seen|=1<<n;todo.append(n)
 return seen

def solve(w,h,free,goals,boxes,p,limit=15000):
 ns=[[c-w if c>=w else -1,c+1 if c%w<w-1 else -1,c+w if c//w<h-1 else -1,c-1 if c%w else -1] for c in range(w*h)]
 distances=[]
 for goal in goals:
  dist={goal:0};q=[goal]
  for c in q:
   for d in range(4):
    n=ns[c][d];b=ns[n][d] if n>=0 else -1
    if b>=0 and free>>n&1 and free>>b&1 and n not in dist:dist[n]=dist[c]+1;q.append(n)
  distances.append(dist)
 perms=list(itertools.permutations(range(len(goals))));hc={}
 def heuristic(bs):
  if bs not in hc:hc[bs]=min(sum(distances[j].get(bs[i],10000) for i,j in enumerate(perm)) for perm in perms)
  return hc[bs]
 start=(tuple(sorted(boxes)),min(positions(reach(p,boxes,free,ns))));cost={start:0};parent={};todo=[(heuristic(start[0]),0,0,start)];serial=0;expanded=0
 target=set(goals)
 while todo and expanded<limit:
  _,g,_,key=heapq.heappop(todo)
  if cost.get(key)!=g:continue
  bs,at=key;expanded+=1
  if set(bs)==target:
   moves=[]
   while key!=start:
    before,move=parent[key];moves.append(move);key=before
   return list(reversed(moves)),expanded
  area=reach(at,bs,free,ns);occupied=set(bs)
  for b in bs:
   for d in range(4):
    dest=ns[b][d];stand=ns[b][(d+2)%4]
    if dest<0 or stand<0 or not area>>stand&1 or not free>>dest&1 or dest in occupied:continue
    nb=tuple(sorted(occupied-{b}|{dest}));hv=heuristic(nb)
    if hv>=10000:continue
    nk=(nb,min(positions(reach(b,nb,free,ns))))
    if g+1>=cost.get(nk,10**9):continue
    cost[nk]=g+1;parent[nk]=(key,(b,d));serial+=1;heapq.heappush(todo,(g+1+hv,g+1,serial,nk))
 return None,expanded

def candidate(rng,tier,index):
 w=h=6 if tier==0 else 7 if tier==1 else rng.choice((8,9));nbox=1 if tier==0 and index<10 else 2 if tier<2 else 3
 free=0
 for y in range(1,h-1):
  for x in range(1,w-1):
   if rng.random()>(.08 if tier==0 else .19):free|=1<<(y*w+x)
 cells=list(positions(free))
 if len(cells)<nbox*4+4:return
 ns=[[c-w if c>=w else -1,c+1 if c%w<w-1 else -1,c+w if c//w<h-1 else -1,c-1 if c%w else -1] for c in range(w*h)]
 if reach(cells[0],(),free,ns)!=free:return
 goals=rng.sample(cells,nbox);boxes=set(goals);p=rng.choice([c for c in cells if c not in boxes]);seen=set()
 for _ in range(rng.randint(8,18) if tier==0 else rng.randint(20,40) if tier==1 else rng.randint(45,85)):
  area=reach(p,boxes,free,ns);choices=[]
  for b in boxes:
   for d in range(4):
    a=ns[b][d];z=ns[a][d] if a>=0 else -1
    if z>=0 and area>>a&1 and free>>z&1 and z not in boxes:choices.append((b,a,z))
  if not choices:break
  fresh=[c for c in choices if (tuple(sorted(boxes-{c[0]}|{c[1]})),c[2]) not in seen]
  b,a,p=rng.choice(fresh or choices);boxes.remove(b);boxes.add(a);seen.add((tuple(sorted(boxes)),p))
 if boxes==set(goals):return
 return w,h,free,goals,sorted(boxes),p

def replay(level):
 w,h=level['w'],level['h'];free=int(level['free'],16);goals=set(level['goals']);boxes=set(level['boxes']);p=level['player'];ns=[[c-w if c>=w else -1,c+1 if c%w<w-1 else -1,c+w if c//w<h-1 else -1,c-1 if c%w else -1] for c in range(w*h)]
 last=-1;switches=off=0
 for b,d in level['solution']:
  assert b in boxes and 0<=d<4
  to=ns[b][d];stand=ns[b][(d+2)%4];assert to>=0 and stand>=0 and free>>to&1 and to not in boxes and reach(p,boxes,free,ns)>>stand&1
  switches+=last>=0 and b!=last;off+=b in goals and to not in goals
  boxes.remove(b);boxes.add(to);p=b;last=to
 assert boxes==goals
 return switches,off

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--seed',type=int,default=20261026);ap.add_argument('--out',type=pathlib.Path,default=pathlib.Path('assets/sokoban'));ap.add_argument('--checkpoint',type=pathlib.Path);a=ap.parse_args();rng=random.Random(a.seed);cp=a.checkpoint;levels=[]
 if cp and cp.exists():
  saved=json.loads(cp.read_text());assert saved['seed']==a.seed and saved['generator']==1
  levels=saved['levels'];rs=saved['rng'];rng.setstate((rs[0],tuple(rs[1]),rs[2]))
 known={(x['w'],x['h'],x['free'],tuple(x['goals']),tuple(x['boxes'])) for x in levels}
 for tier in range(3):
  attempts=0
  while sum(x['tier']==tier for x in levels)<30:
   index=sum(x['tier']==tier for x in levels);attempts+=1
   if attempts>20000:raise RuntimeError(('generation budget',tier,index))
   c=candidate(rng,tier,index)
   if not c:continue
   w,h,free,goals,boxes,p=c;key=(w,h,hex(free),tuple(sorted(goals)),tuple(boxes))
   if key in known:continue
   solution,expanded=solve(*c,limit=10000 if tier<2 else 16000)
   if not solution or len(solution)>48:continue
   n=len(solution);lo,hi=(2,6) if tier==0 and index<10 else (3,10) if tier==0 else (8,22) if tier==1 else (16,40)
   if not lo<=n<=hi:continue
   level=dict(id=tier*30+index,w=w,h=h,free=hex(free),goals=sorted(goals),boxes=boxes,player=p,tier=tier,solution=solution,pushes=n,expanded=expanded)
   switches,off=replay(level)
   if tier==0 and (off or switches>3):continue
   if tier==1 and switches<1:continue
   if tier==2 and (switches<4 or expanded<50):continue
   level.update(switches=switches,off_goal=off);levels.append(level);known.add(key)
   if cp:
    cp.parent.mkdir(parents=True,exist_ok=True);cp.write_text(json.dumps(dict(seed=a.seed,generator=1,rng=rng.getstate(),levels=levels)))
   print(tier,index+1,n,switches,expanded,flush=True)
 emit(levels,a.seed,a.out)

def emit(levels,seed,out):
 # 单箱入门在前，同档按推数逐步增加。/ Single-box tutorials first, then a gradual push-count progression per tier.
 levels.sort(key=lambda x:(x['tier'],len(x['boxes']),x['pushes'],x['switches'],x['expanded'],x['free'],x['goals'],x['boxes'],x['player']))
 for i,level in enumerate(levels):level['id']=i
 payload=bytearray()
 for level in levels:
  replay(level);rec=bytearray(160);rec[:8]=bytes([level['w'],level['h'],level['tier'],len(level['boxes']),level['player'],len(level['solution']),level['pushes'],0]);struct.pack_into('<HBB',rec,8,level['id'],level['switches'],level['off_goal']);free=int(level['free'],16);wall=((1<<128)-1)^free
  for offset,mask in ((12,wall),(28,sum(1<<g for g in level['goals'])),(44,sum(1<<b for b in level['boxes']))):rec[offset:offset+16]=mask.to_bytes(16,'little')
  for i,(b,d) in enumerate(level['solution']):rec[60+i*2:62+i*2]=bytes([b,d])
  payload+=rec
 header=bytearray(32);header[:4]=b'PNSK';struct.pack_into('<HHII',header,4,1,160,len(levels),zlib.crc32(payload));out.mkdir(parents=True,exist_ok=True);(out/'levels.pnsk').write_bytes(header+payload);(out/'levels.json').write_text(json.dumps({'seed':seed,'generator':1,'levels':levels},ensure_ascii=False,indent=2)+'\n');print('Complete:',len(header+payload),flush=True)
if __name__=='__main__':main()
