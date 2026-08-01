# SpaceTime MIDI implementation chart

**Implementation snapshot:** 2026-07-31
**Purpose:** authoritative controller mapping, feedback protocol, and manual MIDI test reference.
**Numbering:** MIDI channels are shown as users see them (1-16). CC and Program
Change data bytes are shown as raw MIDI values (0-127).

## 1. Module placement and device setup

| Item | Current implementation |
|---|---|
| Chain layout | `HEAD ALL - HEAD ... HEAD - MIDI - PROGRAM - STAGE4 ... STAGE4` |
| MIDI module | Singleton, transparent to head numbering |
| Input device | Selected in MIDI module context menu; input channel is forced to All |
| Output device | Selected independently in MIDI module context menu |
| PROGRAM controls channel | Configurable 1-16; default channel 16 |
| Stage sliders channel | Configurable 1-16; default channel 15 |
| HEAD input channels | Fixed: HEAD 1-8 use channels 1-8 |
| HEAD ALL input channel | Fixed: channel 9; CC 0-12 broadcast to all HEADs |
| Head numbering | HEAD 1 is immediately left of MIDI; numbers increase leftward |
| Input map | Fixed CC map; no base-CC offset and no MIDI learn |
| Persistence | Input, performance-output, and feedback-output devices, both input channels, the live-stage option, and all output-lane settings are saved in the Rack patch |

The PROGRAM-controls and stage-slider channels must differ, and channel 10 is
reserved for feedback-protocol requests. The context menu rejects either
conflict. Keep both outside channels 1-10 unless combined routing to a HEAD is
intentional. The defaults, channels 16 and 15 respectively, avoid all fixed
routes. The panel readout shows `P16` and `S15` for these defaults.

## 2. Message value conventions

| Convention | Raw value behavior | DROID implication |
|---|---|---|
| Momentary command | `64-127` fires; `0-63` ignored | A normal `127` press / `0` release pair is safe |
| PROGRAM gesture | `64-127` means up/add/set; `0-63` means down/remove/unset | Do not emit an automatic low release after a high press unless the opposite action is intended |
| Radio selection | `64-127` selects; `0-63` ignored | A normal press/release pair is safe |
| Binary state | `0-63` selects state 0; `64-127` selects state 1 | Send the intended persistent state |
| Continuous 7-bit | Full `0-127` range is scaled | Use an absolute value, not a relative encoder pulse |
| Discrete selector | Raw `0-127` is divided into equal bins | Configure the E4 encoder with the values listed below |

## 3. Incoming PROGRAM and stage MIDI

Stage-slider messages use the configurable stage-slider channel, default MIDI
channel 15. PROGRAM commands, gestures, global controls and Program Change use
the configurable PROGRAM-controls channel, default MIDI channel 16.

### 3.1 Stage sliders

| CC | Target | Raw-to-parameter mapping | Notes |
|---:|---|---|---|
| 0-63 | Stage voltage sliders 1-64 | `value / 127 * 10 V` | CC number equals zero-based stage index |
| 64-127 | Stage time sliders 1-64 | `value / 127` | CC 64 is stage 1, CC 127 is stage 64 |

By default, MIDI changes the effective stage value and moves the visible Rack
slider handle. `MIDI context menu -> Move stage sliders with CC` can be disabled
to use takeover instead: the handle stays at its physical position and manual
control resumes when it crosses the MIDI value. Preset recall always uses
takeover and never moves the handles.

### 3.2 PROGRAM commands

These messages use the PROGRAM-controls channel.

| CC | Control | Accepted values | Effect |
|---:|---|---|---|
| 64 | Select previous stage | `64-127` | One stage backward, circular |
| 65 | Select next stage | `64-127` | One stage forward, circular |
| 66 | Clear | `64-127` | Clears all stages to defaults |
| 67 | Bulk arm | `64-127` | The next PROGRAM edit applies to all stages |

Values below 64 are ignored, so `127` press / `0` release is safe.

### 3.3 Selected-stage gestures

These are relative hardware-lever gestures. High and low are both active.

