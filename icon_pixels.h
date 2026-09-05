// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <vector>
#include <algorithm>
namespace desk {
inline std::vector<BYTE> IconPixels(HICON icon,int size){
 if(!icon||size<1)return {};BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size;info.bmiHeader.biHeight=-size;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
 HDC dc=CreateCompatibleDC(nullptr);void* bits=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return {};}
 auto old=SelectObject(dc,bitmap);auto p=static_cast<DWORD*>(bits);const size_t count=static_cast<size_t>(size)*size;std::fill_n(p,count,0U);bool okay=DrawIconEx(dc,0,0,icon,size,size,0,nullptr,DI_NORMAL)!=FALSE;GdiFlush();std::vector<DWORD> black(p,p+count);std::fill_n(p,count,0x00ffffffU);okay=DrawIconEx(dc,0,0,icon,size,size,0,nullptr,DI_NORMAL)!=FALSE&&okay;GdiFlush();std::vector<BYTE> result(count*4);
 // Recover premultiplied color and coverage from two native draws. This also
 // handles legacy mask-only icons without treating opaque black as transparent.
 for(size_t i=0;i<count;++i){int difference=std::clamp(static_cast<int>(p[i]&255)-static_cast<int>(black[i]&255),0,255);result[i*4]=static_cast<BYTE>(black[i]);result[i*4+1]=static_cast<BYTE>(black[i]>>8);result[i*4+2]=static_cast<BYTE>(black[i]>>16);result[i*4+3]=static_cast<BYTE>(255-difference);}
 SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return okay?result:std::vector<BYTE>{};
}
inline int IconPixelsTest(){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=16;info.bmiHeader.biHeight=-16;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;auto color=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);BYTE mask[32]{};auto monochrome=CreateBitmap(16,16,1,1,mask);if(!color||!monochrome){if(color)DeleteObject(color);if(monochrome)DeleteObject(monochrome);return 1;}auto p=static_cast<DWORD*>(bits);std::fill_n(p,256,0U);p[0]=0xff000000;p[1]=0xffff0000;p[2]=0x80800000;ICONINFO iconInfo{TRUE,0,0,monochrome,color};auto icon=CreateIconIndirect(&iconInfo);auto pixels=IconPixels(icon,16);if(icon)DestroyIcon(icon);DeleteObject(color);DeleteObject(monochrome);if(pixels.size()!=1024)return 2;if(pixels[3]!=255||pixels[2]!=0||pixels[7]!=255||pixels[6]!=255||pixels[11]<126||pixels[11]>130||pixels[10]<126||pixels[10]>130||pixels[15]!=0)return 3;return 0;}
}
