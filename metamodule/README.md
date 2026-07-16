# SpaceTime MetaModule bus probe

This package is deliberately disposable. It verifies that two modules from one
MetaModule plugin can exchange real-time state through shared plugin memory.
It is not a SpaceTime release and must not be published as one.

## Build

MetaModule requires ARM GNU Toolchain 12.2 or 12.3. VCV and Daisy may continue
using their existing ARM GNU 10 toolchains; this target does not change them.

```sh
cmake --fresh -S metamodule -B metamodule/build -G Ninja \
  -DMETAMODULE_SDK_DIR=/Users/peet/Development/metamodule-plugin-sdk \
  -DTOOLCHAIN_BASE_DIR=/Applications/ArmGNUToolchain/12.3.rel1/arm-none-eabi/bin
cmake --build metamodule/build
```

The installable probe is written to
`metamodule/metamodule-plugins/SpaceTimeProbe.mmplugin`.

## Hardware test

1. Install `SpaceTimeProbe.mmplugin` and add one BUS PROBE CORE and one BUS
   PROBE REMOTE to a patch.
2. Set both Instrument IDs to A. Their displays must change from `WAIT` to
   `LINK`; both STATUS outputs must read +5 V.
3. Patch a changing voltage into CORE PUBLISH. REMOTE SEEN CORE must reproduce
   it without a virtual cable between the probes.
4. Patch a different voltage into REMOTE SEND. CORE SEEN REMOTE must reproduce
   it.
5. Open the CPU map and place the probes on different processor cores. If the
   firmware does not permit manual placement, add enough unrelated processing
   modules to make it assign them to different cores. Repeat both directions
   under load and record the observed assignment.
6. Save and reload the patch. Link and values must recover without intervention.
7. Add a second Core on ID A. Displays must show `DUP` and STATUS must be -5 V.
   Remove either Core; the remaining Core must claim the bus and relink.
8. Change both probes to ID B and repeat. A probe on A must remain unlinked.
9. Delete and re-add each endpoint, then unload/reload the plugin twice. No
   stale owner or false `LINK` state may survive.

Record firmware version, sample rate, CPU allocation/load and any dropped or
stale updates. Product work starts only after every item passes on hardware.