| CC | Target | `64-127` | `0-63` |
|---:|---|---|---|
| 68 | Quantize | Quantized | Continuous/unquantized |
| 69 | Slope | Increase slew: Stepped -> Slew 1 -> Slew 2 | Decrease slew: Slew 2 -> Slew 1 -> Stepped |
| 70 | Voltage range | Full range | Half range |
| 71 | Voltage source | External A-D | Internal slider |
| 77 | Stop stage flag | Add | Remove |
| 78 | Sustain stage flag | Add | Remove |
| 79 | Enable stage flag | Add | Remove |
| 80 | Cycle First flag | Set | Remove |
| 81 | Cycle Last flag | Set | Remove |
| 86 | Time source | External A-D | Internal slider/range |
| 87 | Pulse 1 stage flag | Add | Remove |
| 88 | Pulse 2 stage flag | Add | Remove |

Do not drive these as a momentary `127` followed by `0`: that performs the
positive action and then immediately performs the negative action.

### 3.4 Selected-stage radio controls

| CC | Selection | Accepted values |
|---:|---|---|
| 72 | Limited range octave -2 | `64-127` |
| 73 | Limited range octave -1 | `64-127` |
| 74 | Limited range octave 0 | `64-127` |
| 75 | Limited range octave +1 | `64-127` |
| 76 | Limited range octave +2 | `64-127` |
| 82 | Time range 0.002-0.03 s | `64-127` |
| 83 | Time range 0.02-0.3 s | `64-127` |
| 84 | Time range 0.2-3 s | `64-127` |
| 85 | Time range 2-30 s | `64-127` |

Values below 64 are ignored. Selecting a Limited octave also selects Limited
range through the normal PROGRAM edit logic.

### 3.5 Presets

| Message | Data value | Effect |
|---|---:|---|
| Program Change | 1-12 | Load preset slots 1-12 |
| Program Change | 0 | Current implementation aliases preset slot 1; do not rely on this compatibility quirk |
| Program Change | 13-127 | Ignored |

### 3.6 PROGRAM global state controls

| CC | Control | Encoding | Recommended DROID values |
|---:|---|---|---|
| 89 | Key | 12 equal bins over 0-127: C through B | `0,12,23,35,46,58,69,81,92,104,115,127` |
| 90 | Scale | 3 equal bins: Major, Minor, Chromatic | `0,64,127` |
| 91 | Pulse Retrig | `<64` Off, `>=64` On | `0,127` |

Key and Scale directly update the current PROGRAM musical state; they do not
simulate the modal panel button sequence. Pulse Retrig directly moves the
visible PROGRAM switch. These are state-setting controls and suit stateful
DROID buttons or discrete encoders.

For one second after CC 89, PROGRAM brightens KEY and the selected C-B LED.
For one second after CC 90, it brightens SCALE and LED 10/11/12 for
Major/Minor/Chromatic. This feedback does not enter a modal panel mode and
temporarily overrides the dim saved-preset indication only for the selected LED.

CC 89 key bins are `0-10`, `11-21`, `22-31`, `32-42`, `43-52`, `53-63`,
`64-74`, `75-84`, `85-95`, `96-105`, `106-116`, and `117-127`. CC 90 uses
the same 3-state bins documented for HEAD selectors: `0-42`, `43-84`, and
`85-127`.

### 3.7 PROGRAM controls without MIDI assignments

| Control | Status |
|---|---|
| Preset Save mode | Panel only |
| Context-menu globals | No incoming MIDI assignments |

## 4. Incoming HEAD CC map

The same CC 0-13 map is reused on channels 1-8. The channel selects the HEAD.
Channel 9 broadcasts the same map through CC 12; Display CC 13 is ignored.

| MIDI channel | Target |
|---:|---|
| 1 | HEAD 1, immediately left of MIDI |
| 2 | HEAD 2 |
| 3 | HEAD 3 |
| 4 | HEAD 4 |
| 5 | HEAD 5 |
| 6 | HEAD 6 |
| 7 | HEAD 7 |
| 8 | HEAD 8, furthest left |
| 9 | HEAD ALL: broadcast CC 0-12 to every HEAD; CC 13 ignored |

### 4.1 HEAD controls

