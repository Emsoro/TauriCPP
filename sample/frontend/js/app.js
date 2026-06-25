// 监听来自C++的事件
__tauricpp__.listen('backend-event', function(data) {
    appendEventLog('backend-event', data);
});

__tauricpp__.listen('timer', function(data) {
    appendEventLog('timer', data);
});

function appendEventLog(event, data) {
    const log = document.getElementById('eventLog');
    const time = new Date().toLocaleTimeString();
    const item = document.createElement('div');
    item.className = 'event-item';
    item.innerHTML = '<span class="time">[' + time + '] ' + event + ':</span> <span class="data">' + JSON.stringify(data) + '</span>';
    if (log.textContent === 'Waiting for events...') {
        log.textContent = '';
    }
    log.appendChild(item);
    log.scrollTop = log.scrollHeight;
}

async function callGreet() {
    const name = document.getElementById('nameInput').value;
    try {
        const result = await __tauricpp__.invoke('greet', { name: name });
        document.getElementById('invokeResult').textContent = 'greet result: ' + JSON.stringify(result);
    } catch(e) {
        document.getElementById('invokeResult').textContent = 'Error: ' + e.message;
    }
}

async function callAdd() {
    const a = parseInt(document.getElementById('aInput').value);
    const b = parseInt(document.getElementById('bInput').value);
    try {
        const result = await __tauricpp__.invoke('add', { a: a, b: b });
        document.getElementById('invokeResult').textContent = 'add result: ' + JSON.stringify(result);
    } catch(e) {
        document.getElementById('invokeResult').textContent = 'Error: ' + e.message;
    }
}

async function callEcho() {
    const msg = document.getElementById('echoInput').value;
    try {
        const result = await __tauricpp__.invoke('echo', { message: msg });
        document.getElementById('invokeResult').textContent = 'echo result: ' + JSON.stringify(result);
    } catch(e) {
        document.getElementById('invokeResult').textContent = 'Error: ' + e.message;
    }
}

async function getSystemInfo() {
    try {
        const result = await __tauricpp__.invoke('get_system_info', {});
        document.getElementById('sysInfo').textContent = JSON.stringify(result, null, 2);
    } catch(e) {
        document.getElementById('sysInfo').textContent = 'Error: ' + e.message;
    }
}

// ---- Window Controls ----
async function windowMinimize() {
    await __tauricpp__.invoke('window.minimize', {});
}

async function windowMaximize() {
    await __tauricpp__.invoke('window.maximize', {});
}

async function windowRestore() {
    await __tauricpp__.invoke('window.restore', {});
}

async function windowSetTitle() {
    const title = document.getElementById('titleInput').value;
    await __tauricpp__.invoke('window.set_title', { title: title });
}

// ---- File Dialog ----
async function dialogOpen() {
    try {
        const result = await __tauricpp__.invoke('dialog.open', {
            title: 'Open File',
            filters: [
                { name: 'Text Files', pattern: '*.txt' },
                { name: 'All Files', pattern: '*.*' }
            ]
        });
        document.getElementById('dialogResult').textContent = JSON.stringify(result, null, 2);
    } catch(e) {
        document.getElementById('dialogResult').textContent = 'Error: ' + e.message;
    }
}

async function dialogSave() {
    try {
        const result = await __tauricpp__.invoke('dialog.save', {
            title: 'Save File',
            default_filename: 'untitled.txt',
            filters: [
                { name: 'Text Files', pattern: '*.txt' },
                { name: 'All Files', pattern: '*.*' }
            ]
        });
        document.getElementById('dialogResult').textContent = JSON.stringify(result, null, 2);
    } catch(e) {
        document.getElementById('dialogResult').textContent = 'Error: ' + e.message;
    }
}

// ---- Clipboard ----
async function clipboardWrite() {
    const text = document.getElementById('clipboardInput').value;
    try {
        const result = await __tauricpp__.invoke('clipboard.write', { text: text });
        document.getElementById('clipboardResult').textContent = 'Copied: ' + text;
    } catch(e) {
        document.getElementById('clipboardResult').textContent = 'Error: ' + e.message;
    }
}

