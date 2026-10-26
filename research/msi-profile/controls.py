# SPDX-License-Identifier: GPL-2.0-only
"""Fault controls over production source through Clang virtual file overlays."""
import json

def controls(root,out,flags,msi,run):
    source=root/"internal-boot/boot/full-release.c"
    original=source.read_text()
    cases=[
      ("invent-edid","stage=GU_NO_EDID_DRIVER?4U:3U","stage=3U",
       "full synthetic topology replay succeeds without EDID stage"),
      ("accept-bad-attributes",
       "return (command&5U)==0 && !(attributes&~0x300ULL) &&\n    ((attributes&EFI_PCI_IO_ATTRIBUTE_MEMORY)!=0)==((command&2U)!=0);",
       "(void)command;(void)attributes;return TRUE;",
       "release fault fails closed"),
      ("omit-prom-recheck",
       "if(s==EFI_SUCCESS && ((cfg[1]&7U)!=2U || !Bytes(cfg,Full.Config,sizeof(cfg))))s=EFI_NOT_READY;",
       "(void)cfg;",
       "MMIO requires raw MEM1/BME0"),
      ("replace-first-error","if(s==EFI_SUCCESS)s=freed;","if(freed!=EFI_SUCCESS)s=freed;",
       "PROM read error survives a later failed free")]
    result=[]
    inputs=["internal-boot/boot/test_msi_profile.c","internal-boot/boot/console-flow.c","internal-boot/boot/full-flow.c","internal-boot/unlock/checks.c","internal-boot/unlock/sha256.c"]
    for name,needle,replacement,expected in cases:
        assert original.count(needle)==1,name
        changed=out/(name+".c")
        changed.write_text(original.replace(needle,replacement))
        overlay=out/(name+".json")
        overlay.write_text(json.dumps({"version":0,"roots":[{"type":"file","name":str(source),"external-contents":str(changed),"use-external-name":False}]}))
        binary=out/name
        run(name+"-compile",flags+msi+["-DGU_MSI_SYNTHETIC","-ivfsoverlay",str(overlay),*inputs,"-o",str(binary)])
        run(name,[str(binary)],expected=1,contains=expected)
        result.append(name)
    return result
