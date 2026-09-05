// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
namespace desk {
struct Grid {
 int columns, rows, cellWidth, cellHeight, top=48, left=18;
 static Grid Make(int width,int height,int icon) {
  int cw=std::max(92,icon+44), ch=icon+48;
  return {std::max(1,(width-36)/cw),std::max(1,(height-66)/ch),cw,ch};
 }
 int MaxScroll(int count) const {return std::max(0,(count+columns-1)/columns-rows);}
 int Hit(int x,int y,int scroll,int count) const {
  if(x<left||y<top)return -1;
  int col=(x-left)/cellWidth,row=(y-top)/cellHeight;
  int index=(row+scroll)*columns+col;
  return col<columns&&row<rows&&index<count?index:-1;
 }
};
inline int GeometryTest() {
 auto small=Grid::Make(330,220,48), large=Grid::Make(900,500,48);
 if(small.cellWidth!=large.cellWidth||small.cellHeight!=large.cellHeight)return 1;
 if(small.Hit(0,0,0,100)!=-1||small.Hit(18,48,0,100)!=0)return 2;
 if(small.Hit(18,48,1,100)!=small.columns)return 3;
 if(large.MaxScroll(100)>=small.MaxScroll(100)||small.MaxScroll(0)!=0)return 4;
 if(small.Hit(18,48,0,0)!=-1)return 5;
 return 0;
}
}
