"""大图降采样、连续块预算与损坏出口。 / Large-image downsampling, contiguous budgets and corrupt exits."""
import pathlib,json,os,subprocess,sys,re,tempfile
binary=sys.argv[1];root=pathlib.Path(sys.argv[2])
manifest=json.loads((root/'jpeg-large-samples.json').read_text())
for sample in manifest['samples']:
    path=root/sample['file']
    env=dict(os.environ,PN_JPEG_BUDGET=str(6*1024*1024),PN_JPEG_MAX_BLOCK=str(262144+64))
    result=subprocess.run([binary,str(path),'184','256'],env=env,capture_output=True,timeout=30)
    assert result.returncode==0,result.stderr
    assert b'used=0 live=0' in result.stderr,result.stderr
    pixels=result.stdout.split(b'\n',3)[3];assert len(pixels)==184*256
    # 原创纯色带内部有独立亮度预期；缩小图与原始最近邻不强行等同。/ Flat bands have independent luma expectations; scaled IDCT differs from native nearest sampling.
    assert abs(pixels[64*184+92]-119)<=17 and abs(pixels[192*184+92]-170)<=17
    if not sample['progressive']:assert int(re.search(rb'peak=(\d+)',result.stderr)[1])<256*1024,result.stderr
    if sample['progressive']:
        # 系数整图约5MiB放不进2MiB池：稀疏后备存储换出行，像素须与大池逐字节一致（无损）；过小池干净失败。
        # Whole-image coefficients (about 5 MiB) cannot fit a 2 MiB pool: the sparse backing store swaps rows out and pixels must match the large pool byte for byte (lossless); a tiny pool fails cleanly.
        small=subprocess.run([binary,str(path),'184','256'],env=dict(env,PN_JPEG_BUDGET=str(2*1024*1024)),capture_output=True,timeout=60)
        assert small.returncode==0 and b'used=0 live=0' in small.stderr,small.stderr
        assert small.stdout==result.stdout,'sparse backing store changed pixels'
        assert int(re.search(rb'peak=(\d+)',small.stderr)[1])<=2*1024*1024,small.stderr
        tiny=subprocess.run([binary,str(path),'184','256'],env=dict(env,PN_JPEG_BUDGET=str(96*1024)),capture_output=True,timeout=60)
        assert tiny.returncode!=0 and not tiny.stdout and b'used=0 live=0' in tiny.stderr,tiny.stderr
    print(sample['file'],result.stderr.decode().strip())
    with tempfile.TemporaryDirectory() as folder:
        bad=pathlib.Path(folder)/'truncated.jpg';bad.write_bytes(path.read_bytes()[:-2])
        failed=subprocess.run([binary,str(bad),'184','256'],env=env,capture_output=True,timeout=30)
        assert failed.returncode!=0 and not failed.stdout and b'used=0 live=0' in failed.stderr,failed.stderr
print('large JPEG: scaled luma, bounded chunks, coefficient budgets and no corrupt output passed')
