"""中文传输网页的PC HTTP适配，所有文件事务交给原生owner。 / PC HTTP adapter for Chinese transfer UI; native owner performs all file transactions."""
import argparse
import hashlib
import http.server
import json
import pathlib
import re
import secrets
import subprocess
import threading
import time
import urllib.parse

ROOT=pathlib.Path(__file__).resolve().parents[1]
KINDS={'book':1,'font':2,'cover':3,'wallpaper':4}
NAMES={value:key for key,value in KINDS.items()}
MESSAGES={1:'输入参数无效',2:'资源正在使用或同名文件已存在',3:'没有找到该文件或上传会话',4:'空间、配额或大小超过限制',5:'内存不足，请稍后重试',6:'存储介质已失效',7:'文件身份已经变化',8:'上传已取消',9:'暂不支持此格式',10:'摘要或文件格式校验失败',11:'读写失败，请重新查询后续传'}
HTTP_CODES={1:400,2:409,3:404,4:413,5:503,6:503,7:409,8:409,9:422,10:422,11:503}

class NativeWorker:
 def __init__(self,executable,root):
  self.lock=threading.Lock()
  self.process=subprocess.Popen([str(executable),str(root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  ready=json.loads(self.process.stdout.readline())
  if not ready.get('ready'):raise RuntimeError('原生传输进程启动失败')
 def call(self,command,body=b''):
  with self.lock:
   if self.process.poll() is not None:raise RuntimeError('原生传输进程已退出')
   self.process.stdin.write(command.encode('ascii')+b'\n'+body);self.process.stdin.flush()
   result=self.process.stdout.readline(4096)
   if not result:raise RuntimeError('原生传输进程没有返回结果')
   return json.loads(result)
 def close(self):
  try:
   if self.process.poll() is None:self.call('EXIT')
   self.process.wait(timeout=5)
  finally:
   if self.process.poll() is None:self.process.terminate();self.process.wait(timeout=5)
   for pipe in (self.process.stdin,self.process.stdout,self.process.stderr):pipe.close()

class TransferApp:
 def __init__(self,worker,root):
  self.root=pathlib.Path(root).resolve();self.root.mkdir(parents=True,exist_ok=True)
  self.worker=NativeWorker(worker,self.root)
  self.pair_code=f'{secrets.randbelow(1000000):06d}'
  self.token=secrets.token_hex(16)
  self.pair_expires=time.monotonic()+300;self.locked_until=0;self.failures=0;self.closed=False
  self.auth_lock=threading.Lock()
 def pair(self,code):
  with self.auth_lock:
   now=time.monotonic()
   if self.closed:return 503,{'message':'传输服务已停止'}
   if now<self.locked_until:return 429,{'message':'尝试过于频繁，请稍后再试','retry_after':max(1,int(self.locked_until-now))}
   if self.failures>=5:self.failures=0
   if now>=self.pair_expires:return 401,{'message':'配对码已过期，请重新开启传输'}
   if not isinstance(code,str) or not secrets.compare_digest(code,self.pair_code):
    self.failures+=1
    if self.failures>=5:self.locked_until=now+60
    return 401,{'message':'配对码不正确'}
   self.failures=0;return 200,{'token':self.token}
 def authorized(self,value):
  with self.auth_lock:return not self.closed and isinstance(value,str) and secrets.compare_digest(value,'Bearer '+self.token)
 def close(self):
  with self.auth_lock:self.closed=True;self.token=''
  self.worker.close()

class BadRequest(Exception):pass

def integer(value,maximum):
 if type(value) is not int or value<0 or value>maximum:raise BadRequest('数值超出允许范围')
 return value

def digest(value):
 if not isinstance(value,str) or not re.fullmatch('[0-9a-fA-F]{64}',value):raise BadRequest('请提供完整文件摘要')
 return value.lower()

def basename(value):
 if not isinstance(value,str) or not value or len(value.encode('utf-8'))>255 or '\0' in value:raise BadRequest('文件名无效或过长')
 return value.encode('utf-8').hex()

def public_state(result):
 output={'next_offset':result.get('offset',0),'size':result.get('size',0),'phase':result.get('phase',0),'cleanup_pending':result.get('cleanup_pending',False)}
 if 'name_hex' in result:output.update(name=bytes.fromhex(result['name_hex']).decode('utf-8'),kind=NAMES[result['kind']],sha256=result['sha256'],upload_id=result['id'])
 return output

class Handler(http.server.BaseHTTPRequestHandler):
 protocol_version='HTTP/1.1'
 def setup(self):
  super().setup();self.connection.settimeout(10)
 def log_message(self,*args):pass
 def send_json(self,status,value):
  data=json.dumps(value,ensure_ascii=False,separators=(',',':')).encode('utf-8')
  self.send_response(status);self.send_header('Content-Type','application/json; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.end_headers();self.wfile.write(data)
 def error(self,status,message):
  self.close_connection=True;self.send_json(status,{'message':message,'retryable':status in (409,429,503),'request_id':secrets.token_hex(4)})
 def header(self,name):
  values=self.headers.get_all(name,[])
  if len(values)>1:raise BadRequest('请求头重复')
  return values[0] if values else None
 def security(self):
  if self.header('Host')!=self.server.authority:self.error(403,'请求地址不匹配');return False
  origin=self.header('Origin')
  if origin and origin!='http://'+self.server.authority:self.error(403,'请从设备传输页面操作');return False
  if self.command in ('POST','PUT','DELETE') and not origin:self.error(403,'缺少页面来源');return False
  return True
 def body(self,limit):
  if self.header('Transfer-Encoding') is not None:raise BadRequest('不支持此请求编码')
  length=self.header('Content-Length')
  if length is None or not re.fullmatch('[0-9]+',length):raise BadRequest('缺少有效的内容长度')
  length=int(length)
  if length>limit:raise BadRequest('请求内容超过限制')
  data=self.rfile.read(length)
  if len(data)!=length:raise BadRequest('请求尚未接收完整')
  return data
 def object(self,allowed,required):
  content_type=self.header('Content-Type') or ''
  if content_type.split(';')[0].strip()!='application/json':raise BadRequest('请使用正确的请求格式')
  try:value=json.loads(self.body(8192))
  except (ValueError,RecursionError):raise BadRequest('请求格式有误')
  if not isinstance(value,dict) or set(value)-set(allowed) or set(required)-set(value):raise BadRequest('请求字段有误')
  return value
 def native(self,command,body=b'',success=200):
  result=self.server.app.worker.call(command,body);code=result['code']
  if code:self.error(HTTP_CODES.get(code,500),MESSAGES.get(code,'操作失败'));return None
  return result
 def dispatch(self):
  try:
   if not self.security():return
   parsed=urllib.parse.urlsplit(self.path);path=parsed.path
   if self.command=='GET' and path in ('/','/qa','/qa.mjs','/app.js','/style.css','/sha256.mjs','/hash-worker.mjs'):
    filename='index.html' if path in ('/','/qa') else path[1:];data=(ROOT/'assets/transfer'/filename).read_bytes();data=data.replace(b'</body>',b'<script type="module" src="/qa.mjs"></script></body>') if path=='/qa' else data;mime='text/html' if filename.endswith('.html') else 'text/css' if filename.endswith('.css') else 'text/javascript'
    self.send_response(200);self.send_header('Content-Type',mime+'; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self'; worker-src 'self'; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");self.end_headers();self.wfile.write(data);return
   if self.command=='GET' and path=='/api/v1/status':self.send_json(200,{'name':'小纸 Pico','version':'0.0.55','chunk_size':65536,'preview':True});return
   if self.command=='POST' and path=='/api/v1/pair':value=self.object(('code',),('code',));status,result=self.server.app.pair(value['code']);self.send_json(status,result);return
   if not self.server.app.authorized(self.header('Authorization')):self.error(401,'请先输入配对码');return
   if self.command=='POST' and path=='/api/v1/uploads':
    value=self.object(('kind','name','size','sha256','replace'),('kind','name','size','sha256'));kind=KINDS.get(value['kind']) if isinstance(value['kind'],str) else None
    if kind is None:raise BadRequest('文件类别无效')
    name=basename(value['name']);size=integer(value['size'],512*1024*1024);sha=digest(value['sha256']);old=value.get('replace');old_size=0;old_sha='0'*64
    if old is not None:
     if not isinstance(old,dict) or set(old)!=set(('size','sha256')):raise BadRequest('覆盖确认信息无效')
     old_size=integer(old['size'],512*1024*1024);old_sha=digest(old['sha256'])
    identifier=secrets.token_hex(16);result=self.native(f'BEGIN {identifier} {kind} {size} {sha} {name} {int(old is not None)} {old_size} {old_sha}')
    if result is not None:self.send_json(201,public_state(result)|{'chunk_size':65536})
    return
   if self.command=='GET' and path=='/api/v1/session':self.send_json(200,{'paired':True});return
   match=re.fullmatch('/api/v1/uploads/([0-9a-fA-F]{32})(/chunks|/complete)?',path)
   if match:
    identifier=match[1].lower();action=match[2]
    if self.command=='GET' and not action:
     result=self.native('OPEN '+identifier)
     if result is not None:self.send_json(200,public_state(result))
     return
    if self.command=='PUT' and action=='/chunks':
     offset=self.header('X-Offset');sha=digest(self.header('X-Chunk-SHA256'))
     if offset is None or not re.fullmatch('[0-9]+',offset):raise BadRequest('分块位置无效')
     at=integer(int(offset),512*1024*1024);data=self.body(65536)
     if not data:raise BadRequest('分块不能为空')
     result=self.native(f'CHUNK {identifier} {at} {len(data)} {sha}',data)
     if result is not None:self.send_json(200,public_state(result))
     return
    if self.command=='POST' and action=='/complete':
     value=self.object(('sha256',),('sha256',));result=self.native(f'END {identifier} {digest(value["sha256"])}')
     if result is not None:self.send_json(200,public_state(result)|{'message':'文件已保存'})
     return
    if self.command=='DELETE' and not action:
     result=self.native('CANCEL '+identifier)
     if result is not None:self.send_json(200,public_state(result)|{'message':'上传已取消'})
     return
   match=re.fullmatch('/api/v1/files/(book|font|cover|wallpaper)',path)
   if self.command=='GET' and match:
    query=urllib.parse.parse_qs(parsed.query,strict_parsing=True)
    if set(query)!=set(('name',)) or len(query['name'])!=1:raise BadRequest('文件名参数无效')
    result=self.native(f'FILE {KINDS[match[1]]} {basename(query["name"][0])}')
    if result is not None:self.send_json(200,{'size':result['size'],'sha256':result['sha256']})
    return
   self.error(404,'没有这个操作入口')
  except BadRequest as error:self.error(400,str(error))
  except (ValueError,TypeError,UnicodeError):self.error(400,'请求参数无效')
  except (OSError,RuntimeError):self.error(503,'传输服务暂时不可用，请重试')
 do_GET=dispatch
 do_POST=dispatch
 do_PUT=dispatch
 do_DELETE=dispatch
 do_OPTIONS=dispatch

def make_server(app,bind='127.0.0.1',port=8787,authority=None):
 server=http.server.ThreadingHTTPServer((bind,port),Handler);server.daemon_threads=False;server.block_on_close=True;server.app=app;server.authority=authority or f'{bind}:{server.server_port}';return server

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--worker',required=True);parser.add_argument('--root',required=True);parser.add_argument('--bind',default='127.0.0.1');parser.add_argument('--port',type=int,default=8787);parser.add_argument('--authority');args=parser.parse_args()
 app=TransferApp(args.worker,args.root);server=make_server(app,args.bind,args.port,args.authority)
 print('传输地址：http://'+server.authority,flush=True);print('配对码：'+app.pair_code,flush=True)
 try:server.serve_forever()
 except KeyboardInterrupt:pass
 finally:server.server_close();app.close()
if __name__=='__main__':main()