| CC | Control | Input interpretation | Recommended DROID values |
|---:|---|---|---|
| 0 | Virtual clock edge | Fire once at `64-127`; low ignored | Press/tick `127`, release `0` |
| 1 | Start | Fire once at `64-127`; low ignored | Press `127`, release `0` |
| 2 | Stop | Fire once at `64-127`; low ignored | Press `127`, release `0` |
| 3 | Advance | Fire once at `64-127`; low ignored | Press `127`, release `0` |
| 4 | Reset | Fire once at `64-127`; low ignored | Press `127`, release `0` |
| 5 | Address | `0-127` maps to `0-10 V` | Absolute `0-127` |
| 6 | Address source | `<64` Internal, `>=64` External | `0`, `127` |
| 7 | Address mode | 3 equal bins | `0` Strobe, `64` Sequential, `127` Continuous |
| 8 | Direction | 5 equal bins | `0` Fwd, `32` Rev, `64` Pend, `95` Random, `127` Brownian |
| 9 | Clock source | 4 equal bins | `0` Internal, `42` External CV, `85` MIDI clock, `127` Virtual clock |
| 10 | Clock div/mult | 9 equal bins | `0,16,32,48,64,79,95,111,127` |
| 11 | Time CV amount | `value / 127 * 2 - 1` | `0` = -100%, `64` about +0.8%, `127` = +100% |
| 12 | Loop mode | 3 equal bins | `0` One-shot, `64` First/Last, `127` Full chain |
| 13 | Display | Each value `64-127` toggles latch; low ignored | Press `127`, release `0` |

CC 11 has no exact zero in the present 7-bit formula: raw 63 is about -0.8%
and raw 64 is about +0.8%. Use 64 for the nearest positive center value.

### 4.2 Exact discrete-selector bins

| Control | Raw MIDI bins |
|---|---|
| 3-state (CC 7, 12) | state 0: `0-42`; state 1: `43-84`; state 2: `85-127` |
| 4-state (CC 9) | state 0: `0-31`; state 1: `32-63`; state 2: `64-95`; state 3: `96-127` |
| 5-state (CC 8) | state 0: `0-25`; state 1: `26-50`; state 2: `51-76`; state 3: `77-101`; state 4: `102-127` |
| 9-state (CC 10) | `0-14`, `15-28`, `29-42`, `43-56`, `57-70`, `71-84`, `85-98`, `99-112`, `113-127` |

CC 10 state order is `/16, /8, /4, /2, x1, x2, x4, x8, x16`.

### 4.3 HEAD CC behavior notes

- Per-head CC Start and Stop are independent of Follow MIDI transport.
- CC 0 only advances a head whose clock-source switch is set to Virtual.
- CC 7 value 0 is a strobe event, not a persistent Strobe mode.
- CC 9 updates the visible four-way clock-source switch.
- Stop takes precedence if Start and Stop reach a head in the same DSP tick.
- The four continuous/discrete controls reported as flipping between 0 and 127
  on the partial DROID patch are CC 8, 9, 10, and 12; configure them with the
  discrete value sets above.
- Channel 9 reuses the same CC values and does not require a HEAD ALL panel
  module. When installed, HEAD ALL mirrors persistent channel-9 controls on its
  panel while the direct broadcast still reaches every HEAD. CC 13 is
  deliberately ignored because only one HEAD may own Display.
- The mapping therefore consumes one additional MIDI channel, not another set
  of CC numbers. Channels 10-14 remain unassigned by the fixed HEAD map.

## 5. Incoming MIDI System Realtime

Realtime messages have no MIDI channel.

| Status | Message | SpaceTime behavior |
|---:|---|---|
| `F8` | MIDI Clock | Global clock edge; used by every HEAD whose clock source is MIDI |
| `FA` | Start | Start pulse for each HEAD with Follow MIDI transport enabled |
| `FB` | Continue | Same current behavior as Start for opted-in HEADs |
| `FC` | Stop | Stop pulse for each HEAD with Follow MIDI transport enabled |

