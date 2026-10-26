# MSI X570 / ASUS RTX 3080 Ti release model

This is a focused proposal for the existing automatic Windows release flow,
stacked on PR #49 at 15e71a6cc6e7a014c5a975d79a16df37ad158a37 (and its
PR #48 dependency), based on upstream main 5c98f11. The bus parameter,
optional-iGPU handling and absent-EDID stage come from #49. This follow-up
adds the fixed 3080 Ti profile, proposed attribute policy and synthetic tests.
It adds no staged boot or C/K procedure.

**The MSI profile is host-only and cannot compile as firmware.** It needs
GU_HOST_REPLAY, a hosted compiler and the headless single-GOP configuration.
Existing builders expose no MSI choice. No unlock payload or Windows image is
executed by these tests. A working model does not establish Windows support.

## Ownership and identity

The immutable profile describes MSI MPG X570 GAMING PLUS BIOS A.N0, ASUS
RTX 3080 Ti 12 GiB, subsystem 1043:8803, at 0000:2d:00.0. It retains the
recorded PROM, NVIDIA GOP and AMI console fingerprints. Existing profile
and generated-local policies retain PR #49's behaviour.

The physical GOP has GraphicsConsole and ConsoleSplitter consumers. The
existing prepare path removes them in order. The full-release model then
requires exactly one NVIDIA PCI owner and its one physical child, with no
active consumer left on that child. Passive GET_PROTOCOL observations are
filtered by the existing snapshot code. No separate EDID owner is invented
or disconnected. Extra active EDID ownership, foreign agents and incomplete
cleanup are rejected.

## Proposed attribute contract

Raw command and cached firmware attributes have separate roles. Capture
requires raw IO/MEM/BME 0/1/0 and the observed attributes 0x300. The captured
attribute baseline remains immutable.
The cache must still equal that baseline immediately before NVIDIA Stop;
an intent/recheck callback cannot move a change into the post-Stop allowance.

After NVIDIA Stop, this model accepts only:

| Raw IO/MEM/BME | Cached attributes |
|---|---|
| 0/0/0 | 0x000 or 0x100 |
| 0/1/0 | 0x200 or 0x300 |

These four outcomes are a proposal for review, not observed MSI Stop results.
The cached IO bit can be retained or cleared; cached memory must agree with
actual memory decode. Every other attribute bit is rejected.

The existing decode policy independently checks all 64 PCI words and permits
only loss of originally enabled IO/MEM bits. Here only MEM was enabled.
An exclusive claim is required before enabling missing memory decode. The
post-Stop attribute observation is pinned across the intent callback; a change
prevents the enable. Restoration must reproduce the complete raw baseline
and exactly the expected cache transition. The resulting attributes are
latched separately and must remain unchanged through final verification.
Raw IO and BME are never enabled by this policy.

PROM allocation precedes the final raw safety/configuration recheck. A later
free failure preserves an earlier read error. Attribute-read failures and
pair-policy failures have separate checkpoints and successful reads record
the actual attribute value.

## Host tests

From Linux or WSL, with Python 3.9+, Clang and nm:

```sh
python3 research/msi-profile/test.py --output /tmp/new-msi-results --fault-controls
```

The output directory must be new. No capture or firmware download is needed.
The harness uses the real prepare, release, decode and full-flow functions,
with generated ownership records and synthetic data checked by real SHA-256.
Synthetic identities require the hosted MSI profile. Buffers are never called
as machine code. Payload and Windows callbacks only increment counters.

Tests cover successful release/restoration, the four declared pairs, invalid
attributes, every PCI word, active/passive ownership, code/hash changes,
partial Stop effects, failed reads/claim/cleanup, callback drift and prevention
of payload/Windows after failure. Attribute-read and pair-policy regressions
assert the exact first failing checkpoint, status and recorded observations;
failed reads must not invent an observed value. Four virtual source faults must fail their
specific regression assertions. Compile controls reject firmware, missing
replay permission, wrong topology and every other current profile selector.
The runner also checks all five supported profiles' cold prechecks and the
existing flow tests. --msi-only limits reruns to changed MSI paths.

## Limits and review request

The fake Stop outcomes are explicit model choices. This does not test vendor
code, actual cached-attribute semantics, payload acceptance, GPU reset,
Windows recovery, storage durability or performance.

The existing RestoreTPL callback window remains: synthetic input drift can
precede NVIDIA disconnect, then stop the flow before decode enable/payload.
No atomicity claim is made for vendor calls.

Review the fixed profile's ownership pins and proposed attribute contract before
considering firmware enablement. No generated-profile envelope, installer,
USB package, boot entry or hardware authorisation is changed here.
