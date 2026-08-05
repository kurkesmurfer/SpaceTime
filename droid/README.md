# SpaceTime DROID test patches

## Stage feedback monitor

`SpaceTimeStageFeedbackTest.ini` is the first hardware test vehicle for
protocol-v1 Stage feedback. It targets the hardware declaration used by the
existing MARFremote patch:

```text
S10, E4, E4, E4, E4, B32, P2B8, P2B8, P2B8, P2B8, P10
```

The patch is intentionally a monitor, not yet a bidirectional editor. DROID's
`encoder.override` drives an E4 ring from external state but disables encoder
movement. This guarantees that selecting a page cannot send stale values back
to SpaceTime while the sixteen-value snapshot is arriving.

### Controls

| DROID control | Function |
|---|---|
| B32 controller 6, buttons 1-8 | Select/request Stage pages 1-8 |
| Selected B32 page button | Press again to request a refresh |
| E4 controllers 2-3, orange | Eight voltage values on the selected page |
| E4 controllers 4-5, cyan | Eight time values on the selected page |
| Master R1-R16 | Raw receive diagnostic for the corresponding sixteen rings |

### MIDI routing

The patch sends requests through USB MIDI. Its `midiin` circuits deliberately
omit `usb` and `trs`, allowing DROID to auto-detect the computer-facing input
stream. On the tested classic MASTER + early X7 system, the Mac is connected
directly to the X7 and the documented port is `usb = 1`; nevertheless, forcing
that value shows X7 MIDI activity without delivering values to the circuits.
Auto-detection receives the same messages correctly. This is a blue-7 firmware
input-selector regression: the USB and TRS selections are reversed. The
explicit blue-7 workaround is `trs = 1` together with `usb = 0`, but these test
patches retain auto-detection so they also remain usable with firmware versions
where the selectors behave as documented. Outgoing MIDI continues to use
`usb = 1`, which works correctly on the same hardware.

- DROID to SpaceTime: channel 10 CC2, value 0-7 requests a page snapshot.
- SpaceTime to DROID: channel 15 CC0-63 carries voltage and CC64-127 carries
  time.

In the SpaceTime MIDI context menu, select the DROID USB destination under
`Controller feedback output`. Do not route that output back into SpaceTime's
controller input.

### Test sequence

1. Install a known pattern in at least two Stage pages, including zero, middle,
   and maximum voltage/time values.
2. Load the DROID patch. Page 1 is selected and requested after MIDI startup.
3. Confirm orange rings 1-8 match voltage and cyan rings 1-8 match time.
   The corresponding Master registers R1-R16 must react as an independent
   indication that DROID consumed the incoming CC values.
4. Select page 2 and confirm all sixteen rings are replaced by page 2 values.
5. Press page 2 again and confirm the same snapshot is requested without a
   page change.
6. Remove the last Stage4 module and request a now-unconnected page. All rings
   for missing stages must clear to zero.
7. Change a Rack Stage slider. With live Stage feedback disabled the ring must
   wait for an explicit page request; with it enabled the ring should follow
   the final coalesced value.

The later bidirectional patch should add takeover or explicit edit/display
modes only after this receive-only path is verified.

### Verified hardware result

The complete receive-only monitor was verified on a classic MASTER plus early
X7 running blue-7, connected to a Mac through the X7 USB port. Page snapshots
and live updates were tested successfully across eight Stage4 modules (32
stages). All voltage and time registers and encoder rings followed both page
selection and live slider changes. MIDI input used auto-detection to avoid the
blue-7 USB/TRS selector reversal described above.

## Full Stage and HEAD remote

`SpaceTimeStageHeadRemote.ini` combines the proven bidirectional 64-stage
editor with a selected-HEAD/HEAD ALL controller. Sixteen private cables hold
the authoritative values received for the selected Stage page. Each matching
MIDI `cctrigger` reloads its encoder's discrete state from that value, including
when an unchanged value is repeated in a snapshot; `snapto` remains connected
as a settling safeguard. MASTER R17-R32 mirror the private values for diagnosis
only: R17-R24 show voltage and R25-R32 show time. The registers do not feed the
encoders or participate in MIDI output.

