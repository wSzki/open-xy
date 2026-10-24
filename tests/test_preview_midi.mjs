import test from 'node:test';
import assert from 'node:assert/strict';
import {createMidiInput} from '../harness/midi-input.mjs';

class MidiPort extends EventTarget {
  constructor(id, name = id) {
    super();
    this.id = id;
    this.name = name;
    this.state = 'connected';
    this.connection = 'closed';
    this.openCalls = 0;
    this.closeCalls = 0;
    this.openBehavior = null;
    this.closeBehavior = null;
  }

  async open() {
    this.openCalls++;
    if (this.openBehavior) await this.openBehavior();
    this.connection = 'open';
  }

  async close() {
    this.closeCalls++;
    if (this.closeBehavior) await this.closeBehavior();
    this.connection = 'closed';
  }

  send(bytes) {
    const event = new Event('midimessage');
    Object.defineProperty(event, 'data', {value:Uint8Array.from(bytes)});
    this.dispatchEvent(event);
  }
  sendWhileOpen(bytes) {
    if (this.connection === 'open') this.send(bytes);
  }
}

class MidiAccess extends EventTarget {
  constructor(...ports) {
    super();
    this.inputs = new Map(ports.map(port => [port.id, port]));
    this.statechangeListeners = new Set();
  }

  addEventListener(type, listener, options) {
    if (type === 'statechange') this.statechangeListeners.add(listener);
    return super.addEventListener(type, listener, options);
  }

  removeEventListener(type, listener, options) {
    if (type === 'statechange') this.statechangeListeners.delete(listener);
    return super.removeEventListener(type, listener, options);
  }

  get outputs() {
    throw new Error('MIDI outputs must not be accessed.');
  }
}

function fixture({access, requestAccess, onStateChange, canPlay = () => true} = {}) {
  const voices = new Map();
  let accepted = 0;
  let released = 0;
  const requests = [];
  const defaultAccess = access ?? new MidiAccess();
  const noteOn = (source, note, velocity) => {
    if (!canPlay() || voices.has(source)) return false;
    voices.set(source, {note, velocity});
    accepted++;
    return true;
  };
  const noteOff = source => {
    if (voices.delete(source)) released++;
  };
  const controller = createMidiInput({
    requestAccess:options => {
      requests.push(options);
      return requestAccess ? requestAccess(options) : Promise.resolve(defaultAccess);
    },
    noteOn,
    noteOff,
    onStateChange,
  });
  return {
    controller,
    voices,
    requests,
    noteOn,
    noteOff,
    counts:() => ({accepted, released}),
  };
}

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((res, rej) => { resolve = res; reject = rej; });
  return {promise, resolve, reject};
}

const flush = () => new Promise(resolve => setImmediate(resolve));


test('explicit connection exposes inputs without opening one and routes note velocity and release', async () => {
  const input = new MidiPort('keyboard-1', 'Keyboard');
  const access = new MidiAccess(input);
  const f = fixture({access});
  assert.equal(f.requests.length, 0);
  assert.equal(input.openCalls, 0);

  assert.equal(await f.controller.connect(), true);
  assert.deepEqual(f.requests, [{sysex:false}]);
  assert.equal(f.controller.getState().selectedId, '');
  assert.equal(input.openCalls, 0);

  assert.equal(await f.controller.selectInput(f.controller.getState().inputs[0].id), true);
  input.send([0x9f, 60, 64]);
  assert.deepEqual([...f.voices.values()], [{note:60, velocity:64 / 127}]);

  input.send([0x9f, 60, 100]);
  assert.equal(f.voices.size, 1);
  assert.equal(f.counts().accepted, 1);

  input.send([0x9f, 60, 0]);
  assert.equal(f.voices.size, 0);
  assert.equal(f.counts().released, 1);
  input.send([0x9f, 62, 80]);
  assert.equal(f.voices.size, 1);
  f.controller.releaseNotes();
  assert.equal(f.voices.size, 0);
  input.send([0x9f, 63, 80]);
  assert.equal(f.voices.size, 1);
  input.send([0x8f, 63, 0]);
  assert.equal(f.voices.size, 0);


  input.send([0x9f, 61]);
  input.send([0xbf, 61, 100]);
  assert.equal(f.voices.size, 0);
  input.send([0x9f, 61, 127]);
  input.send([0x8f, 61, 0]);
  assert.equal(f.voices.size, 0);
});
test('rejected note-on is not owned, and only an accepted strike is released', async () => {
  const input = new MidiPort('gated');
  let canPlay = false;
  const f = fixture({access:new MidiAccess(input), canPlay:() => canPlay});
  assert.equal(await f.controller.connect(), true);
  assert.equal(await f.controller.selectInput(input.id), true);

  input.send([0x90, 55, 90]);
  assert.equal(f.voices.size, 0);
  canPlay = true;
  input.send([0x90, 55, 90]);
  assert.equal(f.voices.size, 1);
  input.send([0x80, 55, 0]);
  assert.equal(f.voices.size, 0);
  assert.equal(f.counts().accepted, 1);
  assert.equal(f.counts().released, 1);
});




