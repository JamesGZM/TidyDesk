"""Fail the build if the withdrawn elevation/bootstrap path is reintroduced."""
from pathlib import Path
import re

root = Path(__file__).parent
for path in root.iterdir():
    if path.suffix not in {'.cpp', '.h', '.iss'}:
        continue
    text = path.read_text(encoding='utf-8-sig')
    patterns = [r'\bCreateServiceW\s*\(', r'\bStartServiceCtrlDispatcherW\s*\(',
                r'"runas"', r"'runas'", r'--install-service']
    for pattern in patterns:
        if path.name == "arrow_control.h" and pattern == r'"runas"':
            continue
        if re.search(pattern, text):
            raise SystemExit(f'Withdrawn elevation path in {path.name}: {pattern}')
assert 'TidyDeskIcons.exe' not in (root/'installer.iss').read_text(encoding='utf-8-sig')
assert 'permission_service.h' not in (root/'arrows.cpp').read_text(encoding='utf-8-sig')
control=(root/'arrow_control.h').read_text(encoding='utf-8-sig')
assert control.count('L"runas"') == 1
assert 'GetSystemDirectoryW(directory,MAX_PATH)' in control
assert 'std::filesystem::path(directory)/L"reg.exe"' in control
assert 'execute.lpDirectory=directory' in control
assert 'execute.lpFile=executable.c_str()' in control
assert 'execute.lpParameters=args.c_str()' in control
assert 'inline DWORD WriteSystem(HWND owner,const Value& value)' in control
print('No withdrawn helper/service bootstrap; only fixed System32 reg.exe setting operation.')