| Control | Function |
|---|---|
| S1.1 | Stage page 1-8, eight stages per page |
| S1.2 | Feedback/display HEAD 1-8 |
| S1.3 up | Override commands to HEAD ALL (MIDI channel 9) |
| E2, E3 | Eight Stage voltage values |
| E4, E5 | Eight Stage time values |
| Master R17-R24 | Selected page voltage feedback diagnostics |
| Master R25-R32 | Selected page time feedback diagnostics |
| B7.1 | Refresh selected Stage page; rainbow controller retained for paging |
| B8.1, B8.2 | Stop, Play |
| B9.1, B9.2 | Holding indicator, Advance |
| B8.3, B8.4 | Reset, Virtual clock |
| B9.3, B9.4 | Display, refresh selected HEAD or all HEADs |
| P7.1, P7.2 | Address, Time CV amount |
| P8.1, P8.2 | Address source (2 states), Address mode (3 states) |
| P9.1, P9.2 | Direction (5 states), Clock source (4 states) |
| P10.1, P10.2 | Clock div/mult (9 states), Loop mode (3 states) |

The B32 is unused and reserved for the PROGRAM controller.

Each encoder uses the hardware-verified `discrete = 128` model. Incoming CCs
are multiplied by 127 for `snapto`; encoder outputs are scaled by 1/127 for
MIDI. `sensivity = 16` and `movementticks = 1` produce approximately one MIDI
step per physical update and one full range per encoder turn. MIDI output uses
`cctrigger` and is emitted only for physical encoder movement, so a snapshot or
live Rack-side update cannot echo back to SpaceTime as a controller edit.

Initial hardware test:

1. Select page 1 and confirm the rings reproduce its Stage values. Confirm
   R17-R24 reproduce voltage and R25-R32 reproduce time independently.
2. Turn each orange and cyan encoder in both directions and confirm only its
   matching Rack slider moves.
3. Change a Rack slider and confirm its ring moves without subsequent MIDI
   traffic from DROID back to SpaceTime.
4. Switch between two distinct pages and confirm all sixteen diagnostic LEDs
   and rings update. The first encoder turn must continue from the newly
   displayed value without a jump.
5. Turn an encoder through a complete revolution and confirm that it covers
   approximately the complete MIDI range in single-value updates.
6. Select HEAD 1-8 with S1.2 and verify Start, Stop, Advance, Reset and Display
   plus their LEDs against each selected HEAD.
7. Verify all eight HEAD pots. Change HEAD and confirm the virtual-pot pickup does
   not jump the new target before physical movement.
8. Put S1.3 up and verify the same controls address HEAD ALL while feedback and
   LEDs continue to show the individual HEAD selected by S1.2.
9. Confirm Address Source and Address Mode use two and three discrete positions
   respectively and follow the selected HEAD snapshot.

## Fixed page-1 diagnostic

`SpaceTimeStageFeedbackPage1Probe.ini` removes page-dependent CC assignments
and uses MIDI-input port auto-detection. B6.1 requests page 1. DROID's
normalized `midiin.cc1...cc4` outputs feed internal cables, R1-R16, and the E4
rings in parallel. Use this patch when SpaceTime traffic reaches the X7 but the
full monitor remains dark.

## Physical encoder diagnostic

`SpaceTimeStageEncoderProbe.ini` removes all MIDI input and feedback logic.
Turning E2.1 must move its orange ring, change R1, and emit channel 15 CC0.
Use it to distinguish physical encoder ownership/indexing from synchronization
or bidirectional MIDI problems.

`SpaceTimeStageRelativeProbe.ini` is the next isolation step. It keeps E2.1's
movement detector independent from its authoritative feedback display and sends
directional edits from the latest returned CC0 value, using explicit `math` and
`logic` circuits. The resulting MIDI message remains an absolute CC value; no
soft takeover is involved. B6.1 requests page 1. A turn must emit channel 15
CC0 in three-value increments, move the Rack slider, and then display
SpaceTime's returned value without echoing it.

`SpaceTimeStageDiscreteProbe.ini` tests the simpler endless-encoder model.
`discrete = 128` maps the E4's virtual state exactly onto MIDI values 0-127.
Incoming normalized CC0 is multiplied by 127 for `snapto`; encoder output is
scaled by 1/127 for MIDI. Only physical movement triggers transmission, so
feedback cannot echo. `sensivity = 16` makes approximately one full turn cover
the complete MIDI range.
