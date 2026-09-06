// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
namespace overlay {
// A nonzero alpha channel avoids all-transparent legacy mask fallback.
// Alpha 1/255 is nearly invisible; actual Explorer cache behavior needs validation.
inline bool Write(const std::filesystem::path& path) {
    const unsigned sizes[]={16,24,32,48,64,256};
    std::vector<BYTE> data;
    auto word=[&](unsigned v){data.push_back(static_cast<BYTE>(v));data.push_back(static_cast<BYTE>(v>>8));};
    auto dword=[&](unsigned v){word(v);word(v>>16);};
    word(0);word(1);word(6);unsigned offset=6+6*16;
    for(auto size:sizes){unsigned bytes=40+size*size*4+((size+31)/32)*4*size;data.push_back(static_cast<BYTE>(size));data.push_back(static_cast<BYTE>(size));word(0);word(1);word(32);dword(bytes);dword(offset);offset+=bytes;}
    for(auto size:sizes){dword(40);dword(size);dword(size*2);word(1);word(32);dword(0);dword(size*size*4);dword(0);dword(0);dword(0);dword(0);for(unsigned pixel=0;pixel<size*size;++pixel)dword(0x01000000U);data.insert(data.end(),((size+31)/32)*4*size,0);}
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));out.close();return !out.fail();
}
inline bool Verify(const std::filesystem::path& path) {
    for(int size:{16,24,32,48,64,256}) {
        auto icon=static_cast<HICON>(LoadImageW(nullptr,path.c_str(),IMAGE_ICON,size,size,LR_LOADFROMFILE));if(!icon)return false;
        HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size;info.bmiHeader.biHeight=-size;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;
        auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);DestroyIcon(icon);return false;}auto old=SelectObject(dc,bitmap);bool okay=true;
        for(UINT flags:{static_cast<UINT>(DI_NORMAL),static_cast<UINT>(DI_NORMAL|DI_NOMIRROR)})for(DWORD color:{0U,0x00ffffffU,0x00654321U}){auto p=static_cast<DWORD*>(pixels);for(int i=0;i<size*size;++i)p[i]=color;if(!DrawIconEx(dc,0,0,icon,size,size,0,nullptr,flags))okay=false;GdiFlush();for(int i=0;i<size*size;++i)for(unsigned shift:{0U,8U,16U})if(abs(static_cast<int>((p[i]>>shift)&255)-static_cast<int>((color>>shift)&255))>1)okay=false;}
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);DestroyIcon(icon);if(!okay)return false;
    }
    return true;
}
}
