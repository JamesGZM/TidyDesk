// SPDX-License-Identifier: MIT
#include "desktop_model.h"
#include <cstdio>
int main(){int result=desk::ModelTest();std::printf("Model recovery: %d\n",result);if(result)return result;result=desk::TransferTest();std::printf("File transfer: %d\n",result);return result;}
