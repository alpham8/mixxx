// Pioneer-PLX-CRSS12-script.js
// ****************************************************************************
// * Mixxx mapping for the Pioneer DJ PLX-CRSS12 turntable.
// *
// * Verified against a controller-debug capture:
// *   - One turntable sends on the DECK1 channels (notes 0x90, pads 0x97),
// *     the other on the DECK2 channels (notes 0x91, pads 0x98) - exactly like
// *     the DJM-S7. Both turntables load THIS mapping; the deck is taken from
// *     the MIDI channel, so deck 1 and deck 2 work without any per-unit edit.
// *   - The unit has TWO physical pad-mode buttons (HOT CUE, SAMPLER); each one
// *     toggles between two modes (press once / press twice), per the manual:
// *       HOT CUE  button: press = Hot Cue,  press twice = Roll
// *       SAMPLER  button: press = Sampler,  press twice = Saved Loop
// *     The firmware reports the resulting active mode via four deck-channel
// *     notes: 0x30 roll, 0x31 hotcue, 0x32 savedloop, 0x33 sampler.
// *   - The firmware changes the pad note range per mode: most modes send pads
// *     on 0x00-0x07, the sampler-style mode sends them on 0x10-0x17. We mask
// *     the low 3 bits for the pad index and use the (host-tracked) mode for the
// *     action, so both ranges work.
// *
// * The PLATTER is a DVS timecode source - configure it under
// * Preferences > Vinyl Control, NOT here. This mapping only covers the pads.
// *
// * Confirmed from a Serato USB capture: 0x31 = Hot Cue (pads followed on
// * 0x00-0x07) and 0x33 = Sampler (pads on 0x10-0x17). The Roll (0x30) vs Saved
// * Loop (0x32) note split is inferred from the two-button layout; if Roll and
// * Saved Loop come out swapped on the unit, exchange them in modeForNote.
// ****************************************************************************

var PioneerPLXCRSS12 = {};

// Deck note status (mode buttons) and pad status, per Mixxx deck
PioneerPLXCRSS12.deckStatus = {
    "[Channel1]": 0x90,
    "[Channel2]": 0x91,
};
PioneerPLXCRSS12.padStatus = {
    "[Channel1]": 0x97,
    "[Channel2]": 0x98,
};

// Pad mode buttons (deck channel notes). HOT CUE button toggles hotcue/roll,
// SAMPLER button toggles sampler/savedloop; the firmware reports the active
// mode with one of these four notes.
PioneerPLXCRSS12.modeForNote = {
    0x30: "roll",
    0x31: "hotcue",
    0x32: "savedloop",
    0x33: "sampler",
};
PioneerPLXCRSS12.modeNoteForName = {
    roll: 0x30,
    hotcue: 0x31,
    savedloop: 0x32,
    sampler: 0x33,
};

// Beat sizes per pad (pad 1..8) for the ROLL mode
PioneerPLXCRSS12.rollSizes = [0.0625, 0.125, 0.25, 0.5, 1, 2, 4, 8];

// Active pad mode per deck
PioneerPLXCRSS12.padMode = {
    "[Channel1]": "hotcue",
    "[Channel2]": "hotcue",
};

// 14-bit tempo/pitch fader state per deck. Centre (0 % adjustment) is MSB 0x40,
// i.e. a combined value of 8192; top (+8 %) is 16383, bottom (-8 %) is 0.
PioneerPLXCRSS12.tempoFaderCenter = 8192;
PioneerPLXCRSS12.tempo = {
    "[Channel1]": {msb: 0x40, lsb: 0x00},
    "[Channel2]": {msb: 0x40, lsb: 0x00},
};

// Startup handshake, reverse-engineered from a Serato USB capture (Pioneer
// prefix 00 40 05). Without it the pads send no MIDI and pad colours do not
// persist. Each turntable is its own USB device and gets this handshake from
// its own mapping instance.
PioneerPLXCRSS12.enableSysexHex = [
    "F0 00 40 05 00 00 04 04 00 03 01 F7",
    "F0 00 40 05 00 00 04 04 00 50 31 F7",
    "F0 00 40 05 00 00 04 04 00 23 00 01 F7",
    "F0 00 40 05 00 00 04 04 00 22 00 01 F7",
    "F0 00 40 05 00 00 04 04 00 21 00 01 F7",
    "F0 00 40 05 00 00 04 04 00 12 0F 0F 0F 0F 0F 0F F7",
    "F0 00 40 05 00 00 04 04 00 11 0F 0F 0F 0F 0F 0F F7",
    "F0 00 40 05 00 00 04 04 00 13 0F 0F 0F 0F 0F 0F F7",
    "F0 00 40 05 00 00 04 04 00 14 0F 0F 0F 0F 0F 0F F7",
    "F0 00 40 05 00 00 04 04 00 24 00 01 F7",
];
PioneerPLXCRSS12.heartbeatSysex = [0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x04, 0x04, 0x00, 0x50, 0x31, 0xF7];
// Serato's native driver sends this every ~260 ms and the unit then holds pad
// colours by itself (a capture proved colours are sent once, never streamed).
//
// IMPORTANT: 250 ms only works on a Mixxx build with the asynchronous PortMidi
// output queue (dedicated output thread + pmHostError retry). On stock Mixxx the
// blocking 12-byte sysex send floods "Host error" and degrades pad input - there,
// raise this to 1000 (pads work but colours fade).
PioneerPLXCRSS12.heartbeatIntervalMs = 250;
PioneerPLXCRSS12.heartbeatTimer = 0;

