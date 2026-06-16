// Pioneer-DJM-S7-script.js
// ****************************************************************************
// * Mixxx mapping script file for the Pioneer DJ DJM-S7.
// * Author: Thomas Wunner
// * Based on the official "DJM-S7 List of MIDI message" (AlphaTheta, v1.00)
// *
// * The DJM-S7 is a 2-channel Serato battle mixer. It has no transport or jog
// * controls of its own - playback is driven by timecode turntables (e.g. the
// * Pioneer PLX-CRSS12) via Mixxx vinyl control. This mapping therefore covers:
// *   * Mixer (channel faders, TRIM, 3-band ISO EQ, COLOR filter, crossfader,
// *            master/booth/headphone levels)  -- all 14-bit
// *   * Browse / Load
// *   * Loop (auto loop, half/double)
// *   * Performance pads with software-tracked pad modes
// *     (HOT CUE / ROLL / SAVED LOOP / SAMPLER)
// *   * BEAT FX LEVEL/DEPTH -> Effect Unit 1 mix
// *
// * Mixer note: the controls map to Mixxx's INTERNAL mixer. When the DJM-S7 is
// * used as a hardware mixer (external mixing mode with DVS), simply leave those
// * controls unused - Mixxx then only needs the deck/pad/FX parts.
// *
// * The performance pads send the same MIDI note in every pad mode; the active
// * mode is selected by the mode buttons and tracked here in software.
// ****************************************************************************

var PioneerDJMS7 = {};

// MIDI status bytes per section (from the official MIDI message list)
PioneerDJMS7.padStatus = {
    "[Channel1]": 0x97, // PERFORMANCE PAD DECK1 (channel 8)
    "[Channel2]": 0x98, // PERFORMANCE PAD DECK2 (channel 9)
};

// Deck note status for mode-button LEDs (DECK1 = 0x90, DECK2 = 0x91)
PioneerDJMS7.deckStatus = {
    "[Channel1]": 0x90,
    "[Channel2]": 0x91,
};

// Pad mode button notes (sent on the deck channel)
PioneerDJMS7.modeForNote = {
    0x1B: "hotcue",
    0x1E: "roll",
    0x20: "savedloop",
    0x22: "sampler",
};

PioneerDJMS7.modeNoteForName = {
    hotcue: 0x1B,
    roll: 0x1E,
    savedloop: 0x20,
    sampler: 0x22,
};

// Beat sizes per pad (pad 1..8) for the ROLL and SAVED LOOP modes
PioneerDJMS7.rollSizes = [0.0625, 0.125, 0.25, 0.5, 1, 2, 4, 8];
PioneerDJMS7.loopSizes = [0.0625, 0.125, 0.25, 0.5, 1, 2, 4, 8];

// Pad RGB colour palette. The DJM-S7 sets a pad colour via a Note On whose
// velocity is a palette index. Both the index palette and the matching RGB
// values were reverse-engineered from a Serato USB capture (the full Serato
// hotcue colour grid was walked in order while sniffing the USB traffic).
// Keys are the swatch RGB, values the device palette index. Mixxx hotcue
// colours are matched to the nearest entry by ColorMapper, so ANY cue colour
// is rendered dynamically with the closest available pad colour.
// These 8 index values are confirmed: Serato sends them as the default pad
// colours (seen on channel 0x9D, notes 0x00-0x07, in the USB capture). The
// index increases monotonically around the hue wheel (blue -> red -> magenta).
PioneerDJMS7.padColorPalette = {
    0x0F88CA: 0x01, // blue
    0x1DBEBD: 0x0C, // cyan
    0x4EB648: 0x15, // green
    0xFAC313: 0x1D, // yellow
    0xF8821A: 0x24, // orange
    0xC02626: 0x2A, // red
    0xCE359E: 0x37, // magenta
    0x6823B6: 0x3A, // violet
};
PioneerDJMS7.colorMapper = null; // ColorMapper, created in init

// Fixed pad colours for the non-hotcue modes
PioneerDJMS7.modeColor = {
    roll: 0x0C,      // cyan
    savedloop: 0x1D, // yellow
    sampler: 0x37,   // magenta
};

