#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host-only MSI release/flow tests with synthetic data and real hashes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode=True
from controls import controls
ROOT=Path(__file__).resolve().parents[2]
PROFILES=["B650M_3090FE","B660M_3070TI","X870_3090TI","B660PLUS_3060","H610M_3060"]
HEADLESS=["-DGU_HEADLESS_RESEARCH","-DGU_SINGLE_RTX_CONSOLE","-DGU_FULL_UNLOCK","-DGU_AUTOMATIC_BOOT"]

def generate(out):
    pins=[]
    for macro,size,seed in [("PROM",1048576,1),("NVGOP_TEXT",0x24426,2),("CONSOLE_GFX",0x1c70,3),("CONSOLE_TEXT",0x4e9a,4)]:
        data=bytes((i*37+(i>>8)*17+seed*53)&255 for i in range(size))
        pins+=["#undef GU_"+macro+"_SHA256","#define GU_"+macro+'_SHA256 "'+hashlib.sha256(data).hexdigest()+'"']
    (out/"msi-synthetic-profile.h").write_text("\n".join(pins)+"\n")
    cfg=[0]*64
    for at,val in {0:0x220810de,1:0x00100002,2:0x030000a1,4:0xfa000000,9:0xf001,11:0x88031043,13:0x40,16:0x00014801,18:0x00020010,19:1<<28}.items():
        cfg[at]=val
    roles={"RtxPci":1,"Rtx":2,"NvAgent":3,"NvImage":3,"OutAgent":4,"OutImage":10,"GfxAgent":5,"GfxImage":11,"Console":6,"Input":6,"Error":6,"Amd":0,"AmdPci":0}
    rows=[
      "{FX_RtxPci,FX_NvAgent,FX_RtxPci,EFI_PCI_IO_PROTOCOL_GUID,16,1}",
      "{FX_RtxPci,FX_NvAgent,FX_Rtx,EFI_DEVICE_PATH_PROTOCOL_GUID,8,1}",
      "{FX_Rtx,FX_GfxAgent,FX_Rtx,EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,16,1}",
      "{FX_Rtx,FX_OutAgent,FX_Rtx,EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID,16,1}",
      "{FX_Rtx,FX_OutAgent,FX_Console,EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID,8,1}",
      "{FX_Rtx,FX_NvAgent,FX_Rtx,EFI_EDID_DISCOVERED_PROTOCOL_GUID,2,1}",
      "{(EFI_HANDLE)7,(EFI_HANDLE)8,(EFI_HANDLE)9,{0x12345678,0,0,{0}},16,1}"]
    lines=["/* Synthetic ownership, not a capture. */","#define FX_HANDLES 11"]
    lines += [f"#define FX_{k} ((EFI_HANDLE){v})" for k,v in roles.items()]
    lines += ["static const EDGE FixtureEdges[]={"+",".join(rows)+"};",
      "static const UINT32 FixtureFullConfig[64]={"+",".join(hex(x) for x in cfg)+"};",
      "static const UINT32 FixtureRtxConfig[16]={"+",".join(hex(x) for x in cfg[:16])+"};",
      "static const UINT32 FixtureAmdConfig[16]={0};",
      "static const UINT32 FixtureCold[9]={0xb72000a1,0x8b8f,0x3ff,0x10,0,0xffffff8f,0x11112222,7,0x3000};",
      "static const UINT64 FixtureMode[7]={0xfcc0000000,8294400,0,1,1920,1080,1920};"]
    (out/"msi-replay-fixture.h").write_text("\n".join(lines)+"\n")
    (out/"platform-config.h").write_text("#define GU_INTERNAL_PARTITION_INITIALIZER {0}\n#define GU_WINDOWS_PARTITION_INITIALIZER {0}\n")

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--output",type=Path,required=True)
    ap.add_argument("--baseline",type=Path)
    ap.add_argument("--msi-only",action="store_true",help="Run only changed MSI paths and compile guards")
    ap.add_argument("--fault-controls",action="store_true",help="Prove four regressions fail under virtual source faults")
    args=ap.parse_args()
    if sys.platform!="linux":
        ap.error("Run on Linux/WSL; only simulated host executables are supported")
    out=args.output.resolve()
    if out.exists():
        ap.error("output must be a new directory")
    out.mkdir(parents=True)
    generate(out)
    def run(name,cmd,expected=0,contains=None):
        p=subprocess.run(cmd,cwd=ROOT,text=True,capture_output=True)
        (out/(name+".log")).write_text(p.stdout+p.stderr)
        assert p.returncode==expected and (contains is None or contains in p.stdout+p.stderr),(name,p.returncode,(p.stdout+p.stderr)[-3000:])
        return p.stdout.strip()
    inc=["-I",str(out),"-I","deps/edk2/MdePkg/Include","-I","deps/edk2/MdePkg/Include/X64"]
    flags=["clang","-std=c11","-O1","-Wall","-Wextra","-Werror","-fshort-wchar","-ffunction-sections","-fdata-sections","-Wl,--gc-sections","-fsanitize=address,undefined","-fno-sanitize-recover=all",*inc]
    msi=[*HEADLESS,"-DGU_PROFILE_MSI_X570_3080TI","-DGU_HOST_REPLAY"]
    binary=out/"msi-profile"
    run("msi-compile",flags+msi+["-DGU_MSI_SYNTHETIC","internal-boot/boot/test_msi_profile.c","internal-boot/boot/console-flow.c","internal-boot/boot/full-flow.c","internal-boot/unlock/checks.c","internal-boot/unlock/sha256.c","-o",str(binary)])
    summary={"msi":run("msi",[str(binary)])}
    symbols=run("msi-symbols",["nm",str(binary)])
    names={line.split()[-1] for line in symbols.splitlines() if line.split()}
    assert {"GuReleaseGop","GuFullFlow"}<=names
    assert "GuUnlock" not in names
    binary=out/"cold-msi"
    run("cold-msi-compile",flags+msi+["internal-boot/boot/test_cold_precheck.c","internal-boot/boot/console-flow.c","internal-boot/unlock/checks.c","internal-boot/unlock/sha256.c","-o",str(binary)])
    summary["msi-cold"]=run("cold-msi",[str(binary)])
    if args.fault_controls:
        summary["fault_controls"]=controls(ROOT,out,flags,msi,run)
    guard=out/"guard.c"
    guard.write_text('#include "internal-boot/unlock/machine-profile.h"\n')
    base=["clang","-fsyntax-only","-I",str(ROOT),*inc]
    run("guard-positive",base+msi+[str(guard)])
    rejected=[]
    for name,defs,marker in [
      ("missing-replay",HEADLESS+["-DGU_PROFILE_MSI_X570_3080TI"],"offline replay only"),
      ("freestanding",msi+["-ffreestanding"],"offline replay only"),
      ("wrong-topology",["-DGU_PROFILE_MSI_X570_3080TI","-DGU_HOST_REPLAY"],"single physical GOP")]:
        run("guard-"+name,base+defs+[str(guard)],1,marker);rejected.append(name)
    for profile in PROFILES+["LOCAL","PROBE_H610M_3060"]:
        run("guard-conflict-"+profile,base+msi+["-DGU_PROFILE_"+profile,str(guard)],1,"Select exactly one")
        rejected.append(profile)
    object_matches=[]
    for profile in ([] if args.msi_only else PROFILES):
        binary=out/("cold-"+profile)
        run("cold-compile-"+profile,flags+HEADLESS+["-DGU_PROFILE_"+profile,"internal-boot/boot/test_cold_precheck.c","internal-boot/boot/console-flow.c","internal-boot/unlock/checks.c","internal-boot/unlock/sha256.c","-o",str(binary)])
        summary[profile]=run("cold-"+profile,[str(binary)])
        if args.baseline:
            for source in ["internal-boot/boot/full-release.c","internal-boot/unlock/checks.c"]:
                name=profile+"-"+Path(source).stem+".obj";target=out/name
                run("object-"+name,["clang","-target","x86_64-pc-windows-msvc","-std=c11","-ffreestanding","-fno-builtin","-fno-stack-protector","-mno-red-zone","-fshort-wchar","-Os","-Wall","-Wextra","-Werror",*HEADLESS,"-DGU_PROFILE_"+profile,*inc,"-c",source,"-o",str(target)])
                current=target.read_bytes();original=(args.baseline/name).read_bytes()
                assert current[:2]==original[:2]==b"\x64\x86"
                # COFF's creation timestamp is the only excluded field.
                assert current[:4]+current[8:]==original[:4]+original[8:],name
                object_matches.append(name)
    if not args.msi_only:
        binary=out/"flow"
        run("flow-compile",flags+["internal-boot/boot/test_full_flow.c","internal-boot/boot/full-flow.c","-o",str(binary)])
        summary["flow"]=run("flow",[str(binary)])
    summary.update({"build_rejections":rejected,"production_objects_equal_except_coff_timestamp":object_matches,"real_unlock_not_linked":True,"hardware_tested":False,"private_captures_required":False})
    (out/"results.json").write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps(summary,indent=2))

if __name__=="__main__":
    main()
