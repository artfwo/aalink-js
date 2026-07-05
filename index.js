'use strict';

const { EventEmitter } = require('node:events');
const binding = require('bindings')('aalink.node');

class Link extends binding.Link {
    constructor(bpm) {
        super(bpm);
        EventEmitter.call(this);

        this._setTempoCallback((tempo) => this.emit('tempo', tempo));
        this._setNumPeersCallback((numPeers) => this.emit('peers', numPeers));
        this._setStartStopCallback((playing) => this.emit('playing', playing));
    }
}

Object.assign(Link.prototype, EventEmitter.prototype);

module.exports = { Link };
