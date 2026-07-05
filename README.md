# aalink-js

aalink-js is a JavaScript port of [aalink](https://github.com/artfwo/aalink),
async interface for Ableton Link, built for interactive multimedia
applications running in Node.js.

It provides a simple programming interface for writing concurrent JavaScript
code synchronized to a beat. The beat can optionally be time-aligned with
other peers in an Ableton Link session.

## Usage

```js
const { Link } = require('aalink');

async function main() {
    const link = new Link(120);
    link.enabled = true;

    for (let i = 0; i < 8; i++) {
        const beat = await link.sync(1);
        console.log('bang!');
    }
}

main();
```

`link.sync(n)` returns a `Promise` scheduled to resolve when Link time
reaches the next n-th beat on the timeline.

In the above example, awaiting `link.sync(1)` will pause and resume the
`main` function at beats 1, 2, 3, and so on.

Keep in mind that awaiting `sync(n)` does not cause a function to sleep
for the given number of beats. Regardless of the moment when the function is
suspended, it will resume when the next closest n-th beat is reached on the
shared Link timeline, e.g. awaiting for `sync(2)` at beat 11.5 will resume
at beat 12.

Non-integral beat syncing is supported. For example:

```js
await link.sync(1/2); // resumes at beats 0.5, 1, 1.5...
await link.sync(3/2); // resumes at beats 1.5, 3, 4.5...
```

Sync events can be scheduled with an offset (also expressed in beats) by
passing a second argument to `sync()`. Offset can be either positive
or negative. Use this to add groove to the function rhythm.

```js
async function arpeggiate() {
    for (let i = 0; i < 16; i++) {
        const swing = i % 2 === 1 ? 0.25 : 0;

        await link.sync(1/2, swing);
        console.log('###', i);

        await link.sync(1/2, 0);
        console.log('@@@', i);
    }
}
```

Combine synced functions to run in series or concurrently:

```js
const { Link } = require('aalink');

async function main() {
    const link = new Link(120);
    link.enabled = true;

    async function sequence(name) {
        for (let i = 0; i < 4; i++) {
            await link.sync(1);
            console.log('bang!', name);
        }
    }

    await sequence('a');
    await sequence('b');

    await Promise.all([sequence('c'), sequence('d')]);
}

main();
```

### Limitations

aalink-js aims to be punctual, but it is not 100% accurate due to the
processing delay in the internal scheduler and the uncertainty of event
loop iterations timing.

For convenience, the numerical values of promises returned from `sync()`
aren't equal to the exact beat time from the moment the promises are
resolved. They correspond to the previously estimated resume times instead.

```js
let b;
b = await link.sync(1); // b will be 1.0, returned at beat 1.00190
b = await link.sync(1); // b will be 2.0, returned at beat 2.00027
b = await link.sync(1); // b will be 3.0, returned at beat 3.00005
```

## License

Copyright (c) 2026 Artem Popov <art@artfwo.net>

aalink-js is licensed under the GNU General Public License (GPL) version 3.
You can find the full text of the GPL license in the `LICENSE` file included
in this repository.

aalink-js includes code from node-addon-api and Ableton Link.

[node-addon-api](https://github.com/nodejs/node-addon-api)

Copyright (c) 2017 Node.js API collaborators

[node-addon-api license](https://github.com/nodejs/node-addon-api/blob/main/LICENSE.md)

[Ableton Link](https://ableton.github.io/link/)

Copyright 2016, Ableton AG, Berlin. All rights reserved.

[Ableton Link license](https://github.com/Ableton/link/blob/master/LICENSE.md)