test('MIDI channels and keyboard ownership release independently', async () => {
  const input = new MidiPort('shared-input');
  const f = fixture({access:new MidiAccess(input)});
  assert.equal(await f.controller.connect(), true);
  assert.equal(await f.controller.selectInput(input.id), true);

  const keyboardSource = 'keyboard:60';
  assert.equal(f.noteOn(keyboardSource, 60, 0.7), true);
  input.send([0x90, 60, 64]);
  input.send([0x91, 60, 96]);
  input.send([0x90, 60, 120]);
  assert.equal(f.voices.size, 3);
  assert.deepEqual([...f.voices.values()].map(voice => voice.note), [60, 60, 60]);
  assert.ok([...f.voices.values()].some(voice => voice.velocity === 64 / 127));
  assert.ok([...f.voices.values()].some(voice => voice.velocity === 96 / 127));

  input.send([0x80, 60, 0]);
  assert.equal(f.voices.size, 2);
  f.noteOff(keyboardSource);
  input.send([0x91, 60, 0]);
  assert.equal(f.voices.size, 0);
  for (let channel = 0; channel < 16; channel++) {
    input.send([0x90 | channel, 40 + channel, 1 + channel]);
  }
  assert.equal(f.voices.size, 16);
  for (let channel = 0; channel < 16; channel++) {
    input.send([0x80 | channel, 40 + channel, 0]);
  }
  assert.equal(f.voices.size, 0);


});


test('switching and disconnecting release held notes and detach the previous input', async () => {
  const first = new MidiPort('first');
  const second = new MidiPort('second');
  const f = fixture({access:new MidiAccess(first, second)});
  assert.equal(await f.controller.connect(), true);
  assert.equal(await f.controller.selectInput(first.id), true);
  first.send([0x90, 48, 100]);
  assert.equal(f.voices.size, 1);

  assert.equal(await f.controller.selectInput(second.id), true);
  assert.equal(f.voices.size, 0);
  assert.equal(first.closeCalls, 1);
  first.send([0x90, 49, 100]);
  assert.equal(f.voices.size, 0);

  second.send([0x90, 50, 100]);
  assert.equal(f.voices.size, 1);
  assert.equal(await f.controller.disconnect(), true);
  assert.equal(f.voices.size, 0);
  assert.equal(second.closeCalls, 1);
  second.send([0x90, 51, 100]);
  assert.equal(f.voices.size, 0);
  assert.equal(f.controller.getState().enabled, false);
});


test('hot-unplug releases the selected input without falling back to another port', async () => {
  const selected = new MidiPort('selected');
  const available = new MidiPort('available');
  const access = new MidiAccess(selected, available);
  const f = fixture({access});
  assert.equal(await f.controller.connect(), true);
  assert.equal(await f.controller.selectInput(selected.id), true);
  selected.send([0x90, 64, 90]);
  assert.equal(f.voices.size, 1);

  access.inputs.delete(selected.id);
  selected.state = 'disconnected';
  access.dispatchEvent(new Event('statechange'));
  assert.equal(f.voices.size, 0);
  assert.equal(f.controller.getState().selectedId, '');
  assert.deepEqual(f.controller.getState().inputs, [{id:'available', name:'available'}]);
  assert.equal(available.openCalls, 0);
  selected.send([0x90, 65, 90]);
  assert.equal(f.voices.size, 0);
  await flush();
  assert.equal(selected.closeCalls, 1);
});


test('disconnect invalidates pending permission and port-open results', async () => {
  const permission = deferred();
  const lateInput = new MidiPort('late');
  const lateAccess = new MidiAccess(lateInput);
  const permissionFixture = fixture({requestAccess:() => permission.promise});
  const connecting = permissionFixture.controller.connect();
  await flush();
  assert.equal(permissionFixture.requests.length, 1);
  assert.equal(await permissionFixture.controller.disconnect(), true);
  permission.resolve(lateAccess);
  assert.equal(await connecting, false);
  assert.equal(permissionFixture.controller.getState().enabled, false);
  assert.equal(lateInput.openCalls, 0);

  const opening = deferred();
  const input = new MidiPort('opening');
  input.openBehavior = () => opening.promise;
  const f = fixture({access:new MidiAccess(input)});
  assert.equal(await f.controller.connect(), true);
  const selecting = f.controller.selectInput(input.id);
  await flush();
  assert.equal(input.openCalls, 1);
  input.send([0x90, 70, 100]);
  assert.equal(f.voices.size, 0);
  assert.equal(await f.controller.disconnect(), true);
  opening.resolve();
  assert.equal(await selecting, false);
  assert.equal(f.controller.getState().selectedId, '');
  assert.equal(f.controller.getState().enabled, false);
  assert.equal(input.closeCalls, 1);
  input.send([0x90, 71, 100]);
  assert.equal(f.voices.size, 0);
});


