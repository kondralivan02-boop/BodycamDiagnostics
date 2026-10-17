import importlib.util
import tempfile
import unittest
from pathlib import Path

class PayloadTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('payload', Path(__file__).parents[1] / 'tools' / 'prepare_single_exe.py')
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)

    def test_verified_cache_is_reused_offline(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'cached'; p.write_bytes(b'abc')
            self.assertEqual(self.module.fetch_verified('https://invalid.invalid/', p, 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'), p)

    def test_modified_download_is_rejected_and_not_published(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'download'; source=Path(d)/'source'; source.write_bytes(b'bad')
            with self.assertRaises(ValueError):
                self.module.fetch_verified(source.as_uri(), p, '0'*64)
            self.assertFalse(p.exists())

    def test_manifest_rejects_conflicts(self):
        with self.assertRaises(ValueError):
            self.module.parse_manifest(('a'*64+'  ffmpeg.exe\n'+'b'*64+'  ffmpeg.exe\n').encode())

    def test_manifest_hashes_both_tools(self):
        m=self.module.parse_manifest(('a'*64+'  ffmpeg.exe\n'+'b'*64+'  PresentMon.exe\n').encode())
        self.assertEqual(m['presentmon.exe'], 'b'*64)
        self.assertEqual(m['ffmpeg.exe'], 'a'*64)

    def test_extracted_binary_hash_is_checked(self):
        import zipfile
        with tempfile.TemporaryDirectory() as d:
            archive=Path(d)/'archive.zip'; target=Path(d)/'ffmpeg.exe'
            with zipfile.ZipFile(archive,'w') as z: z.writestr('build/bin/ffmpeg.exe', b'bad')
            with self.assertRaises(ValueError):
                self.module.extract_verified_ffmpeg(archive, target, '0'*64)
            self.assertFalse(target.exists())

    def test_duplicate_binary_is_rejected(self):
        import zipfile
        with tempfile.TemporaryDirectory() as d:
            archive=Path(d)/'archive.zip'
            with zipfile.ZipFile(archive,'w') as z:
                z.writestr('a/bin/ffmpeg.exe', b'a'); z.writestr('b/bin/ffmpeg.exe', b'b')
            with self.assertRaises(ValueError):
                self.module.extract_verified_ffmpeg(archive, Path(d)/'ffmpeg.exe', '0'*64)

    def test_valid_binary_extraction(self):
        import zipfile
        with tempfile.TemporaryDirectory() as d:
            archive=Path(d)/'archive.zip'; target=Path(d)/'ffmpeg.exe'
            with zipfile.ZipFile(archive,'w') as z: z.writestr('build/bin/ffmpeg.exe', b'abc')
            self.module.extract_verified_ffmpeg(archive, target, 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
            self.assertEqual(target.read_bytes(), b'abc')

    def test_mingw_runtime_notice_is_embedded(self):
        with tempfile.TemporaryDirectory() as d:
            repo=Path(d); folder=repo/'licenses'; folder.mkdir()
            (folder/'NOTICE.txt').write_text('notices')
            for name in ['PresentMon-MIT.txt','FFmpeg-GPLv3.txt','FFmpeg-build-README.txt','LLVM-MinGW.txt','MinGW-w64-runtime.txt','Winpthreads.txt']:
                (folder/name).write_text('LICENSE: '+name)
            text=self.module.make_notices(repo)
            self.assertIn(b'LICENSE: MinGW-w64-runtime.txt',text)
            (folder/'MinGW-w64-runtime.txt').unlink()
            with self.assertRaises(FileNotFoundError): self.module.make_notices(repo)

    def test_verified_download_cannot_overwrite_symlinked_download_file(self):
        import hashlib
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            external = root/'do-not-touch.txt'; external.write_bytes(b'original outside content')
            target = root/'presentmon.download.exe'
            try:
                (root/'presentmon.download.exe.download').symlink_to(external)
            except (OSError, NotImplementedError) as exc:
                self.skipTest('Symlinks unavailable: ' + str(exc))
            source = root/'fixture'; source.write_bytes(b'trusted runtime')
            self.module.fetch_verified(source.as_uri(), target, hashlib.sha256(source.read_bytes()).hexdigest())
            self.assertEqual(target.read_bytes(), source.read_bytes())
            self.assertEqual(external.read_bytes(), b'original outside content')

    def test_ffmpeg_extraction_replaces_symlink_without_following_it(self):
        import hashlib, zipfile
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            external = root/'do-not-touch.txt'; external.write_bytes(b'original outside content')
            target = root/'ffmpeg.exe'
            try:
                target.symlink_to(external)
            except (OSError, NotImplementedError) as exc:
                self.skipTest('Symlinks unavailable: ' + str(exc))
            archive = root/'archive.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('build/bin/ffmpeg.exe', b'trusted runtime')
            self.module.extract_verified_ffmpeg(archive, target, hashlib.sha256(b'trusted runtime').hexdigest())
            self.assertEqual(target.read_bytes(), b'trusted runtime')
            self.assertFalse(target.is_symlink())
            self.assertEqual(external.read_bytes(), b'original outside content')

    def test_failed_extraction_preserves_previous_verified_runtime(self):
        import zipfile
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            target = root/'ffmpeg.exe'; target.write_bytes(b'previous valid data')
            archive = root/'bad.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('build/bin/ffmpeg.exe', b'bad runtime')
            with self.assertRaises(ValueError):
                self.module.extract_verified_ffmpeg(archive, target, '0'*64)
            self.assertEqual(target.read_bytes(), b'previous valid data')
            self.assertFalse(list(root.glob('ffmpeg.exe.*.tmp')))

    def test_atomic_publish_failure_keeps_old_output_and_cleans_temp(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            target = root/'help.txt'
            target.write_bytes(b'old trusted data')
            with patch.object(self.module.os, 'replace', side_effect=OSError('disk failed')):
                with self.assertRaises(OSError):
                    self.module.atomic_write_bytes(target, b'new content')
            self.assertEqual(target.read_bytes(), b'old trusted data')
            self.assertEqual(list(root.glob('help.txt.*.tmp')), [])

    def test_generated_payload_does_not_follow_output_symlinks(self):
        import hashlib, json, zipfile
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); repo = root/'repo'; out = root/'out'
            (repo/'licenses').mkdir(parents=True)
            (repo/'src').mkdir()
            (repo/'src'/'SingleExe.manifest').write_text('<assembly/>')
            (repo/'СПРАВКА.txt').write_text('help')
            for name in ('NOTICE.txt', 'PresentMon-MIT.txt', 'FFmpeg-GPLv3.txt', 'FFmpeg-build-README.txt', 'LLVM-MinGW.txt', 'MinGW-w64-runtime.txt', 'Winpthreads.txt'):
                (repo/'licenses'/name).write_text('license '+name)
            pm_data = b'presentmon'; ff_data = b'ffmpeg'
            pm = root/'download-presentmon'; pm.write_bytes(pm_data)
            ff = root/'download-ffmpeg.zip'
            with zipfile.ZipFile(ff, 'w') as z: z.writestr('build/bin/ffmpeg.exe',ff_data)
            manifest = hashlib.sha256(pm_data).hexdigest() + '  PresentMon.exe\n' + hashlib.sha256(ff_data).hexdigest() + '  ffmpeg.exe\n'
            (repo/'runtime-tools.sha256').write_text(manifest)
            (repo/'runtime-tools.sources.json').write_text(json.dumps({'schemaVersion':1,'presentMon':{'url':'https://example.test/pm','outputName':'PresentMon.exe','downloadSha256':hashlib.sha256(pm_data).hexdigest()},'ffmpeg':{'url':'https://example.test/ff','outputName':'ffmpeg.exe','downloadSha256':hashlib.sha256(ff.read_bytes()).hexdigest()}}))
            out.mkdir()
            external=root/'outside.txt'; external.write_bytes(b'do not overwrite')
            for name in ('PresentMon.exe','runtime-tools.sha256','help.txt','licenses.txt','payload.json','payload.rc','EmbeddedPayload.h'):
                try:
                    (out/name).symlink_to(external)
                except (OSError, NotImplementedError) as exc:
                    self.skipTest('Symlinks unavailable: ' + str(exc))
            def fixture_fetch(url, target, expected):
                return pm if 'pm' in url else ff
            with patch.object(self.module, 'fetch_verified', side_effect=fixture_fetch):
                self.module.prepare(repo,out)
            self.assertEqual(external.read_bytes(), b'do not overwrite')
            self.assertTrue(all(not (out/name).is_symlink() for name in ('PresentMon.exe','ffmpeg.exe','runtime-tools.sha256','help.txt','licenses.txt','payload.json','payload.rc','EmbeddedPayload.h')))
            self.assertEqual((out/'PresentMon.exe').read_bytes(),pm_data)
            self.assertEqual((out/'ffmpeg.exe').read_bytes(),ff_data)
            self.assertEqual(len(json.loads((out/'payload.json').read_text())['assets']),5)

if __name__=='__main__': unittest.main()
