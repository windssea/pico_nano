"""PC LocalSend明文HTTP开发适配；本机控制确认，原生owner安装。 / PC LocalSend HTTP development adapter; local consent and native-owner installation."""
import argparse
import http.server
import ipaddress
import json
import pathlib
import re
import secrets
import select
import socket
import subprocess
import threading
import time
import urllib.parse

PREFIX = '/api/localsend/v2/'
HTTP_CODES = {1:400, 2:409, 3:404, 4:413, 5:503, 6:503, 7:403, 8:403, 9:400, 10:422, 11:500}


class LocalSendApp:
    def __init__(self, executable, root):
        pathlib.Path(root).mkdir(parents=True, exist_ok=True)
        self.process = subprocess.Popen([str(executable), str(root)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,start_new_session=True)
        self.owner = threading.Lock()
        self.condition = threading.Condition()
        self.waiting = None
        self.decision = None
        self.peer = None
        self.grants = None
        self.active = False
        self.closed = False
        self.abort = threading.Event()
        self.info = {'alias':'小纸 Pico（PC 测试）', 'version':'2.2', 'deviceModel':'PC simulator',
                     'deviceType':'headless', 'fingerprint':secrets.token_hex(32), 'download':False,
                     'protocol':'http', 'port':53317}
        if not self._read().get('ready'): raise RuntimeError('原生接收进程启动失败')

    def _read(self):
        line = self.process.stdout.readline(16384)
        if not line or not line.endswith(b'\n'): raise RuntimeError('原生接收进程未返回完整结果')
        return json.loads(line)

    def _call(self, command, data=b''):
        try:
            self.process.stdin.write(command.encode('ascii') + b'\n' + data)
            self.process.stdin.flush()
            return self._read()
        except (OSError,RuntimeError,ValueError) as exc:
            self.stop()
            raise RuntimeError('原生接收通道失效；请停止后重新启动') from exc

    def pending(self):
        with self.condition:
            # 返回独立展示快照，不将令牌交给待确认界面。/ Return an independent display snapshot without grant tokens.
            return json.loads(json.dumps(self.waiting)) if self.waiting else None

    def prepare(self, peer, body):
        if not self.owner.acquire(blocking=False): return {'code':2}
        try:
            with self.condition:
                if self.closed: return {'code':11}
                if self.waiting is not None: return {'code':2}
                result = self._call(f'PREP {peer} {len(body)}', body)
                if result['code']: return result
                self.waiting = result; self.decision = None; self.peer = peer; self.grants = None
                self.condition.notify_all()
        finally: self.owner.release()
        deadline = time.monotonic() + 60
        with self.condition:
            while self.decision is None and not self.closed:
                left = deadline - time.monotonic()
                if left <= 0: break
                self.condition.wait(left)
            result = self.decision
        if result is None:
            with self.owner: self._call('LOCAL_CANCEL')
            result = {'code':8}
        with self.condition: self.waiting = None
        return result

    def decide(self, generation, choices):
        if type(generation) is not int or not 0<=generation<2**64 or not isinstance(choices,str) or not re.fullmatch('[0134]{1,8}',choices): return {'code':1}
        if not self.owner.acquire(blocking=False): return {'code':2}
        try:
            with self.condition:
                if self.closed or not self.waiting or self.decision is not None: return {'code':2}
                result = self._call(f'DECIDE {generation} {choices}')
                if result['code'] not in (0,8): return result
                self.decision = result; self.grants = result.get('grants'); self.condition.notify_all()
                return result
        finally: self.owner.release()

    def cancel(self, peer, sid):
        with self.condition:
            if self.closed or peer != self.peer or not self.grants or not secrets.compare_digest(sid,self.grants['sessionId']): return {'code':7}
            if self.active:
                self.abort.set(); return {'code':0}
        if not self.owner.acquire(blocking=False): return {'code':2}
        try: return self._call(f'CANCEL {peer} {sid}')
        finally: self.owner.release()

    def upload(self, peer, sid, file_id, token, size, stream, connection):
        if not self.owner.acquire(blocking=False): return {'code':2}
        started = False; final = False
        try:
            with self.condition:
                if self.closed: return {'code':11}
                result = self._call(f'AUTH {peer} {sid} {file_id.encode("utf8").hex()} {token}')
                if not result.get('body'): return result
                started = True; self.active = True; self.abort.clear()
            if result['size'] != size:
                final = True; self._call('ABORT'); return {'code':1}
            connection.settimeout(1)
            remaining = size; idle = time.monotonic(); deadline = idle + 1800
            while remaining:
                if self.abort.is_set() or time.monotonic() >= deadline or time.monotonic() - idle >= 10:
                    final = True; return self._call('ABORT')
                if not select.select([connection],[],[],.25)[0]: continue
                try: data = connection.recv(min(8192,remaining))
                except socket.timeout: continue
                if not data:
                    final = True; return self._call('ABORT')
                result = self._call(f'DATA {len(data)}',data)
                if not result.get('frame'):
                    final = True; return result
                remaining -= len(data); idle = time.monotonic()
            final = True
            result = self._call('ABORT' if self.abort.is_set() else 'EOF')
            with self.condition:
                if self.abort.is_set(): self._call(f'CANCEL {peer} {sid}')
            return result
        finally:
            try:
                if started and not final and self.process.poll() is None:
                    self._call('ABORT')
            finally:
                with self.condition: self.active = False; self.condition.notify_all()
                self.owner.release()

    def stop(self):
        with self.condition:
            self.closed = True; self.abort.set(); self.condition.notify_all()

    def close(self):
        # 服务先停止入口并join处理线程；然后才关闭原生owner。/ Stop admission and join request handlers before closing the native owner.
        self.stop()
        with self.owner:
            try:
                if self.process.poll() is None: self._call('EXIT')
            except RuntimeError: pass
            finally:
                for pipe in (self.process.stdin,self.process.stdout):
                    try: pipe.close()
                    except OSError: pass
                self.process.wait(timeout=15)


class LocalSendHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    # 头部读取不预取正文；上传直接从socket轮询，可及时取消且不复读超时的BufferedReader。
    # Header reads never prefetch bodies; upload polls the socket for cancellation without reusing timed-out buffered readers.
    rbufsize = 0
    def setup(self):
        super().setup(); self.connection.settimeout(10)
    def log_message(self,*args): pass
    def respond(self, status, value=None):
        # 每次请求关闭连接，失败未读正文不能被解析成下一请求。/ Close each connection so unread failed bodies cannot become subsequent requests.
        self.close_connection = True
        data = json.dumps(value,ensure_ascii=False,separators=(',',':')).encode() if value is not None else b''
        self.send_response(status); self.send_header('Content-Length',str(len(data)))
        self.send_header('Connection','close'); self.send_header('Cache-Control','no-store')
        if data: self.send_header('Content-Type','application/json; charset=utf-8')
        self.end_headers(); self.wfile.write(data)
    def length(self, maximum):
        values = self.headers.get_all('Content-Length',[])
        if self.headers.get_all('Transfer-Encoding') or len(values)!=1 or not re.fullmatch('[0-9]{1,10}',values[0]): raise ValueError('正文长度无效')
        size = int(values[0])
        if size>maximum: raise ValueError('正文超过限制')
        return size
    def handle_expect_100(self):
        self.respond(417,{'message':'开发服务暂不支持预先确认正文'}); return False
    def do_GET(self):
        if self.path != PREFIX+'info': self.respond(404); return
        self.respond(200,self.server.app.info)
    def do_POST(self):
        try:
            if self.server.app.closed: self.respond(503); return
            if len(self.path)>1024: raise ValueError('地址过长')
            url = urllib.parse.urlsplit(self.path)
            if url.scheme or url.netloc or not url.path.startswith(PREFIX): self.respond(404); return
            query = urllib.parse.parse_qs(url.query,keep_blank_values=True,strict_parsing=True,max_num_fields=4)
            if any(len(v)!=1 for v in query.values()): raise ValueError('参数重复')
            peer = str(ipaddress.ip_address(self.client_address[0]))
            operation = url.path[len(PREFIX):]
            if operation in ('prepare-upload','register'):
                if query: raise ValueError('开发服务未启用PIN')
                size = self.length(8192)
                if not size: raise ValueError('元数据为空')
                chunks = []; left = size; deadline = time.monotonic()+10
                while left:
                    if time.monotonic()>=deadline: raise ValueError('元数据接收超时')
                    self.connection.settimeout(max(.001,deadline-time.monotonic()))
                    part = self.rfile.read(left)
                    if not part: raise ValueError('元数据不完整')
                    chunks.append(part); left -= len(part)
                body = b''.join(chunks)
                if operation=='register':
                    obj = json.loads(body)
                    if not isinstance(obj,dict) or not all(isinstance(obj.get(k),str) and 0<len(obj[k])<=128 for k in ('alias','version','fingerprint')) or not obj['version'].startswith('2.'): raise ValueError('设备信息无效')
                    self.respond(200,self.server.app.info); return
                result = self.server.app.prepare(peer,body)
                self.respond(HTTP_CODES.get(result['code'],200),result.get('grants') if not result['code'] else {'message':'请求未获接收授权'}); return
            if operation=='upload':
                if set(query)!={'sessionId','fileId','token'}: raise ValueError('缺少上传参数')
                sid, file_id, token = (query[k][0] for k in ('sessionId','fileId','token'))
                if not re.fullmatch('[0-9a-f]{32}',sid) or not re.fullmatch('[0-9a-f]{32}',token) or not 0<len(file_id.encode())<=64 or '\0' in file_id: raise ValueError('上传参数无效')
                result = self.server.app.upload(peer,sid,file_id,token,self.length(512*1024*1024),self.rfile,self.connection)
                self.respond(HTTP_CODES.get(result['code'],200)); return
            if operation=='cancel':
                if set(query)!={'sessionId'} or not re.fullmatch('[0-9a-f]{32}',query['sessionId'][0]) or self.length(0): raise ValueError('取消参数无效')
                result = self.server.app.cancel(peer,query['sessionId'][0]); self.respond(HTTP_CODES.get(result['code'],200)); return
            self.respond(404)
        except (ValueError,UnicodeError): self.respond(400,{'message':'请求格式无效'})
        except (OSError,RuntimeError): self.respond(500,{'message':'接收失败，请重新发起'})


class LocalSendServer(http.server.ThreadingHTTPServer):
    daemon_threads = False
    def __init__(self,address,app):
        self.app = app; self.slots = threading.BoundedSemaphore(8)
        super().__init__(address,LocalSendHandler)
        app.info['port'] = self.server_address[1]
    def process_request(self,request,address):
        if not self.slots.acquire(blocking=False):
            request.settimeout(1)
            try: request.sendall(b'HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
            except OSError: pass
            self.shutdown_request(request); return
        try: super().process_request(request,address)
        except BaseException: self.slots.release(); raise
    def process_request_thread(self,request,address):
        try: super().process_request_thread(request,address)
        finally: self.slots.release()


def console(app):
    previous = None
    while not app.closed:
        pending = app.pending()
        if not pending or pending['generation']==previous: time.sleep(.1); continue
        previous = pending['generation']
        print(f'\n发送方：{pending["alias"]}；确认编号：{previous}',flush=True)
        for i,file in enumerate(pending['files'],1): print(f'{i}. {file["name"]}（{file["size"]} 字节）',flush=True)
        print('逐文件输入选择：0拒绝，1书籍/字体，3封面，4锁屏。例如两文件输入13。',flush=True)
        while not app.closed:
            try: choices = input('接收选择：').strip()
            except EOFError: return
            result = app.decide(previous,choices)
            if result['code']==1:
                print('选择无效，请按文件数量输入；图片须选3或4。',flush=True); continue
            print('已确认' if result['code']==0 else '未接收；请重新发起',flush=True); break


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker',type=pathlib.Path,required=True)
    parser.add_argument('--root',type=pathlib.Path,required=True)
    parser.add_argument('--bind',default='127.0.0.1')
    parser.add_argument('--port',type=int,default=53317)
    args = parser.parse_args()
    app = LocalSendApp(args.worker,args.root)
    server = LocalSendServer((args.bind,args.port),app)
    threading.Thread(target=console,args=(app,),daemon=True).start()
    print(f'PC 明文HTTP开发服务：http://{args.bind}:{server.server_address[1]}；仅本机控制台可确认。尚未接入UDP发现/设备HTTPS。',flush=True)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: app.stop(); server.server_close(); app.close()


if __name__=='__main__': main()