MIDI clock does not require Follow MIDI transport. DIV/MULT applies to MIDI
clock, external CV clock, and virtual clock. Missing MIDI clock changes the
HEAD indication to HOLD after the starvation timeout. Transport counters stay
synchronized while Follow is disabled, so enabling Follow does not replay old
Start/Stop events.

### 5.1 Clock-rate interpretation

Each received `F8` byte is one base edge before the HEAD DIV/MULT algorithm.
Standard MIDI Clock sends 24 of these edges per quarter note.

| HEAD DIV/MULT | Stage transitions relative to incoming `F8` |
|---|---|
| `/16` | One transition per 16 MIDI Clock bytes |
| `/8` | One transition per 8 MIDI Clock bytes |
| `/4` | One transition per 4 MIDI Clock bytes |
| `/2` | One transition per 2 MIDI Clock bytes |
| `x1` | One transition per MIDI Clock byte |
| `x2`, `x4`, `x8`, `x16` | Two, four, eight, or sixteen transitions per measured MIDI Clock interval |

There is currently no `/24` setting, so no setting directly means one stage
per quarter note. Treat this as an explicit implementation constraint during
sync testing; a dedicated MIDI musical-division layer may be a later design
decision.

Clock pulses are stored as a 1 ms timer and consumed only by the matching
selected source. Select MIDI or Virtual clock before starting its stream: a
clock event received while another source is selected can currently produce
one pending edge when that source is selected later.

## 6. Incoming messages currently ignored

| Message/features | Status |
|---|---|
| Note On/Off input | Ignored; notes are currently output only |
| Poly/channel pressure | Ignored |
| Pitch bend | Ignored |
| SysEx | Ignored |
| MIDI learn / arbitrary maps | Not implemented |
| NRPN input | Deferred |
| 14-bit CC input | Deferred |
| Song Position Pointer | Ignored |

## 7. Outgoing MIDI: per-head lanes

There are eight output lanes, configured under `MIDI context menu -> HEAD N
output`. Each lane stores its own mode, output channel, note gate source, and
CC number.

### 7.1 Defaults

| Lane setting | Default |
|---|---|
| Mode | Off |
| Output channel | Same as head number: HEAD 1-8 -> channel 1-8 |
| Note gate source | ALL |
| CC number | HEAD 1-8 -> CC 20-27 |

### 7.2 Notes mode

| Property | Current behavior |
|---|---|
| Pitch source | Current HEAD CV after stage source, PROGRAM modifiers, quantization, and HEAD playback |
| Eligibility | Note On is emitted only when the current stage is Quantized |
| Conversion | `note = clamp(round(CV * 12) + 60, 0, 127)` |
| Reference | 0 V -> note 60; 1 V -> note 72 |
| Note On | Status `9n`, configured channel, velocity 100 |
| Note Off | Status `8n`, same note, velocity 0 |
| Gate source P1/P2 | Note On at gate rise; Note Off at gate fall; 30 s safety timeout |
| Gate source ALL | Note On at each ALL rise; fixed 30 ms note duration |
| Head Stop | Immediately releases an active note |
| Lane Off/mode change | Releases an active note on the next status update |
| Missing/removed head | Releases its active note |

PROGRAM Pulse Retrig directly affects P1/P2 note articulation. With Pulse
Retrig On, adjacent pulse-enabled stages create Note Off / Note On boundaries.
With it Off, the gate stays high, so adjacent stages form one sustained note
and a changed stage pitch does not retrigger the MIDI note.

An unquantized stage is silent at a qualifying gate rise. Quantization is an
eligibility flag as well as the pitch rounding already present in the HEAD CV.

### 7.3 CC 7-bit mode

| Property | Current behavior |
|---|---|
| Source | Current HEAD CV |
| Conversion | Clamp CV to 0-10 V, then `round(CV / 10 * 127)` |
| Destination | Per-lane configured channel and CC number |
| Emission | Send only when the 7-bit value changes |
| Throttle | At least 3 ms between messages per lane |
| Gate dependence | None; output follows CV whether the selected note gate is high or low |

### 7.4 Outgoing features deferred

