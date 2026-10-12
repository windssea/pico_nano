"""有界LocalSend IPv4发现；UDP回退响应不触发远端HTTP。 / Bounded LocalSend IPv4 discovery; UDP fallback replies never initiate remote HTTP."""
import ipaddress
import json
import re
import socket
import threading
import time

GROUP = '224.0.0.167'
PORT = 53317
MAX_PACKET = 2048


def unique_object(pairs):
    obj = {}
    for key,value in pairs:
        if key in obj: raise ValueError('报文字段重复')
        obj[key] = value
    return obj


def parse_announcement(data):
    if not 0<len(data)<=MAX_PACKET: return None
    try:
        obj = json.loads(data.decode('utf8'),object_pairs_hook=unique_object,
                         parse_constant=lambda _: (_ for _ in ()).throw(ValueError('非法数值')))
        if not isinstance(obj,dict): return None
        for key in ('alias','fingerprint'):
            value = obj.get(key)
            if not isinstance(value,str) or not 0<len(value.encode('utf8'))<=128 or any(ord(c)<32 or ord(c)==127 for c in value): return None
        if not isinstance(obj.get('version'),str) or not re.fullmatch(r'2\.[0-9]{1,3}',obj['version']): return None
        if type(obj.get('port')) is not int or not 1<=obj['port']<=65535: return None
        if obj.get('protocol') not in ('http','https') or type(obj.get('announce')) is not bool: return None
        return obj
    except (ValueError,UnicodeError,RecursionError): return None


class Discovery:
    def __init__(self,info,interface,listen_port=PORT,target=(GROUP,PORT)):
        address = ipaddress.IPv4Address(interface)
        if address.is_unspecified or address.is_multicast or address.is_reserved or address.is_global: raise ValueError('请选择本机局域网IPv4网卡')
        self.info = dict(info); self.interface = str(address)
        self.port = listen_port; self.target = target
        self.peers = {}; self.bucket_at = None; self.bucket = 4.0
        self.socket = None; self.thread = None; self.stopped = threading.Event()
        self.error = None
        # reply仅由一个发现owner调用；不持久化远端名称或授权信息。
        # A single discovery owner calls reply; remote names and authorization are never persisted.

    def packet(self,announce):
        return json.dumps({**self.info,'announce':announce},ensure_ascii=False,separators=(',',':')).encode('utf8')

    def reply(self,data,origin,now):
        obj = parse_announcement(data)
        if not obj or not obj['announce'] or obj['fingerprint']==self.info['fingerprint']: return None
        try: address = ipaddress.IPv4Address(origin)
        except ipaddress.AddressValueError: return None
        if address.is_unspecified or address.is_multicast or address.is_reserved or address.is_global: return None
        if self.bucket_at is not None: self.bucket = min(4.0,self.bucket+max(0,now-self.bucket_at)*4)
        self.bucket_at = now
        self.peers = {ip:t for ip,t in self.peers.items() if now-t<30}
        if self.bucket<1 or (origin in self.peers and now-self.peers[origin]<1) or (origin not in self.peers and len(self.peers)>=32): return None
        self.bucket -= 1; self.peers[origin] = now
        return self.packet(False)

    def start(self):
        if self.thread is not None: raise RuntimeError('发现服务不可重复启动')
        sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        try:
            sock.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
            sock.bind(('',self.port)); self.port = sock.getsockname()[1]
            if self.target[0]==GROUP:
                membership = socket.inet_aton(GROUP)+socket.inet_aton(self.interface)
                sock.setsockopt(socket.IPPROTO_IP,socket.IP_ADD_MEMBERSHIP,membership)
                sock.setsockopt(socket.IPPROTO_IP,socket.IP_MULTICAST_IF,socket.inet_aton(self.interface))
                sock.setsockopt(socket.IPPROTO_IP,socket.IP_MULTICAST_TTL,1)
            sock.settimeout(.25)
            packet = self.packet(True)
            if len(packet)>MAX_PACKET: raise ValueError('本机公告超过限制')
            sock.sendto(packet,self.target)
        except BaseException: sock.close(); raise
        self.socket = sock
        self.thread = threading.Thread(target=self._run,name='localsend-discovery')
        self.thread.start()

    def _run(self):
        while not self.stopped.is_set():
            try:
                data,origin = self.socket.recvfrom(MAX_PACKET+1)
                response = self.reply(data,origin[0],time.monotonic())
                if response is not None: self.socket.sendto(response,self.target)
            except socket.timeout: continue
            except OSError as exc:
                if not self.stopped.is_set(): self.error = str(exc)
                break

    def close(self):
        self.stopped.set()
        if self.thread is not None: self.thread.join(timeout=2)
        if self.socket is not None: self.socket.close(); self.socket = None
        if self.thread is not None and self.thread.is_alive(): raise RuntimeError('发现线程未停止')
