// SPDX-License-Identifier: MIT
#include "desktop_model.h"
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <fstream>
#include <cstdio>
#include <algorithm>
#include <commctrl.h>
using Microsoft::WRL::ComPtr;
namespace desk {
namespace {
std::filesystem::path testRoot;
std::filesystem::path Known(REFKNOWNFOLDERID id) { PWSTR p=nullptr; if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&p))) throw std::runtime_error("Known folder unavailable"); std::filesystem::path r(p); CoTaskMemFree(p); return r; }
std::wstring Read(const std::filesystem::path& f,const std::wstring& s,const wchar_t* k,const wchar_t* def=L"") { wchar_t b[32768]{}; GetPrivateProfileStringW(s.c_str(),k,def,b,32768,f.c_str()); return b; }
int Number(const std::filesystem::path& f,const std::wstring& s,const wchar_t* k,int d) { return static_cast<int>(GetPrivateProfileIntW(s.c_str(),k,d,f.c_str())); }
}
std::filesystem::path DataDir() { return testRoot.empty()?Known(FOLDERID_LocalAppData)/L"TidyDesk":testRoot; }
std::filesystem::path CollectionsDir() { return Known(FOLDERID_Documents)/L"TidyDesk Collections"; }
std::vector<Box> Load() {
 auto f=DataDir()/L"layout.ini";
 if(Number(f,L"Layout",L"Version",0)!=1) { f=DataDir()/L"layout.bak"; if(Number(f,L"Layout",L"Version",0)!=1)return {}; }
 std::vector<Box> boxes; int count=std::clamp(Number(f,L"Layout",L"Count",0),0,64);
 for(int i=0;i<count;++i) { std::wstring s=L"Box"+std::to_wstring(i); Box b;
 b.id=Read(f,s,L"Id");b.name=Read(f,s,L"Name");b.path=Read(f,s,L"Path"); if(b.id.empty()||b.path.empty())continue;
 b.x=Number(f,s,L"X",40); b.y=Number(f,s,L"Y",60); b.w=std::clamp(Number(f,s,L"W",360),220,2400); b.h=std::clamp(Number(f,s,L"H",300),100,1800);
 b.alpha=static_cast<unsigned>(std::clamp(Number(f,s,L"Alpha",235),80,255));b.size=static_cast<unsigned>(std::clamp(Number(f,s,L"Size",48),24,96));
 b.color=static_cast<DWORD>(Number(f,s,L"Color",RGB(30,43,51)))&0xffffff;b.collapsed=Number(f,s,L"Collapsed",0)!=0;b.locked=Number(f,s,L"Locked",0)!=0;
 for(int n=0;n<std::clamp(Number(f,s,L"OrderCount",0),0,10000);++n)b.order.push_back(Read(f,s,(L"Order"+std::to_wstring(n)).c_str()));
 boxes.push_back(std::move(b)); } return boxes;
}
bool Save(const std::vector<Box>& boxes) {
 HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\TidyDesk.Layout.Writer");if(!mutex)return false;
 const DWORD acquired=WaitForSingleObject(mutex,3000);if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED){CloseHandle(mutex);return false;}
 struct Unlock {HANDLE h;~Unlock(){ReleaseMutex(h);CloseHandle(h);}} unlock{mutex};
 try { auto dir=DataDir();std::filesystem::create_directories(dir);auto temp=dir/L"layout.tmp", dest=dir/L"layout.ini",bak=dir/L"layout.bak";
 // UTF-16 BOM ensures Windows INI APIs preserve non-ASCII paths.
 {std::ofstream f(temp,std::ios::binary|std::ios::trunc);const char bom[]={-1,-2};f.write(bom,2);if(!f)return false;}
 bool ok=true;auto put=[&](const std::wstring& s,const wchar_t* k,const std::wstring& v){ok=WritePrivateProfileStringW(s.c_str(),k,v.c_str(),temp.c_str())&&ok;};
 put(L"Layout",L"Version",L"1");put(L"Layout",L"Count",std::to_wstring(boxes.size()));
 for(size_t i=0;i<boxes.size();++i){const auto& b=boxes[i];auto s=L"Box"+std::to_wstring(i);put(s,L"Id",b.id);put(s,L"Name",b.name);put(s,L"Path",b.path);
 put(s,L"X",std::to_wstring(b.x));put(s,L"Y",std::to_wstring(b.y));put(s,L"W",std::to_wstring(b.w));put(s,L"H",std::to_wstring(b.h));put(s,L"Alpha",std::to_wstring(b.alpha));put(s,L"Size",std::to_wstring(b.size));put(s,L"Collapsed",b.collapsed?L"1":L"0");put(s,L"Locked",b.locked?L"1":L"0");
 put(s,L"Color",std::to_wstring(b.color));put(s,L"OrderCount",std::to_wstring(b.order.size()));for(size_t j=0;j<b.order.size();++j)put(s,(L"Order"+std::to_wstring(j)).c_str(),b.order[j]);}
 WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp.c_str());if(!ok)return false;
 if(std::filesystem::exists(dest))return ReplaceFileW(dest.c_str(),temp.c_str(),bak.c_str(),0,nullptr,nullptr)!=FALSE;
 return MoveFileExW(temp.c_str(),dest.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
 }catch(...){return false;}
}
void Notify(){if(auto w=FindWindowW(L"TidyDesk.Desktop.Controller",nullptr))PostMessageW(w,WM_APP+1,0,0);}
bool Dissolve(HWND owner,const std::wstring& id){auto boxes=Load();auto found=std::find_if(boxes.begin(),boxes.end(),[&](const Box& b){return b.id==id;});if(found==boxes.end())return false;
 const auto folder=found->path;TASKDIALOG_BUTTON buttons[]={{100,L"将内容移回桌面"},{101,L"保留在原文件夹"}};TASKDIALOGCONFIG config{sizeof(config)};config.hwndParent=owner;config.dwFlags=TDF_USE_COMMAND_LINKS;config.dwCommonButtons=TDCBF_CANCEL_BUTTON;config.pszWindowTitle=L"解散收纳框";config.pszMainInstruction=L"解散后如何保留文件？";config.pszContent=L"只移除收纳框，不删除文件。文件冲突由 Windows 处理。";config.cButtons=2;config.pButtons=buttons;int choice=0;if(FAILED(TaskDialogIndirect(&config,&choice,nullptr,nullptr))||(choice!=100&&choice!=101))return false;
 if(choice==100){std::error_code ec;std::vector<std::wstring> files;for(auto& entry:std::filesystem::directory_iterator(folder,ec))files.push_back(entry.path().wstring());if(ec){MessageBoxW(owner,L"无法读取收纳文件夹，未解散。",L"TidyDesk",MB_OK|MB_ICONERROR);return false;}auto hr=Transfer(owner,files,Known(FOLDERID_Desktop).wstring(),false);if(FAILED(hr)){if(hr!=HRESULT_FROM_WIN32(ERROR_CANCELLED))MessageBoxW(owner,L"部分文件未能移回桌面，收纳框已保留。",L"TidyDesk",MB_OK|MB_ICONERROR);return false;}}
 boxes=Load();boxes.erase(std::remove_if(boxes.begin(),boxes.end(),[&](const Box& b){return b.id==id;}),boxes.end());if(!Save(boxes)){MessageBoxW(owner,L"无法保存布局，未完成解散。",L"TidyDesk",MB_OK|MB_ICONERROR);return false;}Notify();return true;
}
void Start(HWND owner){if(FindWindowW(L"TidyDesk.Desktop.Controller",nullptr))return;wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto file=std::filesystem::path(exe).parent_path()/L"TidyDeskDesktop.exe";SHELLEXECUTEINFOW info{sizeof(info)};info.hwnd=owner;info.lpFile=file.c_str();info.nShow=SW_SHOWNOACTIVATE;ShellExecuteExW(&info);}
void Stop(){if(auto w=FindWindowW(L"TidyDesk.Desktop.Controller",nullptr))PostMessageW(w,WM_CLOSE,0,0);}
bool NewBox(HWND owner,bool existing){
 if(!existing){wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto file=std::filesystem::path(exe).parent_path()/L"TidyDeskDesktop.exe";return reinterpret_cast<INT_PTR>(ShellExecuteW(owner,L"open",file.c_str(),L"--new",nullptr,SW_SHOWNORMAL))>32;}

 const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);bool result=false;
 {ComPtr<IFileDialog> picker;if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker)))){
 DWORD flags=0;picker->GetOptions(&flags);picker->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);picker->SetTitle(existing?L"选择要显示的分类文件夹":L"选择或新建一个分类文件夹（右键 → 新建文件夹）");
 if(!existing){std::error_code ec;std::filesystem::create_directories(CollectionsDir(),ec);ComPtr<IShellItem> root;if(SUCCEEDED(SHCreateItemFromParsingName(CollectionsDir().c_str(),nullptr,IID_PPV_ARGS(&root))))picker->SetFolder(root.Get());}
 if(SUCCEEDED(picker->Show(owner))){ComPtr<IShellItem> item;PWSTR p=nullptr;if(SUCCEEDED(picker->GetResult(&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){auto boxes=Load();Box b;b.path=p;CoTaskMemFree(p);b.name=std::filesystem::path(b.path).filename().wstring();GUID id{};CoCreateGuid(&id);wchar_t guid[40]{};StringFromGUID2(id,guid,40);b.id=guid;b.x+=static_cast<int>(boxes.size()%6)*30;b.y+=static_cast<int>(boxes.size()%6)*30;
 bool duplicate=false;for(const auto& old:boxes)if(_wcsicmp(old.path.c_str(),b.path.c_str())==0)duplicate=true;
 if(!duplicate&&boxes.size()<64){boxes.push_back(b);result=Save(boxes);if(result)Notify();}else MessageBoxW(owner,L"这个文件夹已经有收纳框，或已达到 64 个框的上限。",L"TidyDesk",MB_OK);}}}}
 if(SUCCEEDED(com))CoUninitialize();return result;
}
HRESULT Transfer(HWND owner,const std::vector<std::wstring>& paths,const std::wstring& destination,bool executableLinks){
 ComPtr<IFileOperation> op;HRESULT hr=CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&op));if(FAILED(hr))return hr;
 op->SetOwnerWindow(owner);op->SetOperationFlags(FOF_ALLOWUNDO|FOFX_ADDUNDORECORD|FOF_NOCONFIRMMKDIR);
 ComPtr<IShellItem> dest;hr=SHCreateItemFromParsingName(destination.c_str(),nullptr,IID_PPV_ARGS(&dest));if(FAILED(hr))return hr;
 for(const auto& path:paths){std::filesystem::path src(path);if(_wcsicmp(src.parent_path().c_str(),destination.c_str())==0)continue;
 if(executableLinks&&_wcsicmp(src.extension().c_str(),L".exe")==0){ComPtr<IShellLinkW> link;hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));if(FAILED(hr))return hr;link->SetPath(path.c_str());link->SetWorkingDirectory(src.parent_path().c_str());ComPtr<IPersistFile> persist;hr=link.As(&persist);if(FAILED(hr))return hr;
 auto name=std::filesystem::path(destination)/(src.stem().wstring()+L".lnk");for(int n=2;std::filesystem::exists(name);++n)name=std::filesystem::path(destination)/(src.stem().wstring()+L" ("+std::to_wstring(n)+L").lnk");hr=persist->Save(name.c_str(),TRUE);if(FAILED(hr))return hr;
 }else{ComPtr<IShellItem> item;hr=SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item));if(FAILED(hr))return hr;hr=op->MoveItem(item.Get(),dest.Get(),nullptr,nullptr);if(FAILED(hr))return hr;}}
 hr=op->PerformOperations();BOOL aborted=FALSE;op->GetAnyOperationsAborted(&aborted);return aborted?HRESULT_FROM_WIN32(ERROR_CANCELLED):hr;
}
int ModelTest(){wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);testRoot=std::filesystem::path(temp)/(L"TidyDesk-test-"+std::to_wstring(GetCurrentProcessId()));Box b;b.id=L"test";b.name=L"中文分类";b.path=L"C:\\测试 文件";b.order={L"示例.txt"};if(!Save({b}))return 1;auto v=Load();if(v.size()!=1||v[0].name!=b.name||v[0].order!=b.order)return 2;b.name=L"Changed";if(!Save({b}))return 3;{std::ofstream f(testRoot/L"layout.ini");f<<"broken";}v=Load();if(v.size()!=1||v[0].name!=L"中文分类")return 4;std::filesystem::remove_all(testRoot);testRoot.clear();return 0;}
int TransferTest(){wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);auto root=std::filesystem::path(temp)/(L"TidyDesk-transfer-"+std::to_wstring(GetCurrentProcessId()));auto a=root/L"原始",b=root/L"分类";std::filesystem::create_directories(a);std::filesystem::create_directories(b);auto file=a/L"工作 文件.txt",exe=a/L"软件.exe";{std::ofstream f(file);f<<"test payload";}{std::ofstream f(exe);f<<"not executable; shortcut test only";}CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);auto hr=Transfer(nullptr,{file.wstring(),exe.wstring()},b.wstring());if(FAILED(hr)){std::printf("Transfer failed: 0x%08lx\n",static_cast<unsigned long>(hr));return 10;}if(std::filesystem::exists(file)||!std::filesystem::exists(b/file.filename())||!std::filesystem::exists(exe)||!std::filesystem::exists(b/L"软件.lnk")){std::printf("Move/shortcut existence invariant failed\n");return 11;}ComPtr<IShellLinkW> link;CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));ComPtr<IPersistFile> persist;link.As(&persist);persist->Load((b/L"软件.lnk").c_str(),STGM_READ);wchar_t target[32768]{};auto pathResult=link->GetPath(target,32768,nullptr,SLGP_RAWPATH);std::error_code compareError;if(FAILED(pathResult)||!std::filesystem::equivalent(exe,std::filesystem::path(target),compareError)){std::printf("Shortcut target mismatch: hr=%08lx expected=%ls actual=%ls\n",static_cast<unsigned long>(pathResult),exe.c_str(),target);return 12;}persist.Reset();link.Reset();CoUninitialize();std::filesystem::remove_all(root);return 0;}
}
