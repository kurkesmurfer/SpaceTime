# SpaceTime Manual Plan

## Purpose

SpaceTime needs a standalone manual for the instrument that exists in this
repository. It should not be a lightly edited or re-skinned 248/248t manual.
The hardware is useful historical and behavioural context, but SpaceTime has
its own architecture, terminology, controls, MIDI implementation, and VCV
workflows.

Write the manual in original language and use original diagrams, screenshots,
and patches. Credit the Buchla 248 and Tiptop Audio/Buchla 248t as inspiration
and identify hardware-verified behaviour where relevant. Do not copy hardware
manual artwork, page design, or substantial passages.

Include this credit line in the published manual:

> SpaceTime is an independent software instrument inspired by the Buchla Model
> 248 and the Tiptop Audio/Buchla 248t, with full credit to Buchla and Tiptop
> Audio for the hardware instruments.

## Source Hierarchy

1. Current SpaceTime implementation and automated tests.
2. `MIDI_IMPLEMENTATION_CHART.md` and verified MIDI acceptance results.
3. `test/patches/README.md` and repeatable Rack test patches.
4. Hardware observations recorded in the specification.
5. Original hardware manuals as factual references, not publishing templates.

Any disagreement should be resolved in favor of tested SpaceTime behavior and
called out as a deliberate compatibility choice or extension.

## Proposed Structure

1. System overview and module placement.
2. Building a minimal HEAD–PROGRAM–STAGE4 instrument.
3. PROGRAM reference: selection, modifiers, modes, pulses, presets, key/scale,
   external A–D inputs, Poly Out, and context-menu settings.
4. STAGE4 reference: voltage/time sliders, external-source interpolation,
   stage LEDs, chaining, and live reordering.
5. HEAD reference: transport, addressing, regions, direction, loop modes,
   internal/external/MIDI/virtual clocks, div/mult, RESET input, and every output.
6. HEAD ALL reference: terminal placement, common commands, mode-copy behavior,
   normalled common CV inputs, local overrides, and MIDI channel 9.
7. MIDI reference: device/channel setup, complete incoming CC chart, global
   realtime messages, per-head virtual clock and transport, visual takeover,
   and outgoing note/CV instrument MIDI.
8. GLUE LEFT/RIGHT reference, valid layouts, link states, and limitations.
9. Patch recipes for single-head, multi-head, externally addressed, hybrid
   CV/MIDI, and separated-rack workflows.
10. Compatibility notes explaining pulse retrigger, regions, timing behavior,
   and all deliberate departures from the hardware.
11. Troubleshooting, CPU guidance, platform support, and test procedures.

## SpaceTime-Specific Coverage

The following topics cannot be derived from the original manual and need
first-class explanations based on this implementation:

- Up to 64 stages, eight independent heads, and dynamic expander discovery.
- Direction, loop, per-head clock source, div/mult, and TIME CV extensions.
- Per-HEAD RESET inputs and the VCV HEAD ALL common-control terminal.
- MIDI channels, CC mapping, realtime clock/transport, virtual clock messages,
  stateful controls, visual feedback, and outgoing MIDI behavior.
- Key/scale quantization, presets with slider takeover, and Poly Out.
- Glue bridges and physically separated SpaceTime chain fragments.
- VCV context-menu configuration and saved-patch behavior.
- Platform packaging and practical hybrid CV/MIDI synchronization.

## Publication Form

The canonical readable manual should live at
`https://www.kurkesmurfer.com/spacetime/manual/`, with stable per-module pages.
The repository should retain the source material needed to maintain it. A PDF
edition can be generated later from the same content after the screenshots and
page layout have stabilized; it should not become an independently edited
second source of truth.

## Completion Inputs

- Review the existing web manual drafts against the current controls.
- Complete the remaining MIDI acceptance checks, especially tick-for-tick
  virtual clock behavior and outgoing mappings.
- Add final Rack screenshots and annotated example patches.
- Record the measured CPU figures and tested Rack/platform versions.
- Decide which hardware-reference PDF, if any, may legally remain distributed
  in the public source repository; a link is preferable when permission is not
  explicit.