test('late open completion cannot close a newly selected instance of the same input', async () => {
  for (const outcome of ['resolve', 'reject']) {
    const firstOpen = deferred();
    const input = new MidiPort(`same-port-${outcome}`);
    let attempts = 0;
    input.openBehavior = () => ++attempts === 1 ? firstOpen.promise : undefined;
    const f = fixture({access:new MidiAccess(input)});

    assert.equal(await f.controller.connect(), true);
    const staleSelection = f.controller.selectInput(input.id);
    await flush();
    assert.equal(input.openCalls, 1);
    assert.equal(await f.controller.disconnect(), true);

    assert.equal(await f.controller.connect(), true);
    assert.equal(await f.controller.selectInput(input.id), true);
    assert.equal(input.connection, 'open');

    if (outcome === 'resolve') firstOpen.resolve();
    else firstOpen.reject(new Error('stale open rejected'));
    assert.equal(await staleSelection, false);
    assert.equal(f.controller.getState().selectedId, input.id);
    assert.equal(f.controller.getState().error, '');
    assert.equal(input.connection, 'open');

    input.sendWhileOpen([0x90, 72, 100]);
    assert.equal(f.voices.size, 1);
  }
});


test('a failed close after stale open rejection remains visible', async () => {
  const opening = deferred();
  const input = new MidiPort('late-open');
  input.openBehavior = () => opening.promise;
  input.closeBehavior = () => { throw new Error('late cleanup close failed'); };
  const f = fixture({access:new MidiAccess(input)});

  assert.equal(await f.controller.connect(), true);
  const selecting = f.controller.selectInput(input.id);
  await flush();
  assert.equal(await f.controller.disconnect(), true);

  opening.reject(new Error('stale open failed'));
  assert.equal(await selecting, false);
  assert.equal(f.controller.getState().enabled, false);
  assert.equal(f.controller.getState().selectedId, '');
  assert.match(f.controller.getState().error, /late cleanup close failed/);
  assert.doesNotMatch(f.controller.getState().error, /stale open failed/);
});


test('state observer failures do not become MIDI access errors or leak access listeners', async () => {
  const access = new MidiAccess(new MidiPort('observer-test'));
  const observerError = new Error('state observer failed');
  let threw = false;
  const f = fixture({
    access,
    onStateChange:state => {
      if (state.enabled && !threw) {
        threw = true;
        throw observerError;
      }
    },
  });

  await assert.rejects(f.controller.connect(), error => error === observerError);
  assert.equal(f.controller.getState().enabled, true);
  assert.deepEqual(f.controller.getState().inputs, [{id:'observer-test', name:'observer-test'}]);
  assert.equal(f.controller.getState().error, '');
  assert.equal(access.statechangeListeners.size, 1);

  assert.equal(await f.controller.disconnect(), true);
  assert.equal(access.statechangeListeners.size, 0);
});


test('permission, open, and close errors stay visible and explicit actions can recover', async () => {
  const input = new MidiPort('recoverable');
  const access = new MidiAccess(input);
  let permissionAttempts = 0;
  const f = fixture({access, requestAccess:() => {
    if (++permissionAttempts === 1) throw new Error('Permission denied');
    return access;
  }});

  assert.equal(await f.controller.connect(), false);
  assert.match(f.controller.getState().error, /Permission denied/);
  assert.equal(await f.controller.connect(), true);

  let openAttempts = 0;
  input.openBehavior = () => {
    if (++openAttempts === 1) throw new Error('Port open failed');
  };
  assert.equal(await f.controller.selectInput(input.id), false);
  assert.match(f.controller.getState().error, /Port open failed/);
  assert.equal(await f.controller.selectInput(input.id), true);

  let closeAttempts = 0;
  input.closeBehavior = () => {
    if (++closeAttempts === 1) throw new Error('Port close failed');
  };
  assert.equal(await f.controller.disconnect(), false);
  assert.match(f.controller.getState().error, /Port close failed/);
  assert.equal(await f.controller.disconnect(), true);
  assert.equal(f.controller.getState().error, '');
  assert.equal(closeAttempts, 2);
});
