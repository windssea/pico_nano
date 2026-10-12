"""真实进程的确认、流、取消与目录测试。 / Confirmation, streams, cancellation and directories through the real process."""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile


def main(binary):
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        p = subprocess.Popen([binary, directory], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        def reply():
            line = p.stdout.readline(16384)
            assert line, p.poll()
            return json.loads(line)
        def call(command, data=b''):
            p.stdin.write(command.encode('ascii') + b'\n' + data)
            p.stdin.flush()
            return reply()
        def prepare(name, data, file_id='中文 id'):
            body = json.dumps({'info': {'alias': '发送电脑', 'version': '2.2', 'fingerprint': 'sender'},
                               'files': {file_id: {'id': file_id, 'fileName': name, 'size': len(data)}}},
                              ensure_ascii=False).encode('utf8')
            return call(f'PREP 127.0.0.1 {len(body)}', body)
        def authorize(grants, peer='127.0.0.1'):
            file_id, token = next(iter(grants['files'].items()))
            return call(f'AUTH {peer} {grants["sessionId"]} {file_id.encode("utf8").hex()} {token}')
        assert reply()['ready']
        data = ('中文正文\n' * 15000).encode('utf8')
        pending = prepare('正文.txt', data)
        assert pending['code'] == 0 and pending['phase'] == 1
        assert not (root / 'books/正文.txt').exists()
        assert call('DECIDE 0 1')['code'] != 0
        grants = call(f'DECIDE {pending["generation"]} 1')['grants']
        assert authorize(grants, '127.0.0.2')['code'] != 0
        assert authorize(grants)['body']
        for start in range(0, len(data), 511):
            chunk = data[start:start + 511]
            assert call(f'DATA {len(chunk)}', chunk)['code'] == 0
        result = call('EOF')
        assert result['committed'] and result['installed'] == 1
        assert hashlib.sha256((root / 'books/正文.txt').read_bytes()).hexdigest() == result['sha256']
        assert authorize(grants)['code'] != 0
        pending = prepare('正文.txt', data)
        grants = call(f'DECIDE {pending["generation"]} 1')['grants']
        result = authorize(grants)
        assert result['code'] != 0 and not result.get('body') and result['failed'] == 1
        pending = prepare('截断.txt', data)
        grants = call(f'DECIDE {pending["generation"]} 1')['grants']
        assert authorize(grants)['body']
        assert call('DATA 7', b'partial')['code'] == 0
        result = call('ABORT')
        assert result['code'] == 8 and not result['committed']
        assert not (root / 'books/截断.txt').exists()
        assert prepare('../escape.txt', b'bad')['code'] != 0
        pending = prepare('拒绝.txt', b'bad')
        assert call(f'DECIDE {pending["generation"]} 0')['code'] == 8
        assert not (root / 'books/拒绝.txt').exists()
        assert call('EXIT')['code'] == 0
        p.stdin.close(); p.stdout.close()
        assert p.wait(timeout=10) == 0


if __name__ == '__main__':
    main(sys.argv[1])