| Feature | Status / reservation |
|---|---|
| 14-bit CV/CC emission | Deferred by design; intended for high-resolution targets such as Expert Sleepers disting NT |
| 14-bit CC number pair | Not assigned yet |
| NRPN output | Deferred |
| MIDI clock output | Deferred |
| Trigger-note global clock lane | Deferred |

Do not reserve DROID controls or fixed CC pairs for 14-bit output yet. The
MSB/LSB numbering, lane mode, update ordering, and throttle should be specified
when that work starts.

## 8. Controller feedback protocol v1 (frozen)

Protocol v1 gives stateful controllers an authoritative view of SpaceTime after
panel edits, MIDI edits, patch reloads, and controller page changes. It is a
controller-independent CC protocol: Midilize, DROID, or another adapter maps
these semantic messages onto the controls and LEDs of a particular surface.

This section freezes the wire format. HEAD/HEAD ALL feedback, protocol probing,
the separate VCV output, and sparse HEAD deltas are implemented. PROGRAM and
Stage feedback remain capability-gated until their authoritative state adapters
are completed.

### 8.1 Transport and device rules

| Rule | Protocol v1 decision |
|---|---|
| Feedback device | A separate MIDI output selected in the MIDI module context menu; Off by default |
| Input device | Snapshot requests arrive through the existing SpaceTime MIDI input |
| Feedback channels | Fixed and independent of the configurable PROGRAM-controls and stage-slider input channels |
| Message type | Ordinary MIDI CC only; no SysEx, NRPN, notes, or Program Change |
| Live updates | Sparse state deltas, emitted only when a semantic value changes |
| Resynchronization | Controller sends an explicit, stateless snapshot request for the area/page it is showing |
| Multiple controllers | Supported: every listener sees deltas and snapshot replies; SpaceTime stores no controller identity or page |
| Clock traffic | MIDI Clock, virtual-clock ticks, and phase are not echoed |
| Momentary acknowledgement | Advance and Reset emit a short `127` then `0` acknowledgement; these are not persistent state |
| Value convention | Enumerations use their exact zero-based index; binary state uses `0`/`127`; continuous values use `0-127` |

Selecting a feedback output is the explicit declaration that a stateful
controller is attached. Performance output lanes continue to use the existing
MIDI output. Keeping feedback on its own device prevents LED/status traffic
from entering a synthesizer or colliding with per-head note and CV lanes.

### 8.2 Fixed channel allocation

| MIDI channel | Feedback role |
|---:|---|
| 1-8 | HEAD 1-8 state; channel number equals visible HEAD number |
| 9 | Reserved for a future aggregate HEAD ALL status; no v1 state is emitted here |
| 10 | Protocol requests, version response, and snapshot framing |
| 11-14 | Reserved for future SpaceTime feedback areas |
| 15 | Stage voltage/time values |
| 16 | PROGRAM and selected-stage programming state |

HEAD ALL is a command target, not a ninth head with one authoritative state.
After a broadcast, heads may diverge through local panel, CV, or MIDI control.
Its snapshot request therefore returns individual HEAD messages on channels
1-8. Controller adapters may show an ALL layer while deriving its LEDs from
those eight truthful states.

Channel 10 is reserved at the VCV input adapter. It cannot simultaneously be
selected as the configurable PROGRAM-controls or stage-slider channel.

### 8.3 Snapshot requests and framing (channel 10)

Requests use the existing MIDI input. Values below 64 are ignored for the
PROGRAM request; HEAD and Stage page values are exact indices and must not be
followed by an automatic release message.

| CC | Direction | Value | Meaning |
|---:|---|---:|---|
| 0 | Controller -> SpaceTime | `0-7` | Request one HEAD snapshot; value 0 means HEAD 1 |
| 0 | Controller -> SpaceTime | `8` | Request all eight HEAD snapshots (HEAD ALL view) |
| 1 | Controller -> SpaceTime | `64-127` | Request PROGRAM snapshot |
| 2 | Controller -> SpaceTime | `0-7` | Request one eight-stage page; page 0 is stages 1-8 |
| 3 | Controller -> SpaceTime | `64-127` | Protocol probe/version request |
| 3 | SpaceTime -> Controller | `1` | Protocol major version 1 |
| 118 | SpaceTime -> Controller | area code | Snapshot begins |
| 119 | SpaceTime -> Controller | same area code | Snapshot ends |

