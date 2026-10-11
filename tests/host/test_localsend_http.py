"""真实HTTP与原生owner联测，测试方明确确认。 / Real HTTP/native-owner integration with explicit test-operator consent."""
import concurrent.futures
import hashlib
import http.client
import json
import pathlib
import sys
import tempfile
import threading
import time
import urllib.parse

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'tools'))
from localsend_server import LocalSendApp, LocalSendServer


def main(binary,resources=()):
    with tempfile.TemporaryDirectory() as directory:
        docker_worker = binary=='--docker-worker'
        if docker_worker:
            workspace = pathlib.Path(__file__).resolve().parents[2]
            binary = ['docker','--context','desktop-linux','run','--rm','-i','--mount',f'type=bind,source={workspace},target=/work',
                      '--mount',f'type=bind,source={directory},target=/sd','--entrypoint','/work/build-host/pn_localsend_host','pico-nano-sdl:dev','/sd']
        app = LocalSendApp(binary, directory)
        server = LocalSendServer(('127.0.0.1', 0), app)
        thread = threading.Thread(target=server.serve_forever); thread.start()
        def request(path, data=b'', headers=None, method='POST'):
            conn = http.client.HTTPConnection(*server.server_address, timeout=15)
            conn.request(method, path, data, headers or {})
            result = conn.getresponse(); body = result.read(); code = result.status
            conn.close(); return code, json.loads(body) if body else None
        def prepare(name, data, accept='1', digest=None):
            info = {'info': {'alias': '测试电脑', 'version': '2.2', 'fingerprint': 'test'},
                    'files': {'中文 ID': {'id': '中文 ID', 'fileName': name, 'size': len(data), 'sha256': digest}}}
            with concurrent.futures.ThreadPoolExecutor() as executor:
                future = executor.submit(request, '/api/localsend/v2/prepare-upload', json.dumps(info).encode())
                deadline = time.monotonic() + 3
                while not app.pending() and time.monotonic() < deadline: time.sleep(.01)
                pending = app.pending(); assert pending
                assert not (pathlib.Path(directory) / 'books' / name).exists()
                assert app.decide(pending['generation'] - 1, accept)['code'] != 0
                assert app.decide(pending['generation'], accept)['code'] in (0, 8)
                return future.result()
        try:
            assert request('/api/localsend/v2/info', method='GET')[1]['download'] is False
            assert request('/api/localsend/v2/register', b'{}')[0] == 400
            assert request('/api/localsend/v2/register', json.dumps({'alias':'电脑','version':'2.2','fingerprint':'sender'}).encode())[0] == 200
            data = ('测试正文\n' * 20000).encode()
            code, grants = prepare('正文.txt', data); assert code == 200
            query = urllib.parse.urlencode({'sessionId': grants['sessionId'], 'fileId': '中文 ID', 'token': grants['files']['中文 ID']})
            assert request('/api/localsend/v2/upload?' + query.replace(grants['files']['中文 ID'], '0'*32), data)[0] == 403
            assert request('/api/localsend/v2/upload?' + query, data)[0] == 200
            assert (pathlib.Path(directory) / 'books/正文.txt').read_bytes() == data
            assert request('/api/localsend/v2/upload?' + query, data)[0] == 409
            assert prepare('拒绝.txt', b'hello', '0')[0] == 403
            code, grants = prepare('摘要错误.txt', b'hello', digest='0'*64); assert code == 200
            query = urllib.parse.urlencode({'sessionId': grants['sessionId'], 'fileId': '中文 ID', 'token': grants['files']['中文 ID']})
            assert request('/api/localsend/v2/upload?' + query, b'hello')[0] == 422
            assert not (pathlib.Path(directory) / 'books/摘要错误.txt').exists()
            code, grants = prepare('长度错误.txt', b'hello'); assert code == 200
            query = urllib.parse.urlencode({'sessionId': grants['sessionId'], 'fileId': '中文 ID', 'token': grants['files']['中文 ID']})
            assert request('/api/localsend/v2/upload?' + query, b'short!')[0] == 400
            assert request('/api/localsend/v2/cancel?sessionId=' + grants['sessionId'])[0] == 200
            for name, choice, folder in [('封面.png','3','covers'), ('锁屏.png','4','wallpapers')]:
                data = (pathlib.Path(__file__).parents[1] / 'fixtures/image-black.png').read_bytes()
                code, grants = prepare(name, data, choice); assert code == 200
                query = urllib.parse.urlencode({'sessionId': grants['sessionId'], 'fileId': '中文 ID', 'token': grants['files']['中文 ID']})
                assert request('/api/localsend/v2/upload?' + query, data)[0] == 200
                assert (pathlib.Path(directory) / folder / name).read_bytes() == data
            # 流未结束时并发取消，随后仍可发起新会话。/ Cancel an unfinished body concurrently, then permit a new session.
            code, grants = prepare('取消.txt', b'x'*100000); assert code == 200
            query = urllib.parse.urlencode({'sessionId':grants['sessionId'],'fileId':'中文 ID','token':grants['files']['中文 ID']})
            conn = http.client.HTTPConnection(*server.server_address,timeout=5)
            conn.putrequest('POST','/api/localsend/v2/upload?'+query)
            conn.putheader('Content-Length','100000'); conn.endheaders(); conn.send(b'x'*100)
            deadline = time.monotonic()+3
            while not app.active and time.monotonic()<deadline: time.sleep(.01)
            assert app.active
            assert request('/api/localsend/v2/cancel?sessionId='+'0'*32)[0] == 403
            assert request('/api/localsend/v2/cancel?sessionId='+grants['sessionId'])[0] == 200
            response = conn.getresponse(); assert response.status == 403; response.read(); conn.close()
            assert not (pathlib.Path(directory)/'books/取消.txt').exists()
            assert prepare('后续.txt','正文'.encode())[0] == 200
            assert request('/api/localsend/v2/cancel?sessionId='+app.grants['sessionId'])[0] == 200
            for path in [pathlib.Path(__file__).resolve().parents[2]/'assets/fonts/read-pico-ui.ttf', *map(pathlib.Path,resources)]:
                data = path.read_bytes()
                code, grants = prepare(path.name,data); assert code == 200
                query = urllib.parse.urlencode({'sessionId':grants['sessionId'],'fileId':'中文 ID','token':grants['files']['中文 ID']})
                assert request('/api/localsend/v2/upload?'+query,data)[0] == 200
                folder = 'fonts' if path.suffix.lower()=='.ttf' else 'books'
                installed = (pathlib.Path(directory)/folder/path.name).read_bytes()
                assert hashlib.sha256(installed).digest()==hashlib.sha256(data).digest()
                print(f'HTTP/native SHA256 {path.name}: {hashlib.sha256(installed).hexdigest()}',flush=True)
            if not docker_worker:
                app.process.terminate(); app.process.wait(timeout=5)
                assert request('/api/localsend/v2/prepare-upload',b'{}')[0] == 500
                assert app.closed and app.owner.acquire(blocking=False)
                app.owner.release()
                assert request('/api/localsend/v2/prepare-upload',b'{}')[0] == 503
        finally:
            server.shutdown(); app.stop(); server.server_close(); thread.join(); app.close()


if __name__ == '__main__': main(sys.argv[1],sys.argv[2:])
