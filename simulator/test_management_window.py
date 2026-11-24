"""管理三键（方案A统一焦点）与面板返回，不跳过删除确认。 / Plan A management keys and sheet return without skipping deletion confirmation."""
import os,pathlib,subprocess,sys,tempfile
with tempfile.TemporaryDirectory() as folder:
    root=pathlib.Path(folder);books=root/'books';books.mkdir();state=root/'state';state.mkdir();fonts=root/'fonts';fonts.mkdir()
    book=books/'a.txt';book.write_text('管理键验证。\n'*50,encoding='utf-8')
    book2=books/'b.txt';book2.write_text('第二本书。\n'*50,encoding='utf-8')
    def run(script):
        print('management scenario:',script,flush=True)
        result=subprocess.run([sys.argv[1],'--library',str(books),'--state-dir',str(state),'--font-dir',str(fonts)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',PN_SIM_LIBRARY_SCRIPT='library,'+script),capture_output=True,text=True,timeout=60)
        assert result.returncode==0,(result.stdout,result.stderr)
        assert 'used=0 live=0' in result.stdout,result.stdout
        return result.stdout
    # 书库选择态：KEY2进焦点，KEY3/KEY1移焦点不翻页，KEY2长按退出焦点 / Shelf selection mode: KEY2 focuses, KEY3/KEY1 move focus, KEY2 long exits focus
    out=run('key-home,key-confirm,key-left,key-middle-long,quit')
    assert 'shelf_focus before=-1 after=0' in out and 'shelf_focus before=0 after=1' in out and 'shelf_focus before=1 after=0' in out and 'shelf_focus before=0 after=-1' in out,out
    # 设置子页长按KEY2逐级返回，顶层长按KEY2回主页 / Settings subpage returns via KEY2 long, root returns Home
    out=run('settings,tap:100:700,key-middle-long,key-middle-long,quit')
    assert 'settings command=1 status=0' in out and 'management home status=0' in out,out
    # 设置页KEY2先入首个非返回焦点（字体管理）再KEY2确认进入，长按KEY2逐级返回；移至“阅读”首项（全局排版）确认并逐级返回 / Settings KEY2 focuses Font Management first and unwinds; moving to Global Typesetting confirms and unwinds
    out=run('settings,key-home,key-home,key-middle-long,key-middle-long,quit')
    assert 'settings closed request=3' in out and 'font_manage command=1 status=0' in out and 'management home status=0' in out,out
    out=run('settings,key-home,key-confirm,key-confirm,key-confirm,key-home,key-middle-long,key-middle-long,quit')
    assert 'settings command=51 status=0' in out and 'settings command=1 status=0' in out and 'management home status=0' in out,out
    # 删除确认：未聚焦按KEY2只安全落到取消不删书，长按KEY2取消留面板 / Delete dialog: unfocused KEY2 focuses Cancel safely, KEY2 long cancels
    out=run('rtap:100:520,tap:300:1060,key-home,key-middle-long,key-middle-long,quit')
    assert 'actions command=3 status=0' in out and 'actions command=4 status=0 active=1' in out,out
    assert book.exists()
    # 排版页未移焦点时KEY2直接保存回正文；阅读中长按KEY2开工具栏，短按关工具栏，再短按回主页 / Styles quick-apply and reading toolbar/home
    out=run('enter,styles,style-plus,key-home,next,quit,quit')
    assert 'style_ui command=2 status=0 active=0' in out and 'management home status=0' not in out,out
    out=run('enter,key-middle-long,key-home,key-home,quit')
    assert 'reading middle long settings status=0' in out and 'reading settings middle status=0 active=0' in out and 'reading middle home status=0' in out,out
    # 设置二级子页未聚焦时短按KEY2直接返回上一级；从传书/设置按左右键出焦点后切底栏焦点自动清零 / Unfocused KEY2 short returns to parent in settings subpages; tab switch clears stale focus
    out=run('settings,tap:100:700,key-home,key-middle-long,quit')
    assert 'settings command=1 status=0' in out and 'management home status=0' in out,out
    out=run('settings,key-confirm,tap:430:1150,tap:300:760,key-home,key-middle-long,quit')
    assert 'hub open status=0' in out and 'file_manager command=1 status=0' in out and 'management home status=0' in out,out
    # 确认页切焦点到“确认删除”再按KEY2短按才删除 / Explicit delete requires moving focus to Confirm then pressing KEY2 short
    out=run('rtap:100:520,tap:300:1060,key-home,key-confirm,key-home,quit')
    assert 'actions command=5 status=0' in out and not book.exists(),out
print('management keys: Plan A shelf focus, nested Back, Confirm, Home, reader cleanup and explicit delete passed')