// Dim colour for an empty hotcue pad, so the pad grid stays visible (like Serato)
PioneerDJMS7.emptyPadColor = 0x0A;

// Mode-button LED colours (from the USB capture). Every mode button stays lit
// in its colour; the dim value is used when the mode is not selected and the
// bright value (~dim + 0x40) when it is the active mode.
PioneerDJMS7.modeButtonColor = {
    hotcue: {dim: 0x40, bright: 0x7F},
    roll: {dim: 0x11, bright: 0x51},
    savedloop: {dim: 0x19, bright: 0x59},
    sampler: {dim: 0x3B, bright: 0x79},
};

// Active pad mode per deck
PioneerDJMS7.padMode = {
    "[Channel1]": "hotcue",
    "[Channel2]": "hotcue",
};

// Full startup handshake, reverse-engineered from a Serato USB capture
// (serato-startup.pcapng). It enables the performance pads (pad MIDI output)
// AND - crucially - the "00 0A" LED-state init plus the 31/32 commands put the
// device into the PERSISTENT pad-LED mode. Without that block, pad colours set
// via Note On only flash and then revert to white. Stored as hex strings and
// sent verbatim in init().
PioneerDJMS7.enableSysexHex = [
    "F0 00 20 7F 01 02 01 01 3F 06 02 0A 01 05 04 01 09 09 0E 04 05 09 08 0F 09 F7",
    "F0 00 20 7F 03 01 F7",
    "F0 00 20 7F 50 01 F7",
    "F0 00 20 7F 11 00 00 00 00 00 05 F7",
    "F0 00 20 7F 00 0A 00 71 00 6F 00 24 15 32 55 48 14 21 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 51 22 4C 02 65 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 48 2A 64 2A 11 29 42 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F7",
    "F0 00 20 7F 00 0A 00 71 00 6F 01 24 15 32 55 48 14 21 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 51 22 4C 02 65 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 48 2A 64 2A 11 29 42 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F7",
    "F0 00 20 7F 12 00 00 00 00 00 05 F7",
    "F0 00 20 7F 32 10 F7",
    "F0 00 20 7F 31 10 F7",
    "F0 00 20 7F 31 11 F7",
];

// Periodic keep-alive that Serato keeps sending after the handshake. Only while
// this keeps arriving does the unit stay in the persistent pad-LED mode in which
// host-set Note On colours stick. The device never holds pad colours on its own -
// confirmed on hardware: without the handshake the pads are dead (no input) and
// only flash a fixed default; with the handshake but a slow keep-alive the colours
// flash and revert. Persistent colours therefore need Serato's ~250 ms cadence.
//
// 250 ms requires this branch's asynchronous PortMidi output queue (dedicated
// output thread); with it the send is non-blocking and pad input keeps working.
// On a stock Mixxx WITHOUT that queue the blocking sysex at 250 ms jams the pad
// subsystem (presses stop arriving, LEDs go dark) - raise to 1000 there (pads
// work, colours fade).
PioneerDJMS7.heartbeatSysex = [0xF0, 0x00, 0x20, 0x7F, 0x50, 0x01, 0xF7];
PioneerDJMS7.heartbeatIntervalMs = 250;
PioneerDJMS7.heartbeatTimer = 0;

//
// Init / Shutdown
//

