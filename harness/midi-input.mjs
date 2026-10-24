export function createMidiInput({requestAccess, noteOn, noteOff, onStateChange}) {
  if (typeof requestAccess !== 'function' || typeof noteOn !== 'function' ||
      typeof noteOff !== 'function') {
    throw new TypeError('MIDI input requires requestAccess, noteOn, and noteOff callbacks.');
  }

  const state = {enabled:false, busy:false, inputs:[], selectedId:'', error:''};
  const ownedSources = new Set();
  const pendingCloses = new Set();
  const closeInFlight = new Map();
  let access = null;
  let accessListener = null;
  let selected = null;
  let pendingSelection = null;
  let generation = 0;

  const snapshot = () => ({...state, inputs:state.inputs.map(input => ({...input}))});
  const emit = () => onStateChange?.(snapshot());
  const errorText = error => {
    if (typeof error?.message === 'string' && error.message) return error.message;
    if (typeof error === 'string' && error) return error;
    return 'MIDI input operation failed.';
  };
  const isConnected = port => port && port.state !== 'disconnected';
  const isAvailable = (owner, port) => owner?.inputs?.get(port.id) === port && isConnected(port);

  function listInputs(owner) {
    const inputs = [];
    for (const port of owner.inputs.values()) {
      if (!isConnected(port)) continue;
      const id = port.id;
      if (typeof id !== 'string') continue;
      inputs.push({id, name:typeof port.name === 'string' && port.name ? port.name : id});
    }
    return inputs;
  }

  function releaseNotes() {
    for (const source of ownedSources) {
      ownedSources.delete(source);
      noteOff(source);
    }
  }

  function detachSelected() {
    if (!selected) return null;
    const previous = selected;
    selected = null;
    previous.port.removeEventListener('midimessage', previous.handler);
    return previous.port;
  }

  function closePort(port) {
    const existing = closeInFlight.get(port);
    if (existing) return existing;

    pendingCloses.add(port);
    let resolveClose;
    const operation = new Promise(resolve => { resolveClose = resolve; });
    closeInFlight.set(port, operation);
    (async () => {
      try {
        await port.close();
        pendingCloses.delete(port);
        resolveClose(null);
      } catch (error) {
        pendingCloses.add(port);
        resolveClose(error);
      } finally {
        if (closeInFlight.get(port) === operation) closeInFlight.delete(port);
      }
    })();
    return operation;
  }

  async function closePendingPorts() {
    let firstError = null;
    for (const port of [...pendingCloses]) {
      const error = await closePort(port);
      if (error && !firstError) firstError = error;
    }
    return firstError;
  }
  async function closeStalePort(port) {
    if (selected?.port === port || pendingSelection?.port === port) return;

    const token = generation;
    const error = await closePort(port);
    if (error && token === generation) {
      state.error = errorText(error);
      emit();
    }
  }

  function handleMessage(inputId, event) {
    const data = event?.data;
    if (!data || data.length !== 3) return;

    const status = data[0];
    const note = data[1];
    const velocity = data[2];
    if (!Number.isInteger(status) || status < 0x80 || status > 0x9f ||
        !Number.isInteger(note) || note < 0 || note > 127 ||
        !Number.isInteger(velocity) || velocity < 0 || velocity > 127) return;

    const kind = status & 0xf0;
    if (kind !== 0x80 && kind !== 0x90) return;
    const channel = status & 0x0f;
    const source = `midi:${inputId}:${channel}:${note}`;
    if (kind === 0x80 || velocity === 0) {
      if (ownedSources.delete(source)) noteOff(source);
      return;
    }
    if (ownedSources.has(source)) return;
    if (noteOn(source, note, velocity / 127)) ownedSources.add(source);
  }

  function handleAccessStateChange(owner) {
    if (access !== owner) return;
    state.inputs = listInputs(owner);

    if (pendingSelection && !isAvailable(owner, pendingSelection.port)) {
      ++generation;
      pendingSelection = null;
      state.busy = false;
      state.error = 'The selected MIDI input disconnected before it could be opened.';
      emit();
      return;
    }

    if (selected && !isAvailable(owner, selected.port)) {
      const token = ++generation;
      releaseNotes();
      const lostPort = detachSelected();
      pendingCloses.add(lostPort);
      state.selectedId = '';
      state.busy = true;
      emit();
      void (async () => {
        const error = await closePort(lostPort);
        if (token !== generation || access !== owner) return;
        state.busy = false;
        if (error) state.error = errorText(error);
        emit();
      })();
      return;
    }

    emit();
  }

  async function connect() {
    if (state.enabled) return !state.busy;
    if (state.busy) return false;

    const token = ++generation;
    state.busy = true;
    state.error = '';
    emit();
    if (token !== generation) return false;


    if (pendingCloses.size) {
      const closeError = await closePendingPorts();
      if (token !== generation) return false;
      if (closeError) {
        state.busy = false;
        state.error = errorText(closeError);
        emit();
        return false;
      }
    }
    try {
      const nextAccess = await requestAccess({sysex:false});
      if (token !== generation) return false;
      if (!nextAccess?.inputs || typeof nextAccess.addEventListener !== 'function' ||
          typeof nextAccess.removeEventListener !== 'function') {
        throw new Error('The browser did not provide MIDI input access.');
      }
      const inputs = listInputs(nextAccess);
      const handler = () => handleAccessStateChange(nextAccess);
      nextAccess.addEventListener('statechange', handler);
      access = nextAccess;
      accessListener = {owner:nextAccess, handler};
      state.enabled = true;
      state.inputs = inputs;
      state.selectedId = '';
      state.busy = false;
      state.error = '';
    } catch (error) {
      if (token === generation) {
        access = null;
        accessListener = null;
        state.enabled = false;
        state.inputs = [];
        state.selectedId = '';
        state.busy = false;
        state.error = errorText(error);
        emit();
      }
      return false;
    }
    emit();
    return true;
  }

  async function selectInput(id) {
    if (state.busy || !access || typeof id !== 'string') return false;
    const owner = access;
    const port = owner.inputs.get(id);
    if (!isConnected(port) || port.id !== id) return false;
    if (selected?.port === port) return true;

    const token = ++generation;
    pendingSelection = {id, port, token};
    state.busy = true;
    state.error = '';
    releaseNotes();
    const previousPort = detachSelected();
    state.selectedId = '';
    if (previousPort) pendingCloses.add(previousPort);
    emit();

    const closeError = await closePendingPorts();
    if (token !== generation) return false;
    if (closeError) {
      pendingSelection = null;
      state.busy = false;
      state.error = errorText(closeError);
      emit();
      return false;
    }
    if (access !== owner || pendingSelection?.token !== token) return false;
    if (!isAvailable(owner, port)) {
      pendingSelection = null;
      state.busy = false;
      state.error = 'The selected MIDI input is no longer available.';
      emit();
      return false;
    }

    try {
      await port.open();
    } catch (error) {
      const current = token === generation && access === owner && pendingSelection?.token === token;
      if (!current) {
        await closeStalePort(port);
        return false;
      }
      const closeFailure = await closePort(port);
      if (token === generation && access === owner && pendingSelection?.token === token) {
        pendingSelection = null;
        state.busy = false;
        state.error = closeFailure ? `${errorText(error)}; ${errorText(closeFailure)}` : errorText(error);
        emit();
      }
      return false;
    }

    if (token !== generation || access !== owner || pendingSelection?.token !== token) {
      await closeStalePort(port);
      return false;
    }
    if (!isAvailable(owner, port)) {
      const closeFailure = await closePort(port);
      if (token === generation && access === owner && pendingSelection?.token === token) {
        pendingSelection = null;
        state.busy = false;
        state.error = closeFailure
          ? `The selected MIDI input is no longer available.; ${errorText(closeFailure)}`
          : 'The selected MIDI input is no longer available.';
        emit();
      }
      return false;
    }

    const handler = event => {
      if (selected?.port === port && access === owner) handleMessage(id, event);
    };
    try {
      port.addEventListener('midimessage', handler);
    } catch (error) {
      const closeFailure = await closePort(port);
      if (token === generation && access === owner && pendingSelection?.token === token) {
        pendingSelection = null;
        state.busy = false;
        state.error = closeFailure ? `${errorText(error)}; ${errorText(closeFailure)}` : errorText(error);
        emit();
      }
      return false;
    }

    if (token !== generation || access !== owner || pendingSelection?.token !== token) {
      port.removeEventListener('midimessage', handler);
      await closeStalePort(port);
      return false;
    }
    selected = {port, handler};
    pendingSelection = null;
    state.selectedId = id;
    state.busy = false;
    state.error = '';
    emit();
    return true;
  }

  async function disconnect() {
    const token = ++generation;
    pendingSelection = null;
    releaseNotes();
    const previousPort = detachSelected();
    if (previousPort) pendingCloses.add(previousPort);

    if (accessListener) {
      accessListener.owner.removeEventListener('statechange', accessListener.handler);
      accessListener = null;
    }
    access = null;
    state.enabled = false;
    state.inputs = [];
    state.selectedId = '';
    state.error = '';
    state.busy = pendingCloses.size > 0;
    emit();

    const closeError = await closePendingPorts();
    if (token !== generation) return false;
    state.busy = false;
    if (closeError) state.error = errorText(closeError);
    emit();
    return !closeError;
  }

  return {
    connect,
    selectInput,
    disconnect,
    releaseNotes,
    getState:snapshot,
  };
}
