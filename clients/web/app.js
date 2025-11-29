const GATEWAY_URL = 'http://localhost:8080';
const WS_URL = 'ws://localhost:8080';

async function login() {
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;

    try {
        const response = await fetch(`${GATEWAY_URL}/api/login`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `username=${username}&password=${password}`
        });
        const data = await response.json();
        if (data.success) {
            sessionStorage.setItem('token', data.token);
            sessionStorage.setItem('user_id', data.user_id);
            sessionStorage.setItem('username', username);
            window.location.href = 'chat.html';
        } else {
            document.getElementById('error').innerText = data.message;
        }
    } catch (e) {
        document.getElementById('error').innerText = 'Login failed: ' + e;
    }
}

async function register() {
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;

    try {
        const response = await fetch(`${GATEWAY_URL}/api/register`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `username=${username}&password=${password}`
        });
        const data = await response.json();
        if (data.success) {
            alert('Register successful! User ID: ' + data.user_id);
        } else {
            document.getElementById('error').innerText = data.message;
        }
    } catch (e) {
        document.getElementById('error').innerText = 'Register failed: ' + e;
    }
}

let ws;

function initChat() {
    const token = sessionStorage.getItem('token');
    if (!token) {
        window.location.href = 'index.html';
        return;
    }

    document.getElementById('user-info').innerText = `Logged in as: ${sessionStorage.getItem('username')} (ID: ${sessionStorage.getItem('user_id')})`;

    loadFriends();
    loadFriendRequests();

    ws = new WebSocket(`${WS_URL}/ws?token=${token}`);

    ws.onopen = () => {
        console.log('Connected to Gateway');
        addSystemMessage('Connected to server');
    };

    ws.onmessage = (event) => {
        const msg = event.data;
        // Parse JSON: {"from": 123, "content": "hello"}
        try {
            const data = JSON.parse(msg);
            addMessage(`[User ${data.from}]: ${data.content}`, false);
        } catch (e) {
            addMessage(msg, false);
        }
    };

    ws.onclose = () => {
        addSystemMessage('Disconnected');
    };
}

function addSystemMessage(text) {
    const div = document.createElement('div');
    div.className = 'message system-message';
    div.style.textAlign = 'center';
    div.style.color = '#888';
    div.style.fontSize = '0.8em';
    div.innerText = text;
    document.getElementById('messages').appendChild(div);
}

async function loadFriendRequests() {
    const token = sessionStorage.getItem('token');
    try {
        const response = await fetch(`${GATEWAY_URL}/api/friend/requests?token=${token}`);
        const data = await response.json();

        const list = document.getElementById('request-list');
        list.innerHTML = '';

        if (data.success && data.requests) {
            data.requests.forEach(r => {
                const div = document.createElement('div');
                div.className = 'request-item';
                div.innerHTML = `
                <div>${r.sender_username} (ID: ${r.sender_id})</div>
                <div class="request-actions">
                    <button onclick="handleFriendRequest(${r.sender_id}, true)">Accept</button>
                    <button onclick="handleFriendRequest(${r.sender_id}, false)">Reject</button>
                </div>
            `;
                list.appendChild(div);
            });
        } else if (data.requests && data.requests.length === 0) {
            list.innerHTML = '<div style="padding:5px; color:#999;">No pending requests</div>';
        }
    } catch (e) {
        console.error(e);
    }
}

async function handleFriendRequest(senderId, accept) {
    const token = sessionStorage.getItem('token');
    try {
        const response = await fetch(`${GATEWAY_URL}/api/friend/request/handle`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `token=${token}&request_id=${senderId}&accept=${accept}`
        });
        const data = await response.json();
        if (data.success) {
            alert(accept ? 'Friend accepted!' : 'Request rejected');
            loadFriendRequests();
            loadFriends();
        } else {
            alert('Failed: ' + data.message);
        }
    } catch (e) {
        console.error(e);
    }
}

async function loadHistory() {
    console.log("loadHistory called");
    const targetId = document.getElementById('target-user').value;
    const token = sessionStorage.getItem('token');
    const myId = sessionStorage.getItem('user_id');

    console.log(`Target: ${targetId}, Token: ${token}`);

    if (!targetId) {
        alert("Please enter a Target User ID");
        return;
    }

    document.getElementById('messages').innerHTML = ''; // Clear current

    try {
        const url = `${GATEWAY_URL}/api/history?token=${token}&peer_id=${targetId}`;
        console.log("Fetching:", url);
        const response = await fetch(url);
        const data = await response.json();
        console.log("History data:", data);

        if (data.success && data.messages) {
            data.messages.forEach(msg => {
                const isMe = (msg.from == myId);
                const content = `[${new Date(msg.timestamp).toLocaleTimeString()}] ${msg.content}`;
                addMessage(content, isMe);
            });
        } else {
            console.error("Failed to load history:", data.message);
            addSystemMessage("Failed to load history: " + data.message);
        }
    } catch (e) {
        console.error("Failed to load history", e);
        addSystemMessage("Error loading history: " + e);
    }
}

