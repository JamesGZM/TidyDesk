// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <string>
#include <vector>
namespace desk {
// Slot is a boundary in the original list, including the final trailing boundary.
inline std::vector<std::wstring> Reordered(const std::vector<std::wstring>& current,const std::vector<std::wstring>& selected,int slot){
 slot=std::clamp(slot,0,static_cast<int>(current.size()));std::vector<std::wstring> moving,remaining;int insertion=0;
 for(size_t i=0;i<current.size();++i){if(std::find(selected.begin(),selected.end(),current[i])!=selected.end())moving.push_back(current[i]);else{if(static_cast<int>(i)<slot)++insertion;remaining.push_back(current[i]);}}
 if(moving.empty()||moving.size()!=selected.size())return current;
 remaining.insert(remaining.begin()+insertion,moving.begin(),moving.end());return remaining;
}
inline int OrderTest(){using V=std::vector<std::wstring>;V original={L"甲.lnk",L"乙.lnk",L"丙.txt",L"丁.exe"};
 if(Reordered(original,{original[0]},2)!=V{original[1],original[0],original[2],original[3]})return 1;
 if(Reordered(original,{original[3]},0)!=V{original[3],original[0],original[1],original[2]})return 2;
 if(Reordered(original,{original[2],original[0]},4)!=V{original[1],original[3],original[0],original[2]})return 3;
 if(Reordered(original,{original[1]},1)!=original||Reordered(original,{original[1]},2)!=original)return 4;
 if(Reordered(original,{L"missing"},0)!=original||Reordered(original,{},0)!=original)return 5;
 return 0;
}
}
