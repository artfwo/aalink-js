'use strict';

const { Link } = require('..');

async function main() {
    const link = new Link(120);
    link.enabled = true;

    while (true) {
        await link.sync(1);
        console.log('bang!');
    }
}

main();
