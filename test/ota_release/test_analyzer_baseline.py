"""Narrow analyzer debt boundary; synthetic defects, not a live analyzer run."""
import hashlib
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'scripts'))
import ci_cppcheck_capture as capture


class BaselineContract(unittest.TestCase):
    def test_baseline_schema_rejects_malformed_or_high_severity_debt(self):
        self.assertTrue(callable(getattr(capture, 'load_baseline', None)))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'baseline.json'
            row = ['src/a.cpp', 12, 'style', 'functionStatic', 4, 'a'*64]
            for findings in ([row[:5]], [row[:-1]+['bad']], [row[:4]+[1,row[5]]], [row,row],
                             [['../escape', *row[1:]]]):
                path.write_text(json.dumps({'schema':1, 'findings':findings}))
                with self.assertRaises(ValueError):
                    capture.load_baseline(path)
            path.write_text(json.dumps({'schema':1, 'findings':[row]}))
            self.assertEqual(capture.load_baseline(path), [row])

    def test_exact_verified_finding_and_source_only(self):
        self.assertTrue(callable(getattr(capture, 'is_baselined', None)), 'reviewed baseline matcher missing')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root/'src').mkdir(); (root/'src/a.cpp').write_text('unchanged')
            row = ['src/a.cpp', 12, 'style', 'functionStatic', 4, hashlib.sha256(b'unchanged').hexdigest()]
            defect = SimpleNamespace(file='src/a.cpp', line=12, category='style', id='functionStatic', severity=4,
                                     _crosscub_raw_verified=True, _crosscub_location_verified=True)
            self.assertTrue(capture.is_baselined(defect, [row], root))
            for key, value in [('line',13), ('severity',2), ('id','newCheck'), ('file','../src/a.cpp'),
                               ('_crosscub_raw_verified',False), ('_crosscub_location_verified',False)]:
                candidate = SimpleNamespace(**{**vars(defect), key:value})
                self.assertFalse(capture.is_baselined(candidate, [row], root), key)
            (root/'src/a.cpp').write_text('changed')
            self.assertFalse(capture.is_baselined(defect, [row], root))


if __name__ == '__main__':
    unittest.main()
