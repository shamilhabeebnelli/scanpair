"""End-to-end tests with generated fictional PDFs. Python standard library only.
Also: python3 integration_test.py --demo demo-directory
"""
import hashlib
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile


def pdf(path, seeds):
    objects = [b'<< /Type /Catalog /Pages 2 0 R >>', b'']
    kids = []
    for seed in seeds:
        rng = random.Random(seed)
        commands = ['0.08 0.12 0.18 rg']
        for _ in range(20):
            x, y = rng.randint(30, 400), rng.randint(30, 620)
            w, h = rng.randint(20, 100), rng.randint(12, 70)
            commands.append(f'{x} {y} {w} {h} re f')
        body = '\n'.join(commands).encode()
        page_id = len(objects) + 1
        kids.append(f'{page_id} 0 R')
        objects.append(f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 500 700] /Contents {page_id+1} 0 R /Resources << >> >>'.encode())
        objects.append(f'<< /Length {len(body)} >>\nstream\n'.encode() + body + b'\nendstream')
    objects[1] = f'<< /Type /Pages /Kids [{" ".join(kids)}] /Count {len(kids)} >>'.encode()
    data = bytearray(b'%PDF-1.4\n')
    offsets = [0]
    for index, obj in enumerate(objects, 1):
        offsets.append(len(data))
        data.extend(f'{index} 0 obj\n'.encode() + obj + b'\nendobj\n')
    xref = len(data)
    data.extend(f'xref\n0 {len(objects)+1}\n0000000000 65535 f \n'.encode())
    for offset in offsets[1:]:
        data.extend(f'{offset:010} 00000 n \n'.encode())
    data.extend(f'trailer\n<< /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode())
    Path(path).write_bytes(data)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--demo':
        dest = Path(sys.argv[2]); dest.mkdir(parents=True, exist_ok=True)
        pdf(dest/'scan-a.pdf', [10, 20, 30, 40])
        pdf(dest/'scan-b.pdf', [10, 30, 40, 50])
        print('Fictional demo: A page 2 lacks B counterpart; B page 4 lacks A counterpart.')
        return
    exe = str(Path(sys.argv[1]).resolve())
    checks = 0
    def check(value, message):
        nonlocal checks
        checks += 1
        if not value:
            raise AssertionError(message)
    with tempfile.TemporaryDirectory(prefix='scanpair-e2e-') as directory:
        root = Path(directory)
        # Spaces, shell metacharacters and a leading dash exercise argv handling.
        a, b = root/'-scan A;literal.pdf', root/'scan B.pdf'
        pdf(a, [10, 20, 30, 40]); pdf(b, [10, 30, 40, 50])
        originals = [hashlib.sha256(p.read_bytes()).hexdigest() for p in (a,b)]
        out = root/'review.html'
        def run(*args, env=None):
            return subprocess.run([exe, *map(str,args)], cwd=root, env=env, capture_output=True, text=True, timeout=60)
        result = run(a,b,out)
        check(result.returncode == 0, result.stderr)
        check('Unmatched candidates: 2' in result.stdout, 'expected two unmatched pages')
        html = out.read_text()
        check(html.count('Unmatched candidate</span>') == 2, 'two review cards')
        check('Scan A &middot; page 2' in html and 'Scan B &middot; page 4' in html, 'correct missing-page indices')
        check('data:image/bmp;base64,' in html, 'embedded previews')
        check('<script' not in html and 'https://' not in html, 'report is offline and scriptless')
        check([hashlib.sha256(p.read_bytes()).hexdigest() for p in (a,b)] == originals, 'source PDFs unchanged')
        check(out.stat().st_mode & 0o077 == 0, 'report owner-only permissions')
        before = out.read_bytes()
        check(run(a,b,out).returncode != 0 and out.read_bytes() == before, 'no overwrite')
        check(run(a,b,a).returncode != 0 and hashlib.sha256(a.read_bytes()).hexdigest() == originals[0], 'input destination refused')
        check(run(root/'missing.pdf',b,root/'missing.html').returncode != 0, 'missing input')
        bad = root/'broken.pdf'; bad.write_text('not a PDF')
        badout=root/'broken.html'
        check(run(bad,b,badout).returncode != 0 and not badout.exists(), 'malformed PDF no output')
        check(run(a,b,root/'out.txt').returncode != 0, 'HTML extension required')
        same = run(a,a,root/'same.html')
        check(same.returncode == 0 and 'Unmatched candidates: 0' in same.stdout, 'identical real PDFs')
        env=dict(os.environ,PATH='/nonexistent')
        missingtool=root/'no-tool.html'
        check(run(a,b,missingtool,env=env).returncode != 0 and not missingtool.exists(), 'renderer missing handled')
        check(run('--help').returncode == 0, 'help')
        check(run().returncode == 2, 'usage error')
        # A report symlink cannot overwrite another file.
        target=root/'target';target.write_text('keep')
        link=root/'linked.html';link.symlink_to(target)
        check(run(a,b,link).returncode != 0 and target.read_text() == 'keep', 'symlink output refused')
    print(f'{checks} PDF integration checks passed')


if __name__ == '__main__':
    main()
