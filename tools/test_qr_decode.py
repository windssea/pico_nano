"""独立OpenCV解码模拟截图中的两枚QR，仅默认演练值。 / Independently decode two QR codes in simulated captures using demo defaults."""
import argparse,pathlib,sys
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('captures',nargs='+',type=pathlib.Path);args=parser.parse_args()
 try:import cv2
 except ImportError:raise SystemExit('缺少OpenCV，请在测试环境安装opencv-python后运行')
 expected={'http://192.168.4.1','WIFI:T:WPA;S:小纸 Pico-1234;P:ABCDEFGH2345;H:false;;'}
 for path in args.captures:
  pixels=cv2.imread(str(path),cv2.IMREAD_GRAYSCALE)
  if pixels is None:raise SystemExit(f'无法读取测试捕获：{path}')
  okay,values,_,_=cv2.QRCodeDetector().detectAndDecodeMulti(pixels)
  if not okay or set(values)!=expected:raise SystemExit(f'二维码解码或UTF8内容不一致：{path.name}')
  print(f'{path.name}: exact demo WiFi UTF-8 payload and URL passed (OpenCV {cv2.__version__})')
 return 0
if __name__=='__main__':sys.exit(main())