function sendMsg() {
    const input = document.getElementById('msg-input');
    const targetInput = document.getElementById('target-user');
    const text = input.value;
    const targetId = targetInput.value;

    if (text && targetId && ws) {
        // Format: target_id|content
        ws.send(`${targetId}|${text}`);
        addMessage(`[To ${targetId}]: ${text}`, true);
        input.value = '';
    } else {
        alert('Please enter a message and target user ID');
    }
}

// Add event listener for target user input to load history
document.addEventListener('DOMContentLoaded', () => {
    const targetInput = document.getElementById('target-user');
    if (targetInput) {
        targetInput.addEventListener('change', loadHistory);
    }
});

function addMessage(text, isMe) {
    const div = document.createElement('div');
    div.className = `message ${isMe ? 'my-message' : 'other-message'}`;
    div.innerText = text;
    document.getElementById('messages').appendChild(div);
    document.getElementById('messages').scrollTop = document.getElementById('messages').scrollHeight;
}

// ... (Existing code)

async function addFriend() {
    const friendId = document.getElementById('add-friend-id').value;
    const token = sessionStorage.getItem('token');

    if (!friendId) return;

    try {
        const response = await fetch(`${GATEWAY_URL}/api/friend/add`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `token=${token}&friend_id=${friendId}`
        });
        const data = await response.json();
        if (data.success) {
            alert('Friend request sent!');
            // For MVP, auto-refresh list (assuming auto-accept or just to show pending?)
            // Actually, we need to handle request. But for now let's just reload.
        } else {
            alert('Failed: ' + data.message);
        }
    } catch (e) {
        console.error(e);
    }
}

async function loadFriends() {
    const token = sessionStorage.getItem('token');
    try {
        const response = await fetch(`${GATEWAY_URL}/api/friend/list?token=${token}`);
        const data = await response.json();

        const list = document.getElementById('friend-list');
        list.innerHTML = '';

        if (data.success && data.friends) {
            data.friends.forEach(f => {
                const div = document.createElement('div');
                div.className = 'friend-item';
                div.innerText = `${f.username} (ID: ${f.user_id})`;
                div.onclick = () => {
                    document.getElementById('target-user').value = f.user_id;
                    loadHistory();
                };
                list.appendChild(div);
            });
        }
    } catch (e) {
        console.error(e);
    }
}

function switchTab(tab) {
    const contactsView = document.getElementById('contacts-view');
    const sessionsView = document.getElementById('sessions-view');
    const tabContacts = document.getElementById('tab-contacts');
    const tabSessions = document.getElementById('tab-sessions');

    if (tab === 'contacts') {
        contactsView.style.display = 'block';
        sessionsView.style.display = 'none';
        tabContacts.classList.add('active');
        tabContacts.style.background = '#e4e6eb';
        tabContacts.style.fontWeight = 'bold';
        tabSessions.classList.remove('active');
        tabSessions.style.background = '';
        tabSessions.style.fontWeight = 'normal';
        loadFriends();
        loadFriendRequests();
    } else {
        contactsView.style.display = 'none';
        sessionsView.style.display = 'block';
        tabContacts.classList.remove('active');
        tabContacts.style.background = '';
        tabContacts.style.fontWeight = 'normal';
        tabSessions.classList.add('active');
        tabSessions.style.background = '#e4e6eb';
        tabSessions.style.fontWeight = 'bold';
        loadSessions();
    }
}

async function loadSessions() {
    const token = sessionStorage.getItem('token');
    try {
        const response = await fetch(`${GATEWAY_URL}/api/sessions?token=${token}`);
        const data = await response.json();

        const list = document.getElementById('session-list');
        list.innerHTML = '';

        if (data.success && data.sessions) {
            data.sessions.forEach(s => {
                const div = document.createElement('div');
                div.className = 'session-item';
                div.innerHTML = `
                    <div style="font-weight:bold;">User ${s.peer_id}</div>
                    <div style="font-size:0.8em; color:#666; white-space:nowrap; overflow:hidden; text-overflow:ellipsis;">${s.last_msg}</div>
                    <div style="font-size:0.7em; color:#999;">${new Date(s.timestamp).toLocaleString()}</div>
                `;
                div.onclick = () => {
                    document.getElementById('target-user').value = s.peer_id;
                    loadHistory();
                };
                list.appendChild(div);
            });
        } else if (data.sessions && data.sessions.length === 0) {
            list.innerHTML = '<div style="padding:10px; color:#999; text-align:center;">No recent sessions</div>';
        }
    } catch (e) {
        console.error(e);
    }
}