PioneerDJMS7.init = function() {
    // Run the full handshake first (enables pads + persistent LED mode), then
    // keep the device alive with the periodic heartbeat. With the persistent LED
    // mode active, pad colours set via Note On stick, so no LED re-flooding is
    // needed (a fast re-send blocks incoming pad presses).
    PioneerDJMS7.enableSysexHex.forEach(function(hex) {
        const msg = hex.split(" ").map(function(h) {
            return parseInt(h, 16);
        });
        midi.sendSysexMsg(msg, msg.length);
    });
    PioneerDJMS7.heartbeatTimer = engine.beginTimer(PioneerDJMS7.heartbeatIntervalMs, function() {
        midi.sendSysexMsg(PioneerDJMS7.heartbeatSysex, PioneerDJMS7.heartbeatSysex.length);
    });

    PioneerDJMS7.loadFxUnits();

    PioneerDJMS7.colorMapper = new ColorMapper(PioneerDJMS7.padColorPalette);

    // Make sure enough samplers exist for the SAMPLER pad mode (8 per deck)
    const samplerCount = 16;
    if (engine.getValue("[App]", "num_samplers") < samplerCount) {
        engine.setValue("[App]", "num_samplers", samplerCount);
    }

    // Keep hotcue pad LEDs in sync with Mixxx (status and colour)
    Object.keys(PioneerDJMS7.padStatus).forEach(function(group) {
        for (let i = 1; i <= 8; i++) {
            engine.makeConnection(group, "hotcue_" + i + "_status", PioneerDJMS7.hotcueStatusChanged);
            engine.makeConnection(group, "hotcue_" + i + "_color", PioneerDJMS7.hotcueStatusChanged);
        }
        PioneerDJMS7.updateModeLeds(group);
        PioneerDJMS7.updatePadLeds(group);
    });
};

PioneerDJMS7.shutdown = function() {
    if (PioneerDJMS7.heartbeatTimer) {
        engine.stopTimer(PioneerDJMS7.heartbeatTimer);
        PioneerDJMS7.heartbeatTimer = 0;
    }
    Object.keys(PioneerDJMS7.padStatus).forEach(function(group) {
        const padStatus = PioneerDJMS7.padStatus[group];
        const deckStatus = PioneerDJMS7.deckStatus[group];

        // Turn off all pad LEDs (base + shifted)
        for (let note = 0x00; note <= 0x0F; note++) {
            midi.sendShortMsg(padStatus, note, 0x00);
        }
        // Turn off all mode button LEDs
        Object.keys(PioneerDJMS7.modeForNote).forEach(function(note) {
            midi.sendShortMsg(deckStatus, parseInt(note, 10), 0x00);
        });
    });
};

//
// Browse encoder
//
// The browse encoder is a signed relative control: clockwise sends 0x01..0x1E,
// counter-clockwise sends 0x7F..0x62 (i.e. -1..-30 in 7-bit two's complement).
//

PioneerDJMS7.browseEncoder = function(_channel, _control, value, _status, _group) {
    const delta = value < 0x40 ? value : value - 128;
    engine.setValue("[Library]", "MoveVertical", delta);
};

//
// Headphone cue
//
// The DJM-S7 has no per-deck cue buttons - it uses a headphone cue crossfader.
// Mixxx PFL is on/off per deck (no intensity blend), so the fader is mapped as:
// left = cue deck 1, right = cue deck 2, centre = cue both decks.
//

PioneerDJMS7.headphoneCueMix = function(_channel, _control, value, _status, _group) {
    const cueDeck1 = value <= 0x4A; // left half + centre band
    const cueDeck2 = value >= 0x36; // right half + centre band
    engine.setValue("[Channel1]", "pfl", cueDeck1 ? 1 : 0);
    engine.setValue("[Channel2]", "pfl", cueDeck2 ? 1 : 0);
};

//
// Crossfader
//
// The hardware crossfader is centred at ~0x2900 (10500), not the 14-bit midpoint
// 0x2000, so a direct mapping would sit off-centre in Mixxx. The two halves are
// scaled separately so the physical centre maps to 0. Adjust crossfaderCenter if
// the centre is still slightly off.
//

PioneerDJMS7.crossfaderCenter = 0x2900;
PioneerDJMS7.crossfaderMsb = 0x40;

PioneerDJMS7.crossfaderMsbInput = function(_channel, _control, value, _status, _group) {
    PioneerDJMS7.crossfaderMsb = value;
};

PioneerDJMS7.crossfaderLsbInput = function(_channel, _control, value, _status, _group) {
    const raw = (PioneerDJMS7.crossfaderMsb << 7) | value;
    const center = PioneerDJMS7.crossfaderCenter;
    const position = raw <= center
        ? raw / center - 1
        : (raw - center) / (0x3FFF - center);
    engine.setValue("[Master]", "crossfader", position);
};

