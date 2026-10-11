"""仅本机的可信操作界面，不向发送端暴露确认权限。 / Local trusted-operator UI, never exposing consent privileges to senders."""
import http.server
import json
import pathlib
import secrets
import threading

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def setup(self):super().setup();self.connection.settimeout(5)
    def send(self,status,data=b'',mime='application/json; charset=utf-8'):
        self.close_connection=True;self.send_response(status);self.send_header('Content-Length',str(len(data)))
        self.send_header('Content-Type',mime);self.send_header('Connection','close');self.send_header('Cache-Control','no-store')
        self.send_header('Referrer-Policy','no-referrer');self.send_header('X-Frame-Options','DENY')
        self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'")
        self.end_headers();self.wfile.write(data)
    def header(self,name):
        values=self.headers.get_all(name,[])
        return values[0] if len(values)==1 else None
    def trusted(self,mutation=False):
        owner=self.server.control
        if self.header('Host')!=owner.authority.removeprefix('http://'):return False
        if mutation and self.header('Origin')!=owner.authority:return False
        return secrets.compare_digest(self.header('Authorization') or '', 'Bearer '+owner.token)
    def do_GET(self):
        if self.header('Host')!=self.server.control.authority.removeprefix('http://'):self.send(403);return
        if self.path in ('/','/control.js'):
            name='index.html' if self.path=='/' else 'control.js'
            data=(pathlib.Path(__file__).resolve().parents[1]/'assets/localsend-control'/name).read_bytes()
            self.send(200,data,'text/html; charset=utf-8' if name.endswith('html') else 'text/javascript; charset=utf-8');return
        if self.path!='/state':self.send(404);return
        if not self.trusted():self.send(403);return
        self.send(200,json.dumps(self.server.control.app.control_state(),ensure_ascii=False).encode())
    def do_POST(self):
        if not self.trusted(True):self.send(403);return
        try:
            if self.headers.get_all('Transfer-Encoding'):raise ValueError()
            raw=self.header('Content-Length')
            if raw is None or not raw.isdigit() or not 0<int(raw)<=1024:raise ValueError()
            body=self.rfile.read(int(raw));obj=json.loads(body)
            if not isinstance(obj,dict):raise ValueError()
            app=self.server.control.app
            if self.path=='/decide':result=app.decide(obj.get('generation'),obj.get('choices'))
            elif self.path=='/cancel':result=app.control_cancel(obj.get('generation'))
            else:self.send(404);return
            code=result['code'];status=200 if code==0 or (code==8 and result.get('phase')==6) else 403 if code==8 else 400 if code==1 else 409
            self.send(status,json.dumps({'code':code}).encode())
        except (ValueError,TypeError,UnicodeError):self.send(400)
        except (OSError,RuntimeError):self.send(503)

class Server(http.server.ThreadingHTTPServer):
    def process_request(self,request,address):
        if not self.slots.acquire(blocking=False):self.shutdown_request(request);return
        try:super().process_request(request,address)
        except BaseException:self.slots.release();raise
    def process_request_thread(self,request,address):
        try:super().process_request_thread(request,address)
        finally:self.slots.release()

class LocalControl:
    def __init__(self,app):
        self.app=app;self.token=secrets.token_urlsafe(32)
        self.server=Server(('127.0.0.1',0),Handler)
        self.server.slots=threading.BoundedSemaphore(4)
        self.server.control=self
        self.authority=f'http://127.0.0.1:{self.server.server_port}'
        self.url=self.authority+'/#'+self.token
        self.thread=threading.Thread(target=self.server.serve_forever,name='localsend-control');self.thread.start()
    def close(self):
        self.server.shutdown();self.server.server_close();self.thread.join();self.token=''