// Same Pioneer pad colour palette as the DJM-S7 (RGB -> palette index).
PioneerPLXCRSS12.padColorPalette = {
    0x0F88CA: 0x01, // blue
    0x1DBEBD: 0x0C, // cyan
    0x4EB648: 0x15, // green
    0xFAC313: 0x1D, // yellow
    0xF8821A: 0x24, // orange
    0xC02626: 0x2A, // red
    0xCE359E: 0x37, // magenta
    0x6823B6: 0x3A, // violet
};
PioneerPLXCRSS12.colorMapper = null;
PioneerPLXCRSS12.emptyPadColor = 0x0A;

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------

PioneerPLXCRSS12.init = function() {
    // Run the handshake first (enables pads + persistent LED mode).
    PioneerPLXCRSS12.enableSysexHex.forEach(function(hex) {
        const msg = hex.split(" ").map(function(h) {
            return parseInt(h, 16);
        });
        midi.sendSysexMsg(msg, msg.length);
    });
    PioneerPLXCRSS12.heartbeatTimer = engine.beginTimer(PioneerPLXCRSS12.heartbeatIntervalMs, function() {
        midi.sendSysexMsg(PioneerPLXCRSS12.heartbeatSysex, PioneerPLXCRSS12.heartbeatSysex.length);
    });

    PioneerPLXCRSS12.colorMapper = new ColorMapper(PioneerPLXCRSS12.padColorPalette);

    const samplerCount = 16;
    if (engine.getValue("[App]", "num_samplers") < samplerCount) {
        engine.setValue("[App]", "num_samplers", samplerCount);
    }

    Object.keys(PioneerPLXCRSS12.padStatus).forEach(function(group) {
        for (let i = 1; i <= 8; i++) {
            engine.makeConnection(group, "hotcue_" + i + "_status", PioneerPLXCRSS12.hotcueStatusChanged);
            engine.makeConnection(group, "hotcue_" + i + "_color", PioneerPLXCRSS12.hotcueStatusChanged);
        }
        PioneerPLXCRSS12.updateModeLeds(group);
        PioneerPLXCRSS12.updatePadLeds(group);
    });
};

PioneerPLXCRSS12.shutdown = function() {
    if (PioneerPLXCRSS12.heartbeatTimer) {
        engine.stopTimer(PioneerPLXCRSS12.heartbeatTimer);
        PioneerPLXCRSS12.heartbeatTimer = 0;
    }
    Object.keys(PioneerPLXCRSS12.padStatus).forEach(function(group) {
        const padStatus = PioneerPLXCRSS12.padStatus[group];
        const deckStatus = PioneerPLXCRSS12.deckStatus[group];
        for (let note = 0x00; note <= 0x1F; note++) {
            midi.sendShortMsg(padStatus, note, 0x00);
        }
        Object.keys(PioneerPLXCRSS12.modeForNote).forEach(function(note) {
            midi.sendShortMsg(deckStatus, parseInt(note, 10), 0x00);
        });
    });
};

// ---------------------------------------------------------------------------
// Pad mode selection
// ---------------------------------------------------------------------------

PioneerPLXCRSS12.padModeButton = function(_channel, control, value, _status, group) {
    if (value === 0) {
        return;
    }
    const mode = PioneerPLXCRSS12.modeForNote[control];
    if (mode === undefined) {
        return;
    }
    PioneerPLXCRSS12.padMode[group] = mode;
    PioneerPLXCRSS12.updateModeLeds(group);
    PioneerPLXCRSS12.updatePadLeds(group);
};