// ---- IndexedDB Config Persistence ----
const DB_NAME = 'tauricpp_config';
const DB_VERSION = 1;
const STORE_NAME = 'kv';

function openDB() {
    return new Promise((resolve, reject) => {
        const req = indexedDB.open(DB_NAME, DB_VERSION);
        req.onupgradeneeded = (e) => {
            const db = e.target.result;
            if (!db.objectStoreNames.contains(STORE_NAME)) {
                db.createObjectStore(STORE_NAME);
            }
        };
        req.onsuccess = (e) => resolve(e.target.result);
        req.onerror = (e) => reject(e.target.error);
    });
}

async function configSave() {
    const key = document.getElementById('configKey').value.trim();
    const value = document.getElementById('configValue').value;
    if (!key) { showConfigResult('Error: key is empty'); return; }
    try {
        const db = await openDB();
        const tx = db.transaction(STORE_NAME, 'readwrite');
        tx.objectStore(STORE_NAME).put(value, key);
        await new Promise((res, rej) => { tx.oncomplete = res; tx.onerror = rej; });
        showConfigResult('Saved: ' + JSON.stringify({ [key]: value }));
    } catch(e) {
        showConfigResult('Error: ' + e.message);
    }
}

async function configLoad() {
    const key = document.getElementById('configKey').value.trim();
    if (!key) { showConfigResult('Error: key is empty'); return; }
    try {
        const db = await openDB();
        const tx = db.transaction(STORE_NAME, 'readonly');
        const req = tx.objectStore(STORE_NAME).get(key);
        const value = await new Promise((res, rej) => { req.onsuccess = () => res(req.result); req.onerror = rej; });
        document.getElementById('configValue').value = value !== undefined ? value : '';
        showConfigResult(value !== undefined
            ? 'Loaded: ' + JSON.stringify({ [key]: value })
            : 'Key not found: ' + key);
    } catch(e) {
        showConfigResult('Error: ' + e.message);
    }
}

async function configLoadAll() {
    try {
        const db = await openDB();
        const tx = db.transaction(STORE_NAME, 'readonly');
        const store = tx.objectStore(STORE_NAME);
        const allReq = store.getAll();
        const keysReq = store.getAllKeys();
        const [all, keys] = await Promise.all([
            new Promise((res, rej) => { allReq.onsuccess = () => res(allReq.result); allReq.onerror = rej; }),
            new Promise((res, rej) => { keysReq.onsuccess = () => res(keysReq.result); keysReq.onerror = rej; })
        ]);
        const obj = {};
        keys.forEach((k, i) => { obj[k] = all[i]; });
        showConfigResult('All config (' + keys.length + ' entries):\n' + JSON.stringify(obj, null, 2));
    } catch(e) {
        showConfigResult('Error: ' + e.message);
    }
}

async function configDelete() {
    const key = document.getElementById('configKey').value.trim();
    if (!key) { showConfigResult('Error: key is empty'); return; }
    try {
        const db = await openDB();
        const tx = db.transaction(STORE_NAME, 'readwrite');
        tx.objectStore(STORE_NAME).delete(key);
        await new Promise((res, rej) => { tx.oncomplete = res; tx.onerror = rej; });
        showConfigResult('Deleted key: ' + key);
    } catch(e) {
        showConfigResult('Error: ' + e.message);
    }
}

async function configClear() {
    try {
        const db = await openDB();
        const tx = db.transaction(STORE_NAME, 'readwrite');
        tx.objectStore(STORE_NAME).clear();
        await new Promise((res, rej) => { tx.oncomplete = res; tx.onerror = rej; });
        showConfigResult('All config cleared.');
    } catch(e) {
        showConfigResult('Error: ' + e.message);
    }
}

function showConfigResult(msg) {
    document.getElementById('configResult').textContent = typeof msg === 'string' ? msg : JSON.stringify(msg, null, 2);
}

async function clipboardRead() {
    try {
        const result = await __tauricpp__.invoke('clipboard.read', {});
        document.getElementById('clipboardResult').textContent = 'Clipboard: ' + JSON.stringify(result, null, 2);
    } catch(e) {
        document.getElementById('clipboardResult').textContent = 'Error: ' + e.message;
    }
}
