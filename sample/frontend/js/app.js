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
