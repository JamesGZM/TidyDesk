// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
namespace desk {
struct Grid {
 int columns, rows, cellWidth, cellHeight, top=48, left=32;
 static Grid Make(int width,int height,int icon) {
  int cw=std::max(92,icon+44), ch=icon+48;
  int columns=std::max(1,(width-64)/cw);
  return {columns,std::max(1,(height-80)/ch),cw,ch,48,std::max(32,(width-columns*cw)/2)};
 }
 int ItemHit(int x,int y,int scroll,int count,int icon) const {
  int index=Hit(x,y,scroll,count);if(index<0)return -1;
  int dx=(x-left)%cellWidth,dy=(y-top)%cellHeight;
  bool onIcon=dx>=(cellWidth-icon)/2&&dx<(cellWidth+icon)/2&&dy<icon;
  bool onLabel=dx>=8&&dx<cellWidth-8&&dy>=icon+4&&dy<icon+38;
  return onIcon||onLabel?index:-1;
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
 if(small.Hit(0,0,0,100)!=-1||small.Hit(small.left,48,0,100)!=0)return 2;
 if(small.Hit(small.left,48,1,100)!=small.columns)return 3;
 if(large.MaxScroll(100)>=small.MaxScroll(100)||small.MaxScroll(0)!=0)return 4;
 if(small.Hit(small.left,48,0,0)!=-1)return 5;
 if(small.ItemHit(small.left,48,0,100,48)!=-1||small.ItemHit(small.left+small.cellWidth/2,48,0,100,48)!=0)return 6;
 if(small.left<32||large.left<32||small.top!=48)return 7;
 return 0;
}
}
