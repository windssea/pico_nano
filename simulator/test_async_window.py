"""忙碌显示期间真实事件泵、导航合并和回执保存。 / Real event pumping, coalescing and receipt persistence during busy display."""
import os,pathlib,re,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
 root=pathlib.Path(folder);books=root/'books';books.mkdir()
 (books/'a.txt').write_text(('阅读位置与异步输入测试。'*12+'\n')*400,encoding='utf-8')
 def run(name,script):
  state=root/name
  env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,enter,wait:900,'+script,PN_SIM_PRESENT_DELAY_MS='600',PN_SIM_TIMING='1',PN_SIM_FOCUS_NOISE='1')
  r=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state)],env=env,capture_output=True,text=True,timeout=35)
  assert r.returncode==0 and 'used=0 live=0' in r.stdout,(r.stdout,r.stderr)
  assert 'library_return status=0' in r.stdout,(r.stdout,r.stderr)
  return r
 burst=run('burst','burst:40:next,wait:1500,home,quit')
 coalesced=int(re.search(r'async mailbox coalesced=(\d+)',burst.stdout)[1]);turns=re.findall(r'library_turn pointer=0 status=(\d+)',burst.stdout)
 assert coalesced>=35 and 1<=len(turns)<=3 and all(s=='0' for s in turns),(burst.stdout,burst.stderr)
 # 延迟显示已经接受不可变帧，新首页输入仍被主线程采集；已显示页必须提交再保存。/ Once display accepts the immutable frame, main still captures Home; commit the shown page before saving.
 home=run('home','race-next-home:300,wait:200,continue,wait:300,back,quit')
 assert re.search(r'async capture key=104 pending=1',home.stderr),(home.stdout,home.stderr)
 assert 'library_turn pointer=0 status=0' in home.stdout,(home.stdout,home.stderr)
 positions=re.findall(r'library_position offset=(\d+)',home.stdout)
 # 公共日志包含打开位置与翻页位置；续读首位置应等于最后已显示翻页。/ Public logs include opened/turned locations; restored begin must equal the last shown turn.
 assert home.stdout.count('library_open name=a.txt')==2,(home.stdout,home.stderr)
 confirmed=re.findall(r'library_confirmed offset=(\d+)',home.stdout)
 assert len(positions)==2 and confirmed and int(positions[1])>int(positions[0]) and positions[1]==confirmed[-1],home.stdout
 tools=run('tools','race-next-tools:300,wait:300,back,home,quit')
 assert re.search(r'async capture key=107 pending=1',tools.stderr),(tools.stdout,tools.stderr)
 assert 'toolbar open status=0' in tools.stdout,(tools.stdout,tools.stderr)
 (books/'a.txt').write_bytes(b'Large cancellable original content.\n'*(20*1024*1024//36))
 cold=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(root/'cold')],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,race-open-home:20,wait:300,quit',PN_SIM_PRESENT_DELAY_MS='600',PN_SIM_TIMING='1'),capture_output=True,text=True,timeout=35)
 assert cold.returncode==0 and 'used=0 live=0' in cold.stdout and 'library_open name=a.txt' not in cold.stdout and 'shelf_open_failed' not in cold.stdout,(cold.stdout,cold.stderr)
 # 冷开可在加载画面交接前或期间取消，均不能发布正文。/ Cold opening may cancel before or during loading handoff, never publishing body content.
 assert re.search(r'async capture key=104 pending=[01]',cold.stderr) and 'opening end status=8' in cold.stdout,(cold.stdout,cold.stderr)
 print(f'Async SDL: 40 captured turns -> {len(turns)} executed, {coalesced} coalesced; Home/More captured during pending display; save barriers and release passed')