Snapshot area codes are `0-7` for HEAD 1-8, `8` for all HEADs, `16` for
PROGRAM, and `32-39` for stage pages 1-8. Framing is emitted only for explicit
snapshots, never around sparse deltas. A controller must treat a repeated value
as valid inside a snapshot even if it equals its cached state.

The request is stateless: the desired head or stage page is carried in every
message. SpaceTime does not remember which page any controller is displaying.

### 8.4 HEAD state and acknowledgements (channels 1-8)

Where practical, feedback reuses the incoming HEAD CC number. Start and Stop
are represented as mutually exclusive status LEDs rather than command echoes.

| CC | Feedback value |
|---:|---|
| 1 | Running: `127` only in RUNNING, otherwise `0` |
| 2 | Stopped: `127` only in STOPPED, otherwise `0` |
| 3 | Advance acknowledgement pulse: `127`, then `0` |
| 4 | Reset acknowledgement pulse: `127`, then `0` |
| 5 | Address, absolute `0-127` |
| 6 | Address source: `0` Internal, `127` External |
| 7 | Address mode: `0` Strobe, `1` Sequential, `2` Continuous |
| 8 | Direction: `0` Forward, `1` Reverse, `2` Pendulum, `3` Random, `4` Brownian |
| 9 | Clock source: `0` Internal, `1` External CV, `2` MIDI, `3` Virtual |
| 10 | Clock div/mult: `0-8` for `/16, /8, /4, /2, x1, x2, x4, x8, x16` |
| 11 | Time CV amount, absolute `0-127` using the incoming CC 11 scale |
| 12 | Loop mode: `0` One-shot, `1` First/Last, `2` Full chain |
| 13 | Display ownership: `0` Off, `127` On |
| 14 | Holding: `127` only in HOLDING, otherwise `0` |
| 15 | Current stage: exact zero-based index `0-63` |
| 16 | HEAD present: `127`; an absent requested head reports `0` |
| 17 | Follow MIDI transport: `0` Off, `127` On |

CC 0 is deliberately unused: echoing clock ticks would turn feedback into a
high-rate clock stream. Snapshot order is ascending CC number. A change of run
state emits CC 1, 2, and 14 together so a controller cannot retain a stale
RUN/STOP/HOLD LED combination.

### 8.5 PROGRAM state (channel 16)

| CC | Feedback value |
|---:|---|
| 0 | Selected stage: exact zero-based index `0-63` |
| 1 | Key: exact index `0-11` for C-B |
| 2 | Scale: `0` Major, `1` Minor, `2` Chromatic |
| 3 | Pulse Retrig: `0` Off, `127` On |
| 4 | Bulk arm active: `0` Off, `127` On |
| 16 | Selected-stage Quantize: `0` Continuous, `127` Quantized |
| 17 | Selected-stage slope: `0` Stepped, `1` Slew 1, `2` Slew 2 |
| 18 | Selected-stage voltage range: `0` Full, `1` Half, `2` Limited |
| 19 | Selected-stage Limited octave: exact index `0-4` for -2 through +2 |
| 20 | Selected-stage voltage source: `0` Internal, `127` External |
| 21 | Selected-stage Stop flag: `0` Off, `127` On |
| 22 | Selected-stage Sustain flag: `0` Off, `127` On |
| 23 | Selected-stage Enable flag: `0` Off, `127` On |
| 24 | Selected-stage First flag: `0` Off, `127` On |
| 25 | Selected-stage Last flag: `0` Off, `127` On |
| 26 | Selected-stage time range: exact index `0-3` |
| 27 | Selected-stage time source: `0` Internal, `127` External |
| 28 | Selected-stage Pulse 1 flag: `0` Off, `127` On |
| 29 | Selected-stage Pulse 2 flag: `0` Off, `127` On |

Changing selected stage emits CC 0 followed by CC 16-29 for the newly selected
stage. This is one bounded semantic refresh, not a full 64-stage dump. PROGRAM
snapshot order is CC 0-4 followed by CC 16-29.

