"""真实SHA证明、重挂载与ZIP任务观察，不以自报hit代替入口验证。 / Real SHA proof, remounts and ZIP-job observation, not a self-reported hit alone."""
import os
import pathlib
import shutil
import sys
import tempfile
from test_resume import fixture,run
binary,font,port=sys.argv[1:]
with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp);fixture(root);book=root/'book.epub';seed=root/'seed'
    result,_=run(binary,'epub',book,font,seed,root/'seed-frames','seed',dict(os.environ,PN_BOOTSTRAP_SEED='1'))
    assert result['boot_stored']==1,result
    for case in ('memo','epoch','missing','corrupt','epoch-missing','epoch-corrupt','epoch-reopen'):
        state=root/case;shutil.copytree(seed,state);env=dict(os.environ,PN_BOOTSTRAP_TRUSTED='1')
        if case.startswith('epoch'):env['PN_BOOTSTRAP_EPOCH']='1'
        if case.endswith('reopen'):env['PN_BOOTSTRAP_REOPEN']='1'
        packet=next((state/'reading/resume').glob('*.pnbs'))
        if case.endswith('missing'):packet.unlink()
        if case.endswith('corrupt'):
            b=bytearray(packet.read_bytes());b[-1]^=1;packet.write_bytes(b)
        ref=root/(case+'-reference');shutil.copytree(state,ref)
        for f in ref.rglob('*.pnbs*'):f.unlink()
        _,expected=run(binary,'epub',book,font,ref,root/(case+'-reference-frames'))
        result,actual=run(binary,'epub',book,font,state,root/(case+'-frames'),env=env)
        assert actual==expected,(case,result)
        hit=case in ('memo','epoch','epoch-reopen');assert result['boot_hits']==(2 if case=='epoch-reopen' else int(hit)),result
        assert result['directory_begins']==(0 if hit else 1),result
        if hit:assert (result['identity_steps']>0)==(port=='host' or case.startswith('epoch')),result
        if case=='epoch-reopen':assert (result['reopen_identity_steps']>0)==(port=='host'),result
        print(port,case,result,flush=True)
