"""Synthetic image / mocked GitHub transport; no live publication."""
import copy
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from test_release import synthetic_image
import prepare_ota_release as prep


class DraftIdentity(unittest.TestCase):
    def test_created_id_survives_untagged_draft_readback(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'crosscub-sticky-v1.6.5.4.bin'
            data = synthetic_image(); path.write_bytes(data)
            release = dict(id=42, tag_name='crosscub-v1.6.5.4', target_commitish='a'*40,
                           url='https://api.github.com/repos/tobiasweissert/Crosscub/releases/42',
                           draft=True, prerelease=False, assets=[dict(id=7, name=path.name, size=len(data),
                           digest='sha256:'+hashlib.sha256(data).hexdigest(),
                           url='https://api.github.com/repos/tobiasweissert/Crosscub/releases/assets/7',
                           browser_download_url='https://github.com/tobiasweissert/Crosscub/releases/download/untagged-abc/'+path.name)])
            calls = []
            def api(endpoint, **kwargs):
                calls.append((endpoint, kwargs))
                if endpoint.startswith('git/ref/'):
                    return {'object': {'type': 'commit', 'sha': 'a'*40}}
                if endpoint in ('releases', 'releases/42'):
                    return copy.deepcopy(release)
                raise AssertionError(endpoint)
            with patch.object(prep, 'check_remote'), patch.object(prep, 'github', side_effect=api), \
                 patch.object(prep, 'release_command'), patch.object(prep.subprocess, 'run', return_value=type('R', (), {'stdout':data,'returncode':0})()) as transport:
                prep.upload_release('1.6.5.4', path, 'a'*40)
            upload = transport.call_args_list[0].args[0]
            self.assertEqual(upload[upload.index('--hostname')+1], 'github.com')
            self.assertIn('https://uploads.github.com/repos/tobiasweissert/Crosscub/releases/42/assets?name='+path.name, upload)
            self.assertTrue(any(e == 'releases' and k.get('fields') for e,k in calls), 'creation must return and retain exact release ID')
            self.assertIn(('releases/42', {}), calls)


class RecoveryGuards(unittest.TestCase):
    def test_publish_only_requires_optin_and_exact_positive_id(self):
        for release_id, approved in [(42, False), (0, True), (-1, True), (True, True)]:
            with self.subTest(release_id=release_id, approved=approved), \
                 patch.object(prep, 'github') as api, self.assertRaises(ValueError):
                prep.recover_release('1.6.5.4', Path('missing.bin'), 'a'*40,
                                     release_id=release_id, publish=approved)
            api.assert_not_called()

    def test_incomplete_draft_enumeration_fails_closed(self):
        for page in (None, {}, [{}], [{'tag_name':None}], [{'tag_name':''}]):
            with self.subTest(page=page), patch.object(prep, 'github', side_effect=[None, page, None]):
                with self.assertRaises(ValueError):
                    prep.check_remote('1.6.5.4')

    def test_recovery_uses_existing_id_and_never_uploads_or_repairs_tag(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'crosscub-sticky-v1.6.5.4.bin'
            path.write_bytes(synthetic_image())
            with patch.object(prep, 'verify_release') as verify, \
                 patch.object(prep, 'publish_verified') as publish, \
                 patch.object(prep, 'github', return_value={'tag_name':'crosscub-v1.6.5.3'}) as api, \
                 patch.object(prep.subprocess, 'run') as transport, patch.object(prep, 'ensure_remote_tag') as tag:
                report = prep.recover_release('1.6.5.4', path, 'a'*40, release_id=42, publish=True)
                verify.assert_called_once_with(42, report, 'a'*40, draft=True)
                publish.assert_called_once_with(42, report, 'a'*40)
                api.assert_called_once_with('releases/latest')
                transport.assert_not_called()
                tag.assert_not_called()

    def test_tag_404_does_not_hide_existing_draft(self):
        with patch.object(prep, 'github', side_effect=[None, [{'tag_name':'crosscub-v1.6.5.4', 'draft':True}]]):
            with self.assertRaises(ValueError):
                prep.check_remote('1.6.5.4', skip_published=True)


if __name__ == '__main__':
    unittest.main()