### 8.6 Stage values (channel 15)

Stage feedback mirrors the slider map exactly:

| CC | Feedback value |
|---:|---|
| 0-63 | Stage voltage 1-64, scaled from 0-10 V to `0-127` |
| 64-127 | Stage time 1-64, scaled from 0-1 to `0-127` |

An eight-stage page snapshot emits sixteen values: voltage CC `8p` through
`8p+7`, then time CC `64+8p` through `71+8p`, where `p` is page `0-7`.
Requests for stages beyond the connected chain return `0` so controllers can
clear stale rings or faders after modules are removed.

Live stage deltas are opt-in through a MIDI-module context setting, disabled by
default. When enabled, they are coalesced and rate-limited before transmission;
only the latest 7-bit value for a changed stage is required. Explicit page
snapshots work whenever a feedback output is selected, regardless of the live
stage-delta setting.

### 8.7 Ordering and restart behavior

- SpaceTime state is authoritative; controllers must overwrite cached LEDs and
  rings with each snapshot reply.
- On controller, Midilize document, Rack patch, or MIDI-device reconnect, the
  adapter requests the visible HEAD/HEAD ALL area, PROGRAM, and visible stage
  page. No assumed controller state survives a reconnect.
- Persistent deltas carry final state, not gestures. Panel and incoming-MIDI
  changes therefore produce the same feedback.
- Snapshot replies and deltas may interleave with performance MIDI only at the
  operating-system level because they use separate output devices.
- Feedback loops are the controller adapter's responsibility: messages sent to
  controller LEDs must not be routed back into SpaceTime's input as commands.

### 8.8 Reserved and deferred feedback

Protocol v1 does not report audio-rate phase/CV, raw clock ticks, per-stage
program bitfields, preset contents, note-lane state, or 14-bit values. Channels
9 and 11-14, protocol CC 4-117, and unused area codes remain reserved; future
versions must not reinterpret any v1 assignment.

## 9. Activity and diagnostics

| Indicator/diagnostic | Meaning |
|---|---|
| MIDI IN LED | Any received MIDI message, including ignored messages |
| CLK LED | Received `F8` MIDI Clock |
| MIDI OUT LED | A performance or controller-feedback MIDI message was emitted |
| MIDI context `Last` | Last decoded status/channel/number/value and route |
| Route `program` | Message matched PROGRAM-controls channel |
| Route `stage` | Message matched stage-slider channel |
| Route `head` | CC matched HEAD channel 1-8 |
| Route `head all` | CC matched fixed HEAD ALL channel 9 |
| Route `program+head` | PROGRAM channel overlaps a HEAD channel |
| Route `stage+head` | Stage-slider channel overlaps a HEAD channel |
| HEAD context diagnostic | Last applied HEAD CC, scaled value, and sequence |
| PROGRAM context diagnostic | Last consumed PROGRAM MIDI event and op count |

Continuous MIDI Clock quickly becomes the MIDI module's last diagnostic
message, so use Stoermelder MIDI-MON for ordered raw traffic when debugging
transport or CC interleaving.

## 10. DROID implementation checklist

### Input/controller side

- [ ] Use PROGRAM-controls channel 16 and stage-slider channel 15 unless
      deliberately testing HEAD overlap.
- [ ] Use channels 1-8 for HEAD 1-8.
- [ ] Use channel 9 for HEAD ALL CC 0-12; confirm CC 13 has no effect.
- [ ] Configure PROGRAM gesture controls as two-direction actions, not ordinary
      press/release buttons.
- [ ] Configure CC 8, 9, 10, and 12 as discrete E4 encoder ranges.
- [ ] Configure HEAD CC 0-4 and 13 as `127` press / `0` release.
- [ ] Keep realtime Start/Continue/Stop separate from per-head CC Start/Stop.
- [ ] Emit MIDI Clock globally; select MIDI clock per HEAD with CC 9 or panel.
- [ ] Account for MIDI Clock's current 24-PPQN edge interpretation; do not
      assume `x1` means one stage per quarter note.
