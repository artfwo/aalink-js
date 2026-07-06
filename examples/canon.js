'use strict';

const midi = require('midi');
const { Link } = require('aalink');

const output = new midi.Output();
output.openVirtualPort('canon');

const noteon = (note, vel) => output.sendMessage([0x90, note, vel]);
const noteoff = (note) => output.sendMessage([0x80, note, 0]);
const allOff = () => output.sendMessage([0xb0, 123, 0]);

async function voice(link, n) {
    const melody = [
        [48, 2],
        [50, 1],
        [52, 1],
        [55, 1],
        [57, 1],
        [55, 1],
        [52, 1],
    ];

    await link.sync(1);

    while (true) {
        for (const [pitch, dur] of melody) {
            const note = pitch + 12 * n;
            noteon(note, 127);

            for (let i = 0; i < dur; i++) {
                await link.sync(1 / 2 ** n);
            }

            noteoff(note);
        }
    }
}

const link = new Link(120);
link.enabled = true;

console.log("Connect a synth to the 'canon' MIDI port, press Ctrl+C to stop");
process.on('SIGINT', allOff);

for (let n = 0; n < 3; n++) {
    voice(link, n);
}