PioneerPLXCRSS12.updateModeLeds = function(group) {
    const deckStatus = PioneerPLXCRSS12.deckStatus[group];
    const activeNote = PioneerPLXCRSS12.modeNoteForName[PioneerPLXCRSS12.padMode[group]];
    Object.keys(PioneerPLXCRSS12.modeForNote).forEach(function(note) {
        const noteNumber = parseInt(note, 10);
        midi.sendShortMsg(deckStatus, noteNumber, noteNumber === activeNote ? 0x7F : 0x00);
    });
};

// ---------------------------------------------------------------------------
// Performance pads
// ---------------------------------------------------------------------------

PioneerPLXCRSS12.padPressed = function(_channel, control, value, _status, group) {
    // Pads arrive on 0x00-0x07 (hotcue/roll modes) or 0x10-0x17 (sampler mode);
    // mask the low 3 bits for the pad index, bit 3 marks a shifted pad.
    const shifted = (control & 0x08) !== 0;
    const padIndex = control & 0x07; // 0..7
    const mode = PioneerPLXCRSS12.padMode[group];

    if (mode === "hotcue") {
        const cueKey = shifted ? "hotcue_" + (padIndex + 1) + "_clear"
            : "hotcue_" + (padIndex + 1) + "_activate";
        engine.setValue(group, cueKey, value > 0 ? 1 : 0);
    } else if (mode === "roll") {
        engine.setValue(group, "beatlooproll_" + PioneerPLXCRSS12.rollSizes[padIndex] + "_activate", value > 0 ? 1 : 0);
    } else if (mode === "savedloop") {
        if (value > 0) {
            engine.setValue(group, "beatloop_" + PioneerPLXCRSS12.rollSizes[padIndex] + "_toggle", 1);
        }
    } else if (mode === "sampler") {
        PioneerPLXCRSS12.samplerPad(group, padIndex, shifted, value);
    }
};

PioneerPLXCRSS12.samplerPad = function(group, padIndex, shifted, value) {
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

// ---------------------------------------------------------------------------
// Tempo / pitch fader (14-bit absolute). Maps the hardware position directly to
// the deck rate so the on-screen pitch matches the fader 1:1. Note: with vinyl
// control active in ABSOLUTE mode the timecode owns the rate and this fader is
// ignored; in RELATIVE/internal mode it acts as a pitch offset.
// ---------------------------------------------------------------------------

PioneerPLXCRSS12.tempoFaderMsb = function(_channel, _control, value, _status, group) {
    PioneerPLXCRSS12.tempo[group].msb = value;
    PioneerPLXCRSS12.updateTempo(group);
};

PioneerPLXCRSS12.tempoFaderLsb = function(_channel, _control, value, _status, group) {
    PioneerPLXCRSS12.tempo[group].lsb = value;
    PioneerPLXCRSS12.updateTempo(group);
};

PioneerPLXCRSS12.updateTempo = function(group) {
    const t = PioneerPLXCRSS12.tempo[group];
    const full = (t.msb << 7) | t.lsb; // 0..16383
    let rate = (full - PioneerPLXCRSS12.tempoFaderCenter) / PioneerPLXCRSS12.tempoFaderCenter;
    if (rate > 1) {
        rate = 1;
    } else if (rate < -1) {
        rate = -1;
    }
    engine.setValue(group, "rate", rate);
};

// ---------------------------------------------------------------------------
// Pad LED feedback (speculative - the unit may drive its RGB pad LEDs in
// firmware and ignore MIDI output; the colour protocol is undocumented)
// ---------------------------------------------------------------------------

PioneerPLXCRSS12.hotcueStatusChanged = function(_value, group, _control) {
    if (PioneerPLXCRSS12.padMode[group] === "hotcue") {
        PioneerPLXCRSS12.updatePadLeds(group);
    }
};

PioneerPLXCRSS12.updatePadLeds = function(group) {
    const padStatus = PioneerPLXCRSS12.padStatus[group];
    const mode = PioneerPLXCRSS12.padMode[group];
    for (let i = 0; i < 8; i++) {
        const velocity = mode === "hotcue"
            ? PioneerPLXCRSS12.hotcueColorIndex(group, i)
            : PioneerPLXCRSS12.emptyPadColor;
        midi.sendShortMsg(padStatus, i, velocity);
    }
};

PioneerPLXCRSS12.hotcueColorIndex = function(group, padIndex) {
    if (engine.getValue(group, "hotcue_" + (padIndex + 1) + "_status") <= 0) {
        return PioneerPLXCRSS12.emptyPadColor;
    }
    if (PioneerPLXCRSS12.colorMapper === null) {
        return 0x2A;
    }
    const color = engine.getValue(group, "hotcue_" + (padIndex + 1) + "_color");
    return PioneerPLXCRSS12.colorMapper.getValueForNearestColor(color);
};
