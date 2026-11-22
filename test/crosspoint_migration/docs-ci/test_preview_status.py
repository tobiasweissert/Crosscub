"""Exercise the actual aggregate shell, not an alternate policy model."""
from pathlib import Path
import subprocess, textwrap, unittest

class Status(unittest.TestCase):
    def test_result_matrix(self):
        workflow = (Path(__file__).resolve().parents[3]/'.github/workflows/ci.yml').read_text()
        gate = workflow.split('  test-status:', 1)[1]
        self.assertIn('if: always()', gate)
        self.assertIn('needs: [build, clang-format, cppcheck, required-tests]', gate)
        mapping = dict(BUILD='build', FORMAT='clang-format', ANALYZER='cppcheck', TESTS='required-tests')
        for key, job in mapping.items():
            self.assertIn(key + ': ${{ needs.' + job + '.result }}', gate)
        script = textwrap.dedent(gate.split('        run: |\n')[1])
        def run(ok, **env):
            rc = subprocess.run(['bash', '-c', script], env=env).returncode
            self.assertEqual(rc == 0, ok, env)
        for event in ('workflow_dispatch', 'pull_request', 'workflow_call', 'push'):
            good = dict(EVENT=event, REF='refs/heads/main', **dict.fromkeys(mapping,'success'))
            if event == 'push': good.update(BUILD='skipped', TESTS='skipped')
            run(True, **good)
            for key in mapping:
                for result in ('success','failure','cancelled','skipped',''):
                    if result != good[key]: run(False, **{**good,key:result})
            if event == 'push': run(False, **{**good,'REF':'refs/heads/preview'})

if __name__ == '__main__': unittest.main()
