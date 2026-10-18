import importlib.util
from pathlib import Path
import unittest

class ImportTests(unittest.TestCase):
    def setUp(self):
        spec=importlib.util.spec_from_file_location('inspection',Path(__file__).parents[1]/'tools'/'verify_single_exe.py')
        self.m=importlib.util.module_from_spec(spec); spec.loader.exec_module(self.m)
    def test_compiler_runtime_is_not_a_system_dependency(self):
        for name in ['libc++.dll','libunwind.dll','libwinpthread-1.dll','VCRUNTIME140.dll','unknown.dll']:
            with self.assertRaises(ValueError): self.m.check_imports([name])
    def test_windows_dxgi_system_dependency_is_allowed(self):
        self.m.check_imports(['dxgi.dll', 'DXGI.DLL'])
    def test_windows_system_runtime_is_allowed(self):
        self.m.check_imports(['KERNEL32.dll','bcrypt.dll','api-ms-win-crt-runtime-l1-1-0.dll'])

if __name__=='__main__': unittest.main()
