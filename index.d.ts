import { EventEmitter } from "node:events";

export class Link extends EventEmitter {
    constructor(bpm: number);

    on(event: "tempo", listener: (tempo: number) => void): this;
    on(event: "peers", listener: (numPeers: number) => void): this;
    on(event: "playing", listener: (playing: boolean) => void): this;

    once(event: "tempo", listener: (tempo: number) => void): this;
    once(event: "peers", listener: (numPeers: number) => void): this;
    once(event: "playing", listener: (playing: boolean) => void): this;

    off(event: "tempo", listener: (tempo: number) => void): this;
    off(event: "peers", listener: (numPeers: number) => void): this;
    off(event: "playing", listener: (playing: boolean) => void): this;

    readonly numPeers: number;
    readonly beat: number;
    readonly phase: number;
    readonly time: number;

    quantum: number;
    enabled: boolean;
    startStopSyncEnabled: boolean;
    tempo: number;
    playing: boolean;

    requestBeat(beat: number): void;
    forceBeat(beat: number): void;
    requestBeatAtStartPlayingTime(beat: number): void;
    setIsPlayingAndRequestBeatAtTime(playing: boolean, time: number, beat: number): void;

    /**
     * Resolves at the next multiple of `beat` (with optional `offset`)
     * on the link timeline, with the link beat value.
     */
    sync(beat: number, offset?: number): Promise<number>;
}
