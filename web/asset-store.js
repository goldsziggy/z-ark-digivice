// Asset storage is deliberately separate from game saves and device identity.
export function emptyAssetState() {
  return { schema: 1, revision: 0, active: null, backgroundActive: null, packs: [], stage: null,
    highWater: { release: 0, versions: [] } };
}

// Small deterministic backend for host tests. update must be synchronous. Both
// implementations expose committed snapshots, never partially updated state.
export class MemoryAssetStore {
  constructor(initial = emptyAssetState()) { this.state = structuredClone(initial); }
  async read() { return structuredClone(this.state); }
  async transaction(update) {
    const draft = structuredClone(this.state);
    const result = update(draft);
    if (result?.then) throw new Error('Asset transaction callback must be synchronous');
    this.state = structuredClone(draft);
    return result;
  }
}

export class IndexedDBAssetStore {
  constructor({ name = 'digivice-assets-v1', indexedDB = globalThis.indexedDB } = {}) {
    this.name = name; this.indexedDB = indexedDB; this.database = null;
  }
  async open() {
    if (this.database) return this.database;
    if (!this.indexedDB) throw new Error('IndexedDB asset storage is unavailable');
    this.database = await new Promise((resolve, reject) => {
      const request = this.indexedDB.open(this.name, 1);
      request.onupgradeneeded = () => request.result.createObjectStore('cache');
      request.onerror = () => reject(request.error || new Error('Asset database open failed'));
      request.onblocked = () => reject(new Error('Asset database upgrade blocked by another tab'));
      request.onsuccess = () => resolve(request.result);
    });
    this.database.onversionchange = () => { this.database.close(); this.database = null; };
    return this.database;
  }
  async read() {
    const db = await this.open();
    return new Promise((resolve, reject) => {
      const tx = db.transaction('cache', 'readonly');
      let state;
      tx.objectStore('cache').get('state').onsuccess = event => { state = event.target.result; };
      tx.oncomplete = () => resolve(state === undefined ? emptyAssetState() : state);
      tx.onerror = tx.onabort = () => reject(tx.error || new Error('Asset read failed'));
    });
  }
  async transaction(update) {
    const db = await this.open();
    return new Promise((resolve, reject) => {
      // Strict requests the strongest available browser durability. Older
      // implementations still retain atomic transaction behavior.
      let tx;
      try { tx = db.transaction('cache', 'readwrite', { durability: 'strict' }); }
      catch { tx = db.transaction('cache', 'readwrite'); }
      let result, failure;
      const store = tx.objectStore('cache');
      store.get('state').onsuccess = event => {
        try {
          const draft = event.target.result === undefined ? emptyAssetState() : event.target.result;
          result = update(draft);
          if (result?.then) throw new Error('Asset transaction callback must be synchronous');
          store.put(draft, 'state');
        } catch (error) { failure = error; tx.abort(); }
      };
      // Request success is insufficient: resolve only after transaction commit.
      tx.oncomplete = () => resolve(result);
      tx.onerror = tx.onabort = () => reject(failure || tx.error || new Error('Asset transaction aborted'));
    });
  }
  close() { this.database?.close(); this.database = null; }
}