- [ ] Configure CC 89 as a 12-position key encoder and CC 90 as a 3-position
      scale encoder.
- [x] Confirm CC 89/90 produces the one-second KEY/SCALE and selection LED
      feedback without arming a panel mode.
- [x] Confirm CC 91 mirrors the PROGRAM Pulse Retrig switch visually.
- [ ] Configure the corresponding stateful CC 91 control in the DROID patch.
- [ ] Leave preset Save and context-menu globals as panel-only controls.

### Output/test side

- [x] MIDI output device selection works with IAC 1.
- [x] Note On and Note Off reach MIDI-MON.
- [ ] Verify exact notes: 0 V -> 60 and 1 V -> 72.
- [ ] Verify P1 and P2 gate-fall Note Off timing.
- [ ] Verify ALL produces one 30 ms note per stage transition.
- [ ] Verify Pulse Retrig On and Off articulation across adjacent pulse stages.
- [ ] Verify Off releases an active note.
- [ ] Verify two or more HEAD lanes on independent output channels.
- [ ] Verify CC 7-bit values 0, midpoint, and 127 plus the 3 ms throttle.
- [ ] Verify unquantized stages emit no Note On.
- [ ] Verify virtual clock tick-for-tick operation separately.
- [ ] Stress-test MIDI Clock plus multiple CC lanes and eight HEADs.
- [ ] Add 14-bit CV emission tests when that mode is designed and implemented.

## 11. Current implementation status

| Area | Status |
|---|---|
| PROGRAM/stage CC input | Implemented through CC 91; slider/modifier tests and CC 89/90/91 VCV visual feedback passed; DROID integration pending |
| Per-head CC input | Implemented; Start/Stop and basic routing passed |
| HEAD ALL CC input | Implemented on fixed channel 9 for CC 0-12; Display excluded |
| MIDI Clock input | Implemented; DROID USB clock reaches CLK indicator |
| MIDI realtime transport | Implemented; Start/Stop working, stale-event synchronization fixed; comprehensive retest pending |
| Virtual clock input | Implemented; tick-for-tick secondary verification pending |
| Outgoing Notes | Implemented first pass; Note On/Off arrival confirmed |
| Outgoing CC 7-bit | Implemented first pass; comprehensive value/timing test pending |
| 14-bit CV/CC output | Explicitly deferred |
| NRPN | Deferred |
| MIDI Clock output | Deferred |
| Controller feedback protocol v1 | VCV HEAD/HEAD ALL snapshots, sparse deltas, probe reply, separate output selection, persistence, and diagnostics implemented; PROGRAM/Stage responses capability-gated |

## 12. Shared implementation ownership

All platform-neutral MIDI behavior lives in `dsp/MidiCore.hpp`: raw message
decoding, PROGRAM/HEAD routing, realtime counters, output-lane state, pitch/CC
conversion, note lifetime, and diagnostics. `vcv/src/Midi.cpp` is now a thin
Rack adapter for MIDI device queues, menus, LEDs, JSON persistence, and the
expander connection.

The MetaModule implementation should provide its own similarly thin adapter
around the same `MidiCore`, including a `MidiOutputSink` for hardware output.
Future 14-bit CV emission belongs in `MidiCore` so both platforms receive the
same mapping, ordering, and throttling behavior.

Controller feedback likewise belongs in a platform-neutral shared core. VCV
and MetaModule adapters own only feedback MIDI device I/O, menus, persistence,
and host-specific reconnect detection.

`dsp/MidiFeedback.hpp` implements protocol-v1 request decoding, snapshot
framing, semantic Head/PROGRAM deltas, acknowledgement pulses, and coalesced
stage-value deltas. It consumes a fixed-size `MidiFeedbackState`; adapters are
responsible for populating that state from their local module topology.

The VCV HEAD vertical slice publishes its complete semantic control state and
Advance/Reset event counters through `HeadStatus`. The MIDI anchor collects
that merged status by stable head id into `MidiFeedbackState` and drives a
separate, persistent Rack MIDI output selected under `Controller feedback
output`. The output defaults Off. PROGRAM and Stage requests are consumed but
deliberately unanswered until those state adapters are implemented.
