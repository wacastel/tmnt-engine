/* Standalone host services. Optional frontend patch/high-score injection is disabled. */
#include "burnint.h"
extern "C" void tmnt_native_fault(const char*,unsigned,unsigned);
RomDataInfo *pRDI=nullptr;BurnRomInfo *pDataRomDesc=nullptr;
bool bDoIpsPatch=false;UINT32 nIpsMemExpLen[SND2_ROM+1]{};INT32 nInputIntfMouseDivider=1;UINT8 *MSM6295ROM=nullptr;
void HiscoreInit(){}void HiscoreExit(){}void HiscoreReset(INT32){}void HiscoreApply(){}
void IpsApplyPatches(UINT8*,char*,UINT32,bool){tmnt_native_fault("unexpected IPS patch",0,0);}
INT32 is_netgame_or_recording(){return 0;}