//
// Sampler volume
//
// The DJM-S7 has a single SAMPLER VOLUME knob (attenuator, -inf..0). It is
// mapped to the pregain of all samplers at once (0 .. unity).
//

PioneerDJMS7.samplerVolume = function(_channel, _control, value, _status, _group) {
    const gain = value / 0x7F;
    for (let i = 1; i <= 16; i++) {
        engine.setValue("[Sampler" + i + "]", "pregain", gain);
    }
};

//
// Beat FX
//
// The 6 BEAT FX SELECT buttons (the device must be in BEAT FX mode):
//   Echo / Flanger / Reverb / Phaser each toggle a dedicated effect unit on/off,
//   so several effects can run at the same time. LEVEL/DEPTH is the shared
//   wet/dry of all units. Back Spin and Vinyl Brake are not Mixxx effects - they
//   are recreated with the built-in spinback/brake deck functions.
// The effects apply to both decks (the device's FX channel-select is not
// mapped). The loaded_effect indices below are alphabetical positions in Mixxx's
// effect list; adjust them if extra effects (e.g. LV2) shift the order.
//

PioneerDJMS7.fxUnitEffect = {
    1: 6,  // Echo
    2: 8,  // Flanger
    3: 15, // Reverb
    4: 13, // Phaser
};

PioneerDJMS7.fxUnitForNote = {
    0x00: 1, // Echo
    0x02: 2, // Flanger
    0x03: 3, // Reverb
    0x05: 4, // Phaser
};

PioneerDJMS7.loadFxUnits = function() {
    Object.keys(PioneerDJMS7.fxUnitEffect).forEach(function(unit) {
        const effect = "[EffectRack1_EffectUnit" + unit + "_Effect1]";
        engine.setValue(effect, "loaded_effect", PioneerDJMS7.fxUnitEffect[unit]);
        engine.setValue(effect, "enabled", 1);
    });
};

PioneerDJMS7.fxToggle = function(_channel, control, value, _status, _group) {
    if (value === 0) {
        return;
    }
    const unitGroup = "[EffectRack1_EffectUnit" + PioneerDJMS7.fxUnitForNote[control] + "]";
    const newState = engine.getValue(unitGroup, "group_[Channel1]_enable") > 0 ? 0 : 1;
    engine.setValue(unitGroup, "group_[Channel1]_enable", newState);
    engine.setValue(unitGroup, "group_[Channel2]_enable", newState);
};

PioneerDJMS7.fxLevelDepth = function(_channel, _control, value, _status, _group) {
    const mix = value / 0x7F;
    for (let unit = 1; unit <= 4; unit++) {
        engine.setValue("[EffectRack1_EffectUnit" + unit + "]", "mix", mix);
    }
};

PioneerDJMS7.backSpin = function(_channel, _control, value, _status, _group) {
    const active = value > 0;
    engine.spinback(1, active);
    engine.spinback(2, active);
};

PioneerDJMS7.vinylBrake = function(_channel, _control, value, _status, _group) {
    const active = value > 0;
    engine.brake(1, active);
    engine.brake(2, active);
};

//
// Pad mode selection
//

PioneerDJMS7.padModeButton = function(_channel, control, value, _status, group) {
    if (value === 0) {
        return;
    }
    const mode = PioneerDJMS7.modeForNote[control];
    if (mode === undefined) {
        return;
    }
    PioneerDJMS7.padMode[group] = mode;
    PioneerDJMS7.updateModeLeds(group);
    PioneerDJMS7.updatePadLeds(group);
};

PioneerDJMS7.updateModeLeds = function(group) {
    const deckStatus = PioneerDJMS7.deckStatus[group];
    const activeMode = PioneerDJMS7.padMode[group];

    Object.keys(PioneerDJMS7.modeForNote).forEach(function(note) {
        const mode = PioneerDJMS7.modeForNote[note];
        const color = PioneerDJMS7.modeButtonColor[mode];
        const velocity = mode === activeMode ? color.bright : color.dim;
        midi.sendShortMsg(deckStatus, parseInt(note, 10), velocity);
    });
};

