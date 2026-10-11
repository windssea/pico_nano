"""发现报文与真实UDP停止测试。 / Discovery packets and real UDP shutdown tests."""
import json
import os
import pathlib
import socket
import sys
import time
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]/'tools'))
from localsend_discovery import Discovery, parse_announcement

INFO = {'alias':'小纸 Pico（测试）','version':'2.2','fingerprint':'own','deviceType':'headless','port':53318,'protocol':'http','download':False}


class DiscoveryTests(unittest.TestCase):
    def packet(self,**values):
        obj = {**INFO,'alias':'发送电脑','fingerprint':'other','announce':True,**values}
        return json.dumps(obj,ensure_ascii=False).encode()
    def test_schema(self):
        self.assertEqual(parse_announcement(self.packet())['alias'],'发送电脑')
        for data in [b'[]',b'{"announce":true,"announce":false}',b'{"port":NaN}',b'\xff',b'x'*2049,
                     self.packet(port=True),self.packet(port=0),self.packet(protocol='ftp'),self.packet(version='3.0'),
                     self.packet(alias='\x00'),self.packet(alias='中'*129),self.packet(announce=1)]:
            with self.subTest(data=data[:70]): self.assertIsNone(parse_announcement(data))
    def test_filter_and_rate(self):
        discovery = Discovery(INFO,'127.0.0.1')
        self.assertIsNone(discovery.reply(self.packet(fingerprint='own'),'127.0.0.1',0))
        self.assertIsNone(discovery.reply(self.packet(announce=False),'127.0.0.1',0))
        self.assertIsNone(discovery.reply(self.packet(),'8.8.8.8',0))
        reply = json.loads(discovery.reply(self.packet(),'127.0.0.1',0))
        self.assertFalse(reply['announce']); self.assertEqual(reply['port'],53318)
        self.assertIsNone(discovery.reply(self.packet(),'127.0.0.1',.5))
        self.assertIsNotNone(discovery.reply(self.packet(),'127.0.0.1',2))
        for i in range(1,100): discovery.reply(self.packet(),f'192.168.1.{i}',2+i*.25)
        self.assertLessEqual(len(discovery.peers),32)
        self.assertEqual(len(discovery.peers),32)
        self.assertIsNotNone(discovery.reply(self.packet(),'192.168.2.1',100))
        self.assertEqual(len(discovery.peers),1)
    def test_udp_and_join(self):
        receiver = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        receiver.bind(('127.0.0.1',0)); receiver.settimeout(2)
        discovery = Discovery(INFO,'127.0.0.1',listen_port=0,target=receiver.getsockname())
        discovery.start()
        self.assertTrue(json.loads(receiver.recv(2048))['announce'])
        sender = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        sender.sendto(self.packet(),('127.0.0.1',discovery.port))
        self.assertFalse(json.loads(receiver.recv(2048))['announce'])
        discovery.close(); self.assertFalse(discovery.thread.is_alive())
        discovery.close(); sender.close(); receiver.close()
    def test_shutdown_error_still_closes_owner(self):
        from localsend_server import close_services
        calls = []
        class App:
            def stop(self): calls.append('stop')
            def close(self): calls.append('owner')
        class Server:
            def server_close(self): calls.append('http')
        class BrokenDiscovery:
            def close(self): calls.append('udp'); raise RuntimeError('发现停止失败')
        with self.assertRaises(RuntimeError): close_services(App(),Server(),BrokenDiscovery())
        self.assertEqual(calls,['stop','udp','http','owner'])
    @unittest.skipUnless(os.environ.get('PN_LS_TEST_INTERFACE'),'需要明确本机网卡，默认不发LAN组播')
    def test_real_multicast(self):
        interface = os.environ['PN_LS_TEST_INTERFACE']; group = '224.0.0.167'
        port = int(os.environ.get('PN_LS_TEST_PORT','53317'))
        listener = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        listener.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); listener.bind(('',port))
        listener.setsockopt(socket.IPPROTO_IP,socket.IP_ADD_MEMBERSHIP,socket.inet_aton(group)+socket.inet_aton(interface))
        listener.settimeout(3)
        discovery = Discovery(INFO,interface,listen_port=port,target=(group,port))
        sender = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        sender.setsockopt(socket.IPPROTO_IP,socket.IP_MULTICAST_IF,socket.inet_aton(interface))
        sender.setsockopt(socket.IPPROTO_IP,socket.IP_MULTICAST_TTL,1)
        try:
            discovery.start()
            deadline = time.monotonic()+3
            while time.monotonic()<deadline:
                obj = json.loads(listener.recv(2048))
                if obj.get('fingerprint')=='own' and obj.get('announce') is True: break
            else: self.fail('未收到本机启动公告')
            sender.sendto(self.packet(),(group,port))
            deadline = time.monotonic()+3
            while time.monotonic()<deadline:
                obj = json.loads(listener.recv(2048))
                if obj.get('fingerprint')=='own' and obj.get('announce') is False: break
            else: self.fail('未收到组播回退响应')
        finally: discovery.close(); sender.close(); listener.close()


if __name__=='__main__': unittest.main()
