// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
namespace desk {
struct Box { std::wstring id, name, path; int x=40,y=60,w=360,h=300; unsigned alpha=235, size=48; DWORD color=RGB(30,43,51); bool collapsed=false, locked=false; std::vector<std::wstring> order; };
std::filesystem::path DataDir();
std::filesystem::path CollectionsDir();
std::vector<Box> Load();
bool Save(const std::vector<Box>& boxes);
bool NewBox(HWND owner, bool existing);
void Start(HWND owner);
void Stop();
void Notify();
HRESULT Transfer(HWND owner, const std::vector<std::wstring>& paths, const std::wstring& destination, bool executableLinks=true);
bool Dissolve(HWND owner, const std::wstring& id);
HRESULT MoveOutAndRemove(HWND owner,const std::wstring& folder,const std::wstring& destination);
int ModelTest();
int TransferTest();
}
