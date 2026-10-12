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

if __name__=='__main__': unittest.main()