//
// Performance pads
//

PioneerDJMS7.padPressed = function(_channel, control, value, _status, group) {
    const shifted = control >= 0x08;
    const padIndex = shifted ? control - 0x08 : control; // 0..7
    const mode = PioneerDJMS7.padMode[group];

    if (mode === "hotcue") {
        PioneerDJMS7.hotcuePad(group, padIndex, shifted, value);
    } else if (mode === "roll") {
        PioneerDJMS7.rollPad(group, padIndex, value);
    } else if (mode === "savedloop") {
        PioneerDJMS7.savedLoopPad(group, padIndex, shifted, value);
    } else if (mode === "sampler") {
        PioneerDJMS7.samplerPad(group, padIndex, shifted, value);
    }
};

PioneerDJMS7.hotcuePad = function(group, padIndex, shifted, value) {
    const cueKey = shifted ? "hotcue_" + (padIndex + 1) + "_clear"
        : "hotcue_" + (padIndex + 1) + "_activate";
    engine.setValue(group, cueKey, value > 0 ? 1 : 0);
};

PioneerDJMS7.rollPad = function(group, padIndex, value) {
    // Loop roll is momentary: active while the pad is held
    const size = PioneerDJMS7.rollSizes[padIndex];
    engine.setValue(group, "beatlooproll_" + size + "_activate", value > 0 ? 1 : 0);
};

PioneerDJMS7.savedLoopPad = function(group, padIndex, shifted, value) {
    if (value === 0) {
        return;
    }
    if (shifted) {
        engine.setValue(group, "reloop_toggle", 1);
        return;
    }
    // _toggle sets the loop on first press and removes it on the next press of
    // the same pad, so the pad both activates and deactivates the loop.
    engine.setValue(group, "beatloop_" + PioneerDJMS7.loopSizes[padIndex] + "_toggle", 1);
};

PioneerDJMS7.samplerPad = function(group, padIndex, shifted, value) {
    if (value === 0) {
        return;
    }
    const deckOffset = group === "[Channel1]" ? 0 : 8;
    const samplerGroup = "[Sampler" + (deckOffset + padIndex + 1) + "]";

    if (shifted) {
        if (engine.getValue(samplerGroup, "play")) {
            engine.setValue(samplerGroup, "cue_gotoandstop", 1);
        } else if (engine.getValue(samplerGroup, "track_loaded")) {
            engine.setValue(samplerGroup, "eject", 1);
        }
    } else if (engine.getValue(samplerGroup, "track_loaded")) {
        engine.setValue(samplerGroup, "cue_gotoandplay", 1);
    } else {
        engine.setValue(samplerGroup, "LoadSelectedTrack", 1);
    }
};

//
// Pad LED feedback
//

PioneerDJMS7.hotcueStatusChanged = function(_value, group, _control) {
    if (PioneerDJMS7.padMode[group] === "hotcue") {
        PioneerDJMS7.updatePadLeds(group);
    }
};

PioneerDJMS7.updatePadLeds = function(group) {
    const padStatus = PioneerDJMS7.padStatus[group];
    const mode = PioneerDJMS7.padMode[group];

    for (let i = 0; i < 8; i++) {
        const velocity = mode === "hotcue"
            ? PioneerDJMS7.hotcueColorIndex(group, i)
            : PioneerDJMS7.modeColor[mode];
        // The pad and its shifted layer (note + 0x08) share the colour.
        midi.sendShortMsg(padStatus, i, velocity);
        midi.sendShortMsg(padStatus, i + 0x08, velocity);
    }
};

PioneerDJMS7.hotcueColorIndex = function(group, padIndex) {
    if (engine.getValue(group, "hotcue_" + (padIndex + 1) + "_status") <= 0) {
        return PioneerDJMS7.emptyPadColor;
    }
    if (PioneerDJMS7.colorMapper === null) {
        return 0x2A; // fallback if ColorMapper is unavailable
    }
    const color = engine.getValue(group, "hotcue_" + (padIndex + 1) + "_color");
    return PioneerDJMS7.colorMapper.getValueForNearestColor(color);
};
