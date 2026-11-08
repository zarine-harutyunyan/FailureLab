"""CLI contracts and reports, using only the Python standard library."""
import csv
import json
import os
import random
import re
from pathlib import Path
import subprocess
import sys
import tempfile

if not __debug__:
    raise SystemExit('Integration tests require Python assertions (omit -O/PYTHONOPTIMIZE).')

exe, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()

def cli(*args, code=0, error_contains=None):
    proc = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True)
    assert proc.returncode == code, (args, proc.returncode, proc.stdout, proc.stderr)
    if code:
        assert proc.stderr and not proc.stdout, proc
    else:
        assert not proc.stderr, proc.stderr
    if error_contains is not None:
        assert error_contains in proc.stderr, proc.stderr
    return proc.stdout

assert 'Usage:' in cli('--help')
version = cli('--version').strip()
assert re.fullmatch(r'FailureLab \d+\.\d+\.\d+', version), version
assert version in cli('--help')
for command in ('run', 'compare', 'validate'):
    assert 'Usage:' in cli(command, '--help')
cli(code=2)
cli('unknown', code=2)
cli('run', '--config', code=2)
cli('compare', '--config', 'x', '--output', 'unused', '--seed', '-1', code=2)
with tempfile.TemporaryDirectory(prefix='failurelab-integration-') as folder:
    tmp = Path(folder)
    for config in sorted((root/'scenarios').glob('*.json')):
        cli('validate', '--config', config)
        dest = tmp / config.stem
        cli('compare', '--config', config, '--output', dest, '--trace-limit', '10000')
        report = json.loads((dest/'failurelab-report.json').read_text())
        rows = list(csv.DictReader((dest/'results.csv').open()))
        assert [r['strategy'] for r in report['results']] == ['baseline', 'fixed', 'exponential', 'breaker']
        assert len(rows) == 4
        for r, row in zip(report['results'], rows):
            offered = r['logical_requests_offered']
            assert r['successful_requests'] + r['failed_requests'] == offered
            assert sum(r['final_failures'].values()) == r['failed_requests']
            assert int(row['downstream_attempts']) == r['downstream_attempts']
            assert int(row['success_p95_ms']) == r['successful_request_latency']['p95_ms'] if row['success_p95_ms'] else r['successful_request_latency']['p95_ms'] is None
            assert r['downstream_attempts'] <= offered * report['configuration']['retry']['max_attempts']
        second = tmp / (config.stem+'-repeat')
        cli('compare', '--config', config, '--output', second, '--trace-limit', '10000')
        for p in dest.iterdir():
            assert p.read_bytes() == (second/p.name).read_bytes(), p.name
        cli('compare', '--config', config, '--output', dest, code=4)
        cli('compare', '--config', config, '--output', dest, '--overwrite')
        note = dest/'personal-notes.txt'
        note.write_text('preserve unrelated work')
        before = (dest/'failurelab-report.json').read_bytes()
        cli('compare', '--config', config, '--output', dest, '--overwrite', code=4,
            error_contains='additional or non-regular')
        assert note.read_text() == 'preserve unrelated work'
        assert (dest/'failurelab-report.json').read_bytes() == before
        note.unlink()
        if config.stem == 'recovery':
            r = report['results'][3]
            assert r['successful_requests'] > 0 and r['failed_requests'] > 0
            assert any(t['to'] == 'OPEN' for t in r['breaker_transitions'])
            assert any(t['to'] == 'CLOSED' for t in r['breaker_transitions'])
    config = root/'scenarios/recovery.json'
    truncated = tmp/'truncated'
    cli('compare', '--config', config, '--output', truncated, '--trace-limit', '1')
    traced = json.loads((truncated/'failurelab-report.json').read_text())
    assert all(r['trace_truncated'] and len(r['trace']) == 1 for r in traced['results'])
    for r in traced['results']:
        assert 'all p95' in (truncated/'summary.txt').read_text()
        assert 'success p95' in (truncated/'summary.txt').read_text()
    cli('run', '--config', config, '--strategy', 'baseline', '--output', tmp/'run', '--seed', '18446744073709551615')
    assert json.loads((tmp/'run/failurelab-report.json').read_text())['configuration']['seed'] == 18446744073709551615
    cli('run', '--config', config, '--strategy', 'nonsense', '--output', tmp/'bad-strategy', code=3)
    cli('compare', '--config', config, '--output', tmp/'bad', '--trace-limit', '100001', code=2)
    cli('compare', '--config', config, '--output', tmp/'bad', '--seed', '-1', code=2)
    cli('compare', '--config', config, '--output', tmp/'bad', '--seed', '18446744073709551616', code=2)
    cli('validate', '--config', config, '--overwrite', code=2)
    cli('validate', '--config', config, '--config', config, code=2)
    cli('validate', '--config', config, '--nonsense', code=2)
    bad = tmp/'bad.json'
    for content in ['{oops', '{"schema_version":1,"schema_version":1}', '[]']:
        bad.write_text(content)
        cli('validate', '--config', bad, code=3)
    data = json.loads(config.read_text())
    for key, val in [('request_count', 0), ('arrival_interval_ms', -1), ('deadline_ms', 1e99), ('unknown', 1)]:
        changed = dict(data, **{key: val})
        bad.write_text(json.dumps(changed))
        cli('validate', '--config', bad, code=3)
    bad.write_text('[' * 100 + '0' + ']' * 100)
    cli('validate', '--config', bad, code=3, error_contains='nesting exceeds')
    bad.write_text(json.dumps(data) + ' ' * (1024 * 1024 - len(json.dumps(data))))
    cli('validate', '--config', bad)
    bad.write_text('{"name":"x","name":"y"}')
    cli('validate', '--config', bad, code=3, error_contains='duplicate JSON key')
    bad.write_bytes(b' ' * (1024 * 1024 + 1))
    cli('validate', '--config', bad, code=3)
    cli('validate', '--config', tmp/'missing', code=3)
    blocked = tmp/'file'
    blocked.write_text('preserve')
    cli('compare', '--config', config, '--output', blocked/'child', code=4)
    cli('compare', '--config', config, '--output', blocked, '--overwrite', code=4)
    assert blocked.read_text() == 'preserve'
    arbitrary = tmp/'unrelated'; arbitrary.mkdir(); (arbitrary/'keep').write_text('keep')
    cli('compare', '--config', config, '--output', arbitrary, '--overwrite', code=4)
    assert (arbitrary/'keep').read_text() == 'keep'
    symlink = tmp/'link'; symlink.symlink_to(arbitrary, target_is_directory=True)
    cli('compare', '--config', config, '--output', symlink, '--overwrite', code=4)
    lock = tmp/'locked.failurelab-lock'; lock.mkdir()
    cli('compare', '--config', config, '--output', tmp/'locked', code=4)
    readonly = tmp/'readonly'; readonly.mkdir(); readonly.chmod(0o500)
    try:
        if os.geteuid() != 0:
            cli('compare', '--config', config, '--output', readonly/'report', code=4)
    finally:
        readonly.chmod(0o700)
    assert not list(tmp.glob('*.failurelab-lock/pending'))
    rng = random.Random(381)
    for case in range(40):
        generated = json.loads(config.read_text())
        generated.update(name='Generated, "case" '+str(case), seed=rng.randrange(2**64),
                         request_count=rng.randint(1, 30), arrival_interval_ms=rng.randint(0, 5),
                         deadline_ms=rng.randint(1, 80), attempt_timeout_ms=rng.randint(1, 20))
        generated['service'] = {'latency': {'type': 'uniform', 'min_ms': 0, 'max_ms': rng.randint(0, 50)},
                                'failure_probability': rng.randint(0, 10)/10}
        generated['windows'] = [{'start_ms': 10, 'end_ms': 25, 'service': {
            'latency': {'type': 'fixed', 'value_ms': 30}, 'failure_probability': 1}}]
        generated['retry'] = {'max_attempts': rng.randint(1, 6), 'base_delay_ms': rng.randint(0, 10),
                              'max_backoff_ms': 30, 'full_jitter': bool(rng.getrandbits(1))}
        generated['circuit_breaker'] = {'failure_threshold': rng.randint(1, 5), 'open_duration_ms': rng.randint(1, 20)}
        bad.write_text(json.dumps(generated))
        dest = tmp/('generated-'+str(case))
        cli('compare', '--config', bad, '--output', dest, '--trace-limit', '10000')
        generated_report = json.loads((dest/'failurelab-report.json').read_text())
        generated_rows = list(csv.DictReader((dest/'results.csv').open()))
        assert generated_rows[0]['scenario'] == generated['name']
        for r in generated_report['results']:
            n = generated['request_count']
            assert r['successful_requests'] + sum(r['final_failures'].values()) == n
            assert r['failed_requests'] == sum(r['final_failures'].values())
            maximum = 1 if r['strategy'] == 'baseline' else generated['retry']['max_attempts']
            assert r['downstream_attempts'] <= n * maximum
            assert r['successful_requests'] + r['attempt_service_errors'] + r['attempt_timeouts'] == r['downstream_attempts']
            assert r['circuit_rejections'] == r['final_failures']['circuit_open']
            assert r['late_completions'] == r['attempt_timeouts']
            assert r['processed_events'] <= n * (2 + 3 * maximum)
            assert not r['trace_truncated']
            finals, starts = [0]*n, [0]*n
            previous = 0
            for event in r['trace']:
                assert event['at_ms'] >= previous
                previous = event['at_ms']
                request = event['request']
                if event['event'] == 'attempt_start':
                    starts[request] += 1
                    assert event['at_ms'] < request * generated['arrival_interval_ms'] + generated['deadline_ms']
                if event['event'].startswith('final_'):
                    finals[request] += 1
            assert finals == [1]*n
            assert sum(starts) == r['downstream_attempts']
            assert all(count <= maximum for count in starts)
            if r['strategy'] == 'breaker':
                assert sum(r['breaker_state_duration_ms'].values()) == r['completion_duration_ms']
        repeat = tmp/('generated-repeat-'+str(case))
        cli('compare', '--config', bad, '--output', repeat, '--trace-limit', '10000')
        assert (dest/'failurelab-report.json').read_bytes() == (repeat/'failurelab-report.json').read_bytes()
print('Integration checks passed: five scenarios, four strategies, reports, repeatability, errors, filesystem safety and 160 generated strategy runs.')
