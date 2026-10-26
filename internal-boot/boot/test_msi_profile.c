/* GPL-2.0-only. Synthetic topology and data; real release/flow, host RAM only. */
#define GU_MSI_REPLAY_TEST
#define GU_FULL_REPLAY_TEST
#define main ConsoleSuiteMain
#include "test_console.c"
#undef main
#include "full-flow.h"

static UINT8 NvMemory[GU_NVGOP_IMAGE_SIZE],PromMemory[1048576];
static EFI_DRIVER_BINDING_PROTOCOL NvBinding;
static EFI_LOADED_IMAGE_PROTOCOL NvImage;
static UINT8 NvPath[32];
static EFI_EDID_DISCOVERED_PROTOCOL ChildEdid;
static UINT32 MockConfig[64];
static UINT64 MockAttributes;
static UINT64 CaptureAttributes=0x300,PostAttributes=0;
static UINT32 PostCommand;
static BOOLEAN AttributeIntentDrift,PreStopAttributeDrift,AllocationDrift;
static VOID MsiAllocate(UINTN n) {
  if(AllocationDrift && n==1048576){MockConfig[1]&=~2U;AllocationDrift=FALSE;}
}
static int FullScenario,EnableCalls,ClaimCalls,CloseCalls,Phase,ColdCalls;
static VOID *PromAllocation;
static const char *FirstFailedEvent;
static EFI_STATUS FirstFailedStatus;
static BOOLEAN PreStopReadArmed,CaptureValueSeen,PostValueSeen;
static UINT64 CaptureValue,PostValue;
static VOID MsiAllocated(UINTN n,VOID *p) {if(n==1048576)PromAllocation=p;}
static EFI_STATUS MsiFree(VOID *p) {
  if(p!=PromAllocation)return EFI_SUCCESS;
  PromAllocation=NULL;
  return FullScenario==43?EFI_ACCESS_DENIED:EFI_SUCCESS;
}
static BOOLEAN PhysicalTextInstalled,PhysicalGopInstalled,PhysicalPathInstalled;
static BOOLEAN RestoreArmed,RestoreInjected;
static VOID MsiRestore(VOID) {
  if(FullScenario==34 && RestoreArmed) {
    RestoreArmed=FALSE;RestoreInjected=TRUE;Keys.WaitForKey=NULL;
  }
}
static BOOLEAN MsiGopInstalled(VOID){return PhysicalGopInstalled;}
static EFI_STATUS MsiProtocol(EFI_HANDLE h,EFI_GUID *g,VOID **out) {
  if(h!=FX_Rtx)return EFI_NOT_FOUND;
  if(GuGuidEqual(g,&EdidGuid)){*out=PhysicalPathInstalled?&ChildEdid:NULL;return *out?EFI_SUCCESS:EFI_UNSUPPORTED;}
  if(GuGuidEqual(g,&TextGuid)){*out=PhysicalTextInstalled?&Text:NULL;return *out?EFI_SUCCESS:EFI_UNSUPPORTED;}
  if(GuGuidEqual(g,&GopGuid)){*out=PhysicalGopInstalled?&RtxGop:NULL;return *out?EFI_SUCCESS:EFI_UNSUPPORTED;}
  if(GuGuidEqual(g,&PathGuid)){*out=PhysicalPathInstalled?TestRtxPath:NULL;return *out?EFI_SUCCESS:EFI_UNSUPPORTED;}
  return EFI_NOT_FOUND;
}
static EFI_STATUS ExtraHandle(EFI_HANDLE h,EFI_GUID *g,VOID **out) {
  if(h==FX_NvAgent && GuGuidEqual(g,&BindingGuid)){*out=&NvBinding;return EFI_SUCCESS;}
  if(h==FX_NvImage && GuGuidEqual(g,&ImageGuid)){*out=&NvImage;return EFI_SUCCESS;}
  return EFI_NOT_FOUND;
}
static EFI_STATUS ExtraRead(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH width,UINT32 at,UINTN n,VOID *data) {
  Check(width==EfiPciIoWidthUint32 && at==0 && (n==16 || (io==&RtxPci && n==64)),"only bounded config reads");
  if(FullScenario==30)return EFI_DEVICE_ERROR;
  memcpy(data,io==&RtxPci?MockConfig:FixtureAmdConfig,n*4);
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI MemRead(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT8 bar,UINT64 offset,UINTN count,VOID *dst) {
  Check(io==&RtxPci && bar==0,"only exact target BAR0 read");
  Check((MockConfig[1]&6U)==2U,"MMIO requires raw MEM1/BME0");
  if(FullScenario==31)return EFI_DEVICE_ERROR;
  if(FullScenario==43)return EFI_CRC_ERROR;
  if(w==EfiPciIoWidthUint8 && offset>=0x300000 && offset+count<=0x400000) {
    memcpy(dst,PromMemory+(offset-0x300000),count);
    if(FullScenario==10 && Phase==4 && count)((UINT8 *)dst)[0]^=1;
    return EFI_SUCCESS;
  }
  Check(w==EfiPciIoWidthUint32 && count==1,"only pinned cold dword reads");
  for(UINTN i=0;i<9;i++)if(offset==ColdOffsets[i]) {
    if(FullScenario==32)return EFI_DEVICE_ERROR;
    *(UINT32 *)dst=FixtureCold[i];ColdCalls++;
    if(FullScenario==11 && Phase==4 && i==7)*(UINT32 *)dst^=1;
    return EFI_SUCCESS;
  }
  abort();
}
static EFI_STATUS EFIAPI Bar(EFI_PCI_IO_PROTOCOL *io,UINT8 index,UINT64 *supports,VOID **resources) {
  (void)supports;Check(io==&RtxPci && index==0,"only BAR0 descriptor");
  UINT8 *raw=calloc(1,sizeof(EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR)+2);*resources=raw;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d=(VOID *)raw;
  d->Desc=ACPI_ADDRESS_SPACE_DESCRIPTOR;d->Len=sizeof(*d)-3;d->AddrSpaceGranularity=32;
  d->AddrRangeMin=GU_BAR0_BASE;d->AddrRangeMax=0x00ffffff;d->AddrLen=0x1000000;raw[sizeof(*d)]=ACPI_END_TAG_DESCRIPTOR;
  if(FullScenario==12 && Phase==4)d->AddrRangeMin++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Attributes(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 mask,UINT64 *result) {
  Check(io==&RtxPci,"attributes only on exact target");
  if(op==EfiPciIoAttributeOperationGet) {
    if(FullScenario==41 && Phase==2)return EFI_DEVICE_ERROR;
    if((FullScenario==42 && Phase==4) || (FullScenario==44 && PreStopReadArmed))return EFI_DEVICE_ERROR;
    *result=MockAttributes;
    if(FullScenario==6 && Phase==4)*result|=EFI_PCI_IO_ATTRIBUTE_BUS_MASTER;
    return EFI_SUCCESS;
  }
  if(op==EfiPciIoAttributeOperationSupported){*result=FullScenario==7?0:0x700;return EFI_SUCCESS;}
  Check(op==EfiPciIoAttributeOperationEnable && mask && !(mask&~(EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY)),"only missing IO/MEM enable; never BME/SET/DEVICE_ENABLE");
  Check(Full.Owned,"exclusive held for PCI write");EnableCalls++;
  MockAttributes|=mask;if(mask&EFI_PCI_IO_ATTRIBUTE_IO)MockConfig[1]|=1;if(mask&EFI_PCI_IO_ATTRIBUTE_MEMORY)MockConfig[1]|=2;
  if(FullScenario==8)return EFI_DEVICE_ERROR; /* partial effect followed by error */
  if(FullScenario==9)MockAttributes|=EFI_PCI_IO_ATTRIBUTE_BUS_MASTER;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Open(EFI_HANDLE h,EFI_GUID *g,VOID **out,EFI_HANDLE agent,EFI_HANDLE controller,UINT32 attrs) {
  ClaimCalls++;
  Check(h==FX_RtxPci && GuGuidEqual(g,&PciGuid) && agent==(VOID *)9999 && !controller && attrs==EFI_OPEN_PROTOCOL_EXCLUSIVE,"claim only exact controller");
  if(FullScenario==4)return EFI_ACCESS_DENIED;
  *out=&RtxPci;Live.Edges[Live.Count++]=(EDGE){h,agent,NULL,PciGuid,EFI_OPEN_PROTOCOL_EXCLUSIVE,1};return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Close(EFI_HANDLE h,EFI_GUID *g,EFI_HANDLE agent,EFI_HANDLE controller) {
  (void)controller;CloseCalls++;
  if(FullScenario==13)return EFI_DEVICE_ERROR;
  Drop(h,agent,g);return EFI_SUCCESS;
}
/* Model UEFI child and controller Stop phases separately. Synthetic code is
 * data only and is never called. Parent failure can follow successful child
 * cleanup; a zero-child splitter Stop alone must not erase child edges. */
static int ChildStops,ParentStops;
static EFI_STATUS ExtraDisconnect(EFI_HANDLE h,EFI_HANDLE agent,EFI_HANDLE child) {
  Check(!child && h==FX_RtxPci && agent==FX_NvAgent && Calls==2,"only one NVIDIA disconnect, no EDID stop");
  Calls++;ChildStops++;
  if(FullScenario==1)return EFI_DEVICE_ERROR;
  if(FullScenario!=27)Drop(h,agent,&PathGuid);
  if(FullScenario!=28)PhysicalGopInstalled=FALSE;
  PhysicalPathInstalled=FALSE;Phase=4;
  ParentStops++;
  if(FullScenario==2)return EFI_DEVICE_ERROR;
  if(FullScenario!=3)Drop(h,agent,&PciGuid);
  MockConfig[1]=(MockConfig[1]&~7U)|PostCommand;MockAttributes=PostAttributes;
  if(FullScenario==5)MockConfig[4]^=0x1000000;
  return EFI_SUCCESS;
}
static UINT8 OutMemory[GU_CONSOLE_TEXT_SIZE],GfxMemory[GU_CONSOLE_GFX_SIZE];
static int ConsoleFault,TextChildStops,TextParentStops,GfxStops;
static EFI_STATUS SplitterStop(UINTN children,EFI_HANDLE child) {
  if(!children){TextParentStops++;return EFI_SUCCESS;}
  TextChildStops++;
  if(children!=1 || child!=FX_Console)return EFI_UNSUPPORTED;
  if(ConsoleFault==1)return EFI_DEVICE_ERROR;
  if(ConsoleFault==8) {
    for(UINTN i=0;i<Live.Count;i++)if(Live.Edges[i].Handle==FX_Rtx && Live.Edges[i].Agent==FX_OutAgent && Live.Edges[i].Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) {
      Live.Edges[i]=Live.Edges[--Live.Count];break;
    }
    return EFI_DEVICE_ERROR;
  }
  if(ConsoleFault!=5)Drop(FX_Rtx,FX_OutAgent,&TextGuid);
  if(ConsoleFault==2)return EFI_DEVICE_ERROR;
  return EFI_SUCCESS;
}
static EFI_STATUS MsiConsoleStop(EFI_HANDLE controller,EFI_HANDLE agent,EFI_HANDLE child) {
  Check(controller==FX_Rtx && !child && Calls<2,"bounded console disconnect");Calls++;
  Check(agent==(Calls==1?FX_OutAgent:FX_GfxAgent),"synthetic console binding order");
  if(Calls==1) {
    EFI_STATUS s=SplitterStop(1,FX_Console);
    return s==EFI_SUCCESS?SplitterStop(0,NULL):s;
  }
  GfxStops++;
  if(ConsoleFault==3)return EFI_DEVICE_ERROR;
  Drop(FX_Rtx,agent,&GopGuid);
  if(ConsoleFault==7)return EFI_DEVICE_ERROR;
  if(ConsoleFault!=6)PhysicalTextInstalled=FALSE;
  if(ConsoleFault==4)return EFI_DEVICE_ERROR;
  return EFI_SUCCESS;
}
static VOID SetupMsi(VOID) {
  TextChildStops=TextParentStops=GfxStops=0;
  PhysicalTextInstalled=PhysicalGopInstalled=PhysicalPathInstalled=TRUE;
  RtxMode.FrameBufferBase=FixtureMode[0];RtxMode.FrameBufferSize=FixtureMode[1];
  RtxMode.Mode=(UINT32)FixtureMode[2];RtxMode.MaxMode=(UINT32)FixtureMode[3];
  RtxInfo.HorizontalResolution=(UINT32)FixtureMode[4];RtxInfo.VerticalResolution=(UINT32)FixtureMode[5];RtxInfo.PixelsPerScanLine=(UINT32)FixtureMode[6];
  OutBinding.Stop=(EFI_DRIVER_BINDING_STOP)(OutMemory+GU_CONSOLE_TEXT_STOP);
  GfxBinding.Stop=(EFI_DRIVER_BINDING_STOP)(GfxMemory+GU_CONSOLE_GFX_STOP);
  OutImage.ImageBase=OutMemory;OutImage.ImageSize=sizeof(OutMemory);
  GfxImage.ImageBase=GfxMemory;GfxImage.ImageSize=sizeof(GfxMemory);
}
static EFI_STATUS FullLog(VOID *ctx,const char *event,EFI_STATUS status) {
  if(!strcmp(event,"msi-capture-attributes-value")){CaptureValueSeen=TRUE;CaptureValue=status;}
  if(!strcmp(event,"msi-post-stop-attributes-value")){PostValueSeen=TRUE;PostValue=status;}
  if(EFI_ERROR(status) && !FirstFailedEvent){FirstFailedEvent=event;FirstFailedStatus=status;}
  if(AttributeIntentDrift && !strcmp(event,"pci-decode-restore-intent-io-mem-only"))MockAttributes^=0x100;
  if(FullScenario==40 && !strcmp(event,"capture-bar0"))AllocationDrift=TRUE;
  (void)ctx;if(EFI_ERROR(status))fprintf(stderr,"%s status=%llx fault=%d\n",event,(unsigned long long)status,FullScenario);
  if(!strcmp(event,"detach-nvidia-gop-intent")) {
    PreStopReadArmed=TRUE;
    if(PreStopAttributeDrift)MockAttributes^=0x100;
    if(FullScenario==34)RestoreArmed=TRUE;
    if(FullScenario==21)NvMemory[0x280]^=1;
    if(FullScenario==22)TestRtxPath[17]^=1;
    if(FullScenario==23)Keys.WaitForKey=NULL;
    if(FullScenario==24)return EFI_DEVICE_ERROR;
    if(FullScenario==25)Live.Edges[Live.Count++]=(EDGE){FX_Rtx,FX_Input,FX_Rtx,EdidGuid,16,1};
  }
  if(FullScenario==26 && !strcmp(event,"detach-nvidia-return"))return EFI_DEVICE_ERROR;
  return EFI_SUCCESS;
}
static EFI_STATUS ObservedConsumers(VOID) {
  GRAPH observed;
  EFI_STATUS s=Snapshot(LastTable->BootServices,&observed);
  return s==EFI_SUCCESS?FullConsumers(&observed):s;
}
static EFI_STATUS RunMsi(int scenario) {
  FullMode=FALSE;Phase=0;FullScenario=0;RestoreArmed=RestoreInjected=FALSE;
  Check(Run(0)==EFI_SUCCESS,"synthetic MSI console preparation");
  FullScenario=scenario;EnableCalls=ClaimCalls=CloseCalls=0;ChildStops=ParentStops=0;ColdCalls=0;Phase=2;
  FirstFailedEvent=NULL;FirstFailedStatus=EFI_SUCCESS;
  PreStopReadArmed=CaptureValueSeen=PostValueSeen=FALSE;CaptureValue=PostValue=0;
  memcpy(MockConfig,FixtureFullConfig,sizeof(MockConfig));MockAttributes=CaptureAttributes;
  NvBinding=(EFI_DRIVER_BINDING_PROTOCOL){0};NvBinding.Version=GU_NVGOP_VERSION;
  NvBinding.DriverBindingHandle=FX_NvAgent;NvBinding.ImageHandle=FX_NvImage;
  NvBinding.Stop=(EFI_DRIVER_BINDING_STOP)(NvMemory+GU_NVGOP_STOP);
  NvImage=(EFI_LOADED_IMAGE_PROTOCOL){0};NvImage.ImageBase=NvMemory;NvImage.ImageSize=sizeof(NvMemory);
  NvImage.ImageCodeType=EfiBootServicesCode;NvImage.DeviceHandle=FX_RtxPci;
  memcpy(NvPath,(UINT8[]){4,4,28,0},4);memcpy(NvPath+4,L"nvgop-ga1xx",24);memcpy(NvPath+28,(UINT8[]){127,255,4,0},4);NvImage.FilePath=(VOID *)NvPath;
  RtxPci.Mem.Read=MemRead;RtxPci.GetBarAttributes=Bar;RtxPci.Attributes=Attributes;
  LastTable->BootServices->OpenProtocol=Open;LastTable->BootServices->CloseProtocol=Close;
  FullMode=TRUE;
  EFI_STATUS s=GuReleaseGop(LastTable,(VOID *)9999,FullLog,NULL);
  if(scenario==21)NvMemory[0x280]^=1;
  return s;
}
/* Connect real release/verification to the real flow controller. The payload
 * callback is only a counter, not firmware code or the unlock implementation. */
static int FlowFault,PayloadTokens,WindowsTokens,ReleaseTokens;
static EFI_STATUS FlowOk(VOID *ctx){(void)ctx;return EFI_SUCCESS;}
static EFI_STATUS FlowChooseMsi(VOID *ctx,BOOLEAN *skip){(void)ctx;*skip=FALSE;return EFI_SUCCESS;}
static EFI_STATUS FlowReleaseMsi(VOID *ctx){(void)ctx;ReleaseTokens++;return RunMsi(FlowFault);}
static EFI_STATUS FlowVerifyMsi(VOID *ctx){(void)ctx;return GuVerifyReleased(LastTable,TRUE);}
static EFI_STATUS FlowPayloadToken(VOID *ctx){(void)ctx;PayloadTokens++;return EFI_SUCCESS;}
static EFI_STATUS FlowWindowsToken(VOID *ctx){(void)ctx;WindowsTokens++;return EFI_SUCCESS;}
static EFI_STATUS FlowRecordMsi(VOID *ctx,const char *event,EFI_STATUS status){(void)ctx;(void)event;(void)status;return EFI_SUCCESS;}
static EFI_STATUS FlowMsi(int fault) {
  FlowFault=fault;PayloadTokens=WindowsTokens=ReleaseTokens=0;
  GU_FULL_FLOW_OPS ops={NULL,FlowOk,FlowChooseMsi,FlowOk,FlowReleaseMsi,FlowVerifyMsi,FlowPayloadToken,FlowWindowsToken,FlowWindowsToken,FlowRecordMsi};
  return GuFullFlow(&ops);
}
int main(void) {
  for(UINTN i=0;i<sizeof(PromMemory);i++)PromMemory[i]=(UINT8)(i*37+(i>>8)*17+53);
  for(UINTN i=0;i<GU_NVGOP_TEXT_BYTES;i++)NvMemory[0x280+i]=(UINT8)(i*37+(i>>8)*17+106);
  for(UINTN i=0;i<GU_CONSOLE_GFX_BYTES;i++)GfxMemory[0x2a0+i]=(UINT8)(i*37+(i>>8)*17+159);
  for(UINTN i=0;i<GU_CONSOLE_TEXT_BYTES;i++)OutMemory[0x2a0+i]=(UINT8)(i*37+(i>>8)*17+212);
  Check(RunMsi(0)==EFI_SUCCESS && TextChildStops==1 && TextParentStops==1 && GfxStops==1 && Calls==3 && ChildStops==1 && ParentStops==1,"full synthetic topology replay succeeds without EDID stage");
  Check(GuVerifyReleased(LastTable,TRUE)==EFI_SUCCESS,"fresh final verification succeeds");
  const int faults[]={1,2,3,4,5,6,7,8,9,10,11,12,13,21,22,23,24,25,26,27,28,30,31,32};
  for(UINTN i=0;i<sizeof(faults)/sizeof(faults[0]);i++) {
    int n=faults[i];Check(RunMsi(n)!=EFI_SUCCESS,"release fault fails closed");
    Check(Calls<=3 && EnableCalls<=1,"no retries after partial effects");
    if(n>=30 && n<=32)Check(Calls==2 && !ClaimCalls,"failed capture read prevents NVIDIA disconnect");
    if(n>=21 && n<=25)Check(Calls==2 && !ClaimCalls,"late recheck prevents NVIDIA stop and decode restoration");
  }
  /* Characterise a residual firmware callback window, not a protection claim:
   * RestoreTPL may dispatch an event after the final check and before detach.
   * Input loss is contained after release; the detach itself is not prevented. */
  Check(RunMsi(34)!=EFI_SUCCESS && RestoreInjected && Calls==3 && !EnableCalls,"RestoreTPL input drift is caught after disconnect, before decode enable");
  puts("KNOWN LIMIT: RestoreTPL callback drift can precede NVIDIA disconnect; no unlock/enable follows");
  Check(FlowMsi(0)==EFI_ABORTED && PayloadTokens==1 && WindowsTokens==1 && ReleaseTokens==1,"successful flow reaches only mocked payload and Windows counters");
  for(UINTN i=0;i<sizeof(faults)/sizeof(faults[0]);i++) {
    Check(FlowMsi(faults[i])!=EFI_SUCCESS && !PayloadTokens && !WindowsTokens && ReleaseTokens==1,"every release failure blocks payload and Windows without retry");
  }
  Check(FlowMsi(34)!=EFI_SUCCESS && !PayloadTokens && !WindowsTokens,"RestoreTPL drift also blocks flow payload and Windows");
  for(UINTN word=0;word<64;word++) {
    Check(RunMsi(0)==EFI_SUCCESS,"reset successful replay");MockConfig[word]^=1;
    Check(GuVerifyReleased(LastTable,TRUE)!=EFI_SUCCESS,"every config DWORD remains pinned");
  }
  Check(RunMsi(0)==EFI_SUCCESS,"reset input verification");Keys.WaitForKey=NULL;
  Check(GuVerifyReleased(LastTable,TRUE)!=EFI_SUCCESS,"shared keyboard preserved");
  FullMode=FALSE;Phase=0;OutMemory[0x2a0]^=1;Check(Run(0)==EFI_CRC_ERROR && !Calls,"splitter hash blocks first detach");OutMemory[0x2a0]^=1;
  GfxMemory[0x2a0]^=1;Check(Run(0)==EFI_CRC_ERROR && !Calls,"graphics hash blocks first detach");GfxMemory[0x2a0]^=1;
  for(ConsoleFault=1;ConsoleFault<=8;ConsoleFault++) {
    Check(Run(0)!=EFI_SUCCESS,"partial console Stop failure blocks release");
    Check(Calls==((ConsoleFault<=2 || ConsoleFault==5 || ConsoleFault==8)?1:2),"no retry or later detach after console failure");
  }
  ConsoleFault=0;
  Check(Run(0)==EFI_SUCCESS,"reset splitter standalone control");
  GRAPH before=Live;
  Check(SplitterStop(0,NULL)==EFI_SUCCESS && Bytes(&before,&Live,sizeof(Live)),"actual zero-child Stop model preserves graph");
  Check(SplitterStop(2,FX_Console)==EFI_UNSUPPORTED && Bytes(&before,&Live,sizeof(Live)),"unsupported child count preserves graph");
  Check(SplitterStop(1,FX_RtxPci)==EFI_UNSUPPORTED && Bytes(&before,&Live,sizeof(Live)),"only synthetic shared-console child accepted");
  Check(Run(0)==EFI_SUCCESS,"reset prepared topology");
  Full=(FULL){.Table=LastTable,.Image=(VOID *)9999};
  Check(ObservedConsumers()==EFI_SUCCESS,"observed no-EDID graph accepted after passive references are filtered");
  GRAPH good=Live;
  for(UINTN i=0;i<good.Count;i++)if(good.Edges[i].Handle==FX_RtxPci) {
    Live=good;Live.Edges[i].Agent=FX_Input;Check(ObservedConsumers()!=EFI_SUCCESS,"foreign PCI agent refused");
    Live=good;Live.Edges[i].Count++;Check(ObservedConsumers()!=EFI_SUCCESS,"duplicate ownership refused");
  }
  Live=good;Live.Edges[Live.Count++]=(EDGE){FX_Rtx,FX_Input,FX_Rtx,EdidGuid,16,1};
  Check(ObservedConsumers()!=EFI_SUCCESS,"unexpected active EDID consumer refused");
  Live=good;Live.Edges[Live.Count++]=(EDGE){(EFI_HANDLE)7,FX_NvAgent,(EFI_HANDLE)7,PciGuid,16,1};
  Check(ObservedConsumers()!=EFI_SUCCESS,"NVIDIA ownership outside the exact target is refused");
  Check(Profile(NULL,TRUE)==EFI_UNSUPPORTED,"absent EDID role cannot be profiled");
  Live=good;Live.Edges[Live.Count++]=(EDGE){FX_RtxPci,FX_NvAgent,(EFI_HANDLE)7,PathGuid,8,1};
  Check(ObservedConsumers()!=EFI_SUCCESS,"unobserved second NVIDIA display child is refused");
  /* Accepted post-Stop states are a proposed model, not MSI observations. */
  const UINT32 commands[]={0,0,2,2};
  const UINT64 attributes[]={0,0x100,0x200,0x300};
  for(UINTN i=0;i<4;i++) {
    PostCommand=commands[i];PostAttributes=attributes[i];
    Check(RunMsi(0)==EFI_SUCCESS,"all four declared post-Stop pairs restore safely");
    Check((MockConfig[1]&7U)==2U && Full.Attributes==0x300,"raw baseline restored; original attribute baseline immutable");
    Check(Full.RestoredAttributes==(attributes[i]|0x200) && MockAttributes==Full.RestoredAttributes,"restored observation is separate and exact");
    Check(EnableCalls==(commands[i]==0?1:0),"enable only missing memory, never cached IO");
    Check(GuVerifyReleased(LastTable,TRUE)==EFI_SUCCESS,"final pair remains stable");
    MockAttributes^=0x100;
    Check(GuVerifyReleased(LastTable,TRUE)!=EFI_SUCCESS,"even cached IO drift after restoration is rejected");
  }
  const UINT32 badCommands[]={0,2,2,0,1,4};
  const UINT64 badAttributes[]={0x200,0,0x600,0x800,0x100,0};
  for(UINTN i=0;i<6;i++) {
    PostCommand=badCommands[i];PostAttributes=badAttributes[i];
    Check(FlowMsi(0)!=EFI_SUCCESS && !PayloadTokens && !WindowsTokens && !EnableCalls,"bad post-Stop pairs block enable, payload and Windows");
  }
  PostCommand=0;PostAttributes=0;
  CaptureAttributes=0x200;
  Check(FlowMsi(0)!=EFI_SUCCESS && Calls==2 && !EnableCalls && !PayloadTokens && !WindowsTokens,"wrong capture attributes reject before NVIDIA Stop");
  Check(FirstFailedEvent && !strcmp(FirstFailedEvent,"msi-capture-attributes-pair"),"capture pair mismatch identifies its actual checkpoint");
  Check(CaptureValueSeen && CaptureValue==0x200 && !PostValueSeen,"capture mismatch retains its observed value without a post-Stop value");
  CaptureAttributes=0x300;
  PreStopAttributeDrift=TRUE;
  Check(FlowMsi(0)!=EFI_SUCCESS && Calls==2 && !EnableCalls && !PayloadTokens && !WindowsTokens,"cache drift before NVIDIA Stop is not accepted as a post-Stop outcome");
  Check(FirstFailedEvent && !strcmp(FirstFailedEvent,"msi-pre-stop-attributes-match"),"pre-Stop drift identifies its actual checkpoint");
  PreStopAttributeDrift=FALSE;
  Check(RunMsi(41)==EFI_DEVICE_ERROR && Calls==2 && !EnableCalls,"failed attribute read prevents NVIDIA Stop");
  Check(FirstFailedEvent && !strcmp(FirstFailedEvent,"msi-capture-attributes-read") && FirstFailedStatus==EFI_DEVICE_ERROR,"failed attribute read has distinct checkpoint and original status");
  Check(!CaptureValueSeen && !PostValueSeen,"failed capture read emits no fabricated observed value");
  Check(FlowMsi(44)!=EFI_SUCCESS && Calls==2 && !EnableCalls && !PayloadTokens && !WindowsTokens,"failed pre-Stop attribute read prevents NVIDIA Stop and payload");
  Check(FirstFailedEvent && !strcmp(FirstFailedEvent,"msi-pre-stop-attributes-read") && FirstFailedStatus==EFI_DEVICE_ERROR,"failed pre-Stop read identifies its actual checkpoint");
  Check(FlowMsi(42)!=EFI_SUCCESS && Calls==3 && !EnableCalls && !PayloadTokens && !WindowsTokens,"failed post-Stop attribute read prevents enable and payload");
  Check(FirstFailedEvent && !strcmp(FirstFailedEvent,"msi-post-stop-attributes-read") && FirstFailedStatus==EFI_DEVICE_ERROR,"failed post-Stop read identifies its actual checkpoint");
  Check(CaptureValueSeen && CaptureValue==0x300 && !PostValueSeen,"failed post-Stop read retains the capture value without inventing a new value");
  PostAttributes=0x200;
  Check(FlowMsi(0)!=EFI_SUCCESS && Calls==3 && !EnableCalls && !PayloadTokens && !WindowsTokens,"rejected post-Stop pair prevents enable and payload");
  Check(FirstFailedEvent && !strcmp(FirstFailedEvent,"msi-post-stop-attributes-pair") && FirstFailedStatus==EFI_NOT_READY,"rejected post-Stop pair has a distinct checkpoint and status");
  Check(CaptureValueSeen && CaptureValue==0x300 && PostValueSeen && PostValue==0x200,"rejected pair preserves both distinct attribute observations");
  PostAttributes=0;
  Check(RunMsi(43)==EFI_CRC_ERROR && Calls==2 && !EnableCalls,"PROM read error survives a later failed free");
  Check(FirstFailedEvent && !strcmp(FirstFailedEvent,"capture-prom") && FirstFailedStatus==EFI_CRC_ERROR,"PROM retains the first failing operation");
  AttributeIntentDrift=TRUE;
  Check(FlowMsi(0)!=EFI_SUCCESS && !EnableCalls && !PayloadTokens && !WindowsTokens,"attribute change during intent cannot be repinned");
  AttributeIntentDrift=FALSE;
  Check(FlowMsi(40)!=EFI_SUCCESS && Calls==2 && !EnableCalls && !PayloadTokens && !WindowsTokens,"PROM allocation drift blocks dependent MMIO and effects");
  Check(RunMsi(0)==EFI_SUCCESS,"reset after attribute controls");
  GU_CAPS caps;Check(GuCheckConfig(FixtureFullConfig,0,GU_TARGET_BUS,0,0,&caps),"real synthetic PCI capabilities pass");
  Check(!GuCheckConfig(FixtureFullConfig,0,1,0,0,&caps),"original-profile bus rejected");
  printf("PASS MSI offline replay checks: %d; host RAM only, synthetic code never executed\n",Checks);
  return 0;
}
