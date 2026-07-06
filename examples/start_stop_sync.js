// This example demonstrates resetting local time
// when the transport start/stop state of Link changes.
//
// Try running the example with another peer, e.g. LinkHut,
// make sure that start/stop syncing is enabled and try
// starting and stopping the transport.

'use strict';

const { Link } = require('aalink');

async function main() {
    const link = new Link(120);
    link.enabled = true;
    link.startStopSyncEnabled = true;
    link.quantum = 4;

    link.on('playing', (playing) => {
        if (playing) {
            link.requestBeatAtStartPlayingTime(0);
        }
    });

    while (true) {
        const b = await link.sync(1);
        if (link.playing) {
            console.log('bang!', b);
        }
    }
}

main();
