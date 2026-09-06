// SPDX-License-Identifier: MIT
#pragma once
#include "icon_pixels.h"
#include <shlobj.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
namespace desk {
inline HICON ResourceIcon(const std::wstring& path,int index){
 if(path.empty())return nullptr;wchar_t expanded[32768]{};
 if(!ExpandEnvironmentStringsW(path.c_str(),expanded,32768))return nullptr;
 HICON icon=nullptr;SHDefExtractIconW(expanded,index,0,&icon,nullptr,MAKELONG(64,0));return icon;
}
inline HICON PlainShellIcon(const std::wstring& path,bool generic=false){SHFILEINFOW info{};SHGetFileInfoW(path.c_str(),FILE_ATTRIBUTE_NORMAL,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON|(generic?SHGFI_USEFILEATTRIBUTES:0));return info.hIcon;}
// Resolve the source resource, never the cached .lnk overlay image. No Resolve()
// call: loading an icon must not prompt, launch the target or rewrite the link.
inline HICON CollectionIcon(const std::wstring& path){
 auto extension=std::filesystem::path(path).extension().wstring();
 if(_wcsicmp(extension.c_str(),L".lnk")==0){
  Microsoft::WRL::ComPtr<IShellLinkW> link;Microsoft::WRL::ComPtr<IPersistFile> file;
  if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))&&SUCCEEDED(link.As(&file))&&SUCCEEDED(file->Load(path.c_str(),STGM_READ))){
   wchar_t resource[32768]{},target[32768]{};int index=0;
   if(SUCCEEDED(link->GetIconLocation(resource,32768,&index))){if(auto icon=ResourceIcon(resource,index))return icon;}
   if(SUCCEEDED(link->GetPath(target,32768,nullptr,SLGP_RAWPATH))&&*target){if(auto icon=ResourceIcon(target,0))return icon;if(auto icon=PlainShellIcon(target))return icon;}
  }
  return PlainShellIcon(L"application.exe",true);
 }
 if(_wcsicmp(extension.c_str(),L".url")==0){wchar_t resource[32768]{};GetPrivateProfileStringW(L"InternetShortcut",L"IconFile",L"",resource,32768,path.c_str());int index=static_cast<int>(GetPrivateProfileIntW(L"InternetShortcut",L"IconIndex",0,path.c_str()));if(auto icon=ResourceIcon(resource,index))return icon;return PlainShellIcon(L"page.html",true);}
 return PlainShellIcon(path);
}
inline int ShortcutIconTest(){
 if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;int result=0;
 {wchar_t executable[32768]{},temp[32768]{};GetModuleFileNameW(nullptr,executable,32768);GetTempPathW(32768,temp);auto path=std::filesystem::path(temp)/(L"TidyDesk-icon-"+std::to_wstring(GetCurrentProcessId())+L".lnk");Microsoft::WRL::ComPtr<IShellLinkW> link;Microsoft::WRL::ComPtr<IPersistFile> file;
 if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))||FAILED(link.As(&file)))result=2;
 else{link->SetPath(executable);link->SetIconLocation(executable,0);if(FAILED(file->Save(path.c_str(),TRUE)))result=3;else{auto expected=ResourceIcon(executable,0),actual=CollectionIcon(path.wstring());auto a=IconPixels(expected,64),b=IconPixels(actual,64);if(!expected||!actual||a.empty()||a!=b)result=4;if(expected)DestroyIcon(expected);if(actual)DestroyIcon(actual);std::error_code ec;std::filesystem::remove(path,ec);}}
 }CoUninitialize();return result;
}
inline int ExportCollectionIcon(const wchar_t* source,const wchar_t* output){
 if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;
 auto icon=CollectionIcon(source);auto pixels=IconPixels(icon,64);if(icon)DestroyIcon(icon);CoUninitialize();if(pixels.empty())return 2;
 // Flatten over white for a portable diagnostic BMP; no windows or user settings.
 for(size_t i=0;i<pixels.size();i+=4){unsigned inverse=255-pixels[i+3];for(size_t channel=0;channel<3;++channel)pixels[i+channel]=static_cast<BYTE>((std::min)(255U,static_cast<unsigned>(pixels[i+channel])+inverse));pixels[i+3]=255;}
 BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=64;info.biHeight=-64;info.biPlanes=1;info.biBitCount=32;
 std::ofstream stream(std::filesystem::path(output),std::ios::binary);stream.write(reinterpret_cast<const char*>(&file),sizeof(file));stream.write(reinterpret_cast<const char*>(&info),sizeof(info));stream.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));return stream?0:3;
}
}
