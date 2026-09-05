// SPDX-License-Identifier: MIT
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <vector>
namespace {
std::wstring ShellKey=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons";
std::wstring BackupKey=L"SOFTWARE\\TidyDesk\\ArrowBackup";
HKEY registryRoot=HKEY_LOCAL_MACHINE;bool testing=false;
LSTATUS Write(HKEY k,const wchar_t* n,DWORD t,const void* p,DWORD bytes){return RegSetValueExW(k,n,0,t,static_cast<const BYTE*>(p),bytes);}
LSTATUS Apply(bool restore){HKEY shell=nullptr,backup=nullptr;auto code=RegCreateKeyExW(registryRoot,ShellKey.c_str(),0,nullptr,0,KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,nullptr,&shell,nullptr);if(code)return code;
 code=RegCreateKeyExW(registryRoot,BackupKey.c_str(),0,nullptr,0,KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,nullptr,&backup,nullptr);if(code){RegCloseKey(shell);return code;}
 auto work=[&]()->LSTATUS {DWORD marker=0,size=sizeof(marker);bool saved=RegQueryValueExW(backup,L"Saved",nullptr,nullptr,reinterpret_cast<BYTE*>(&marker),&size)==ERROR_SUCCESS&&marker==1;
 wchar_t file[32768]{};GetModuleFileNameW(nullptr,file,32768);auto icon=std::filesystem::path(file).parent_path().parent_path()/L"blank.ico";std::wstring owned=icon.wstring()+L",0";
 if(restore){if(!saved)return ERROR_SUCCESS;DWORD bytes=0,type=0;auto r=RegQueryValueExW(backup,L"Owned",nullptr,&type,nullptr,&bytes);if(r||type!=REG_SZ||bytes>65536)return ERROR_INVALID_DATA;std::vector<wchar_t> owner(bytes/2+1);r=RegQueryValueExW(backup,L"Owned",nullptr,nullptr,reinterpret_cast<BYTE*>(owner.data()),&bytes);if(r)return r;
 wchar_t current[32768]{};DWORD cb=sizeof(current);r=RegQueryValueExW(shell,L"29",nullptr,&type,reinterpret_cast<BYTE*>(current),&cb);if(r==ERROR_SUCCESS&&(type!=REG_SZ||_wcsicmp(current,owner.data())))return ERROR_REVISION_MISMATCH;if(r!=ERROR_SUCCESS&&r!=ERROR_FILE_NOT_FOUND)return r;
 DWORD exists=0;cb=sizeof(exists);if(RegQueryValueExW(backup,L"Exists",nullptr,nullptr,reinterpret_cast<BYTE*>(&exists),&cb))return ERROR_INVALID_DATA;
 if(exists){cb=sizeof(type);if(RegQueryValueExW(backup,L"Type",nullptr,nullptr,reinterpret_cast<BYTE*>(&type),&cb))return ERROR_INVALID_DATA;cb=0;if(RegQueryValueExW(backup,L"Data",nullptr,nullptr,nullptr,&cb)||cb>1048576)return ERROR_INVALID_DATA;std::vector<BYTE> data(cb);if(RegQueryValueExW(backup,L"Data",nullptr,nullptr,data.data(),&cb))return ERROR_INVALID_DATA;r=Write(shell,L"29",type,data.data(),cb);}else{r=RegDeleteValueW(shell,L"29");if(r==ERROR_FILE_NOT_FOUND)r=ERROR_SUCCESS;}if(r)return r;return RegDeleteValueW(backup,L"Saved");}
 if(!testing&&GetFileAttributesW(icon.c_str())==INVALID_FILE_ATTRIBUTES)return ERROR_FILE_NOT_FOUND;
 if(!saved){DWORD type=0,bytes=0;auto r=RegQueryValueExW(shell,L"29",nullptr,&type,nullptr,&bytes);if(r!=ERROR_SUCCESS&&r!=ERROR_FILE_NOT_FOUND)return r;DWORD exists=r==ERROR_SUCCESS?1U:0U;if(bytes>1048576)return ERROR_INVALID_DATA;std::vector<BYTE> data(bytes);if(exists){r=RegQueryValueExW(shell,L"29",nullptr,&type,data.data(),&bytes);if(r)return r;}if((r=Write(backup,L"Exists",REG_DWORD,&exists,sizeof(exists)))||(r=Write(backup,L"Type",REG_DWORD,&type,sizeof(type)))||(r=Write(backup,L"Data",REG_BINARY,data.data(),bytes)))return r;DWORD one=1;if((r=Write(backup,L"Owned",REG_SZ,owned.c_str(),static_cast<DWORD>((owned.size()+1)*2)))||(r=Write(backup,L"Saved",REG_DWORD,&one,sizeof(one))))return r;RegFlushKey(backup);}
 if(saved){wchar_t previous[32768]{},current[32768]{};DWORD cb=sizeof(previous),type=0;auto r=RegQueryValueExW(backup,L"Owned",nullptr,&type,reinterpret_cast<BYTE*>(previous),&cb);if(r||type!=REG_SZ)return ERROR_INVALID_DATA;cb=sizeof(current);r=RegQueryValueExW(shell,L"29",nullptr,&type,reinterpret_cast<BYTE*>(current),&cb);if(r||type!=REG_SZ||_wcsicmp(previous,current)!=0)return ERROR_REVISION_MISMATCH;auto update=Write(backup,L"Owned",REG_SZ,owned.c_str(),static_cast<DWORD>((owned.size()+1)*sizeof(wchar_t)));if(update)return update;}
 return Write(shell,L"29",REG_SZ,owned.c_str(),static_cast<DWORD>((owned.size()+1)*sizeof(wchar_t)));};
 code=work();RegCloseKey(backup);RegCloseKey(shell);return code;}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR command,int){if(wcscmp(command,L"--test-backup")==0){
 registryRoot=HKEY_CURRENT_USER;testing=true;auto root=L"Software\\TidyDeskIconTest-"+std::to_wstring(GetCurrentProcessId());ShellKey=root+L"\\Shell";BackupKey=root+L"\\Backup";
 HKEY key=nullptr;RegCreateKeyExW(registryRoot,ShellKey.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&key,nullptr);
 const wchar_t original[]=L"%SystemRoot%\\original.ico,0";Write(key,L"29",REG_EXPAND_SZ,original,sizeof(original));
 if(Apply(false)!=ERROR_SUCCESS||Apply(true)!=ERROR_SUCCESS)return 10;
 wchar_t result[256]{};DWORD type=0,cb=sizeof(result);RegQueryValueExW(key,L"29",nullptr,&type,reinterpret_cast<BYTE*>(result),&cb);if(type!=REG_EXPAND_SZ||wcscmp(result,original))return 11;
 RegDeleteValueW(key,L"29");if(Apply(false)||Apply(true))return 12;cb=sizeof(result);if(RegQueryValueExW(key,L"29",nullptr,nullptr,reinterpret_cast<BYTE*>(result),&cb)!=ERROR_FILE_NOT_FOUND)return 13;
 if(Apply(false))return 14;const wchar_t other[]=L"other.ico,0";Write(key,L"29",REG_SZ,other,sizeof(other));if(Apply(true)!=ERROR_REVISION_MISMATCH||Apply(false)!=ERROR_REVISION_MISMATCH)return 15;
 RegCloseKey(key);RegDeleteTreeW(registryRoot,root.c_str());return 0;
 }bool restore=wcscmp(command,L"--restore")==0;if(!restore&&wcscmp(command,L"--hide")!=0)return 2;auto result=Apply(restore);if(result){wchar_t text[200]{};swprintf_s(text,L"图标设置未完成（错误 %ld）。原备份已保留。",result);MessageBoxW(nullptr,text,L"TidyDesk",MB_OK|MB_ICONWARNING);}else MessageBoxW(nullptr,L"系统图标设置已更新。可在设置页刷新图标；若尚未变化，请在方便时注销并重新登录。",L"TidyDesk",MB_OK|MB_ICONINFORMATION);return static_cast<int>(result);}
