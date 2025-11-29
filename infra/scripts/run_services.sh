#!/bin/bash

echo "=== TinyIM Service Launcher ==="

# Function to wait for MySQL
wait_for_mysql() {
    local host="${MYSQL_HOST:-tinyim_mysql}"
    echo "Waiting for MySQL to be ready at $host..."
    for i in {1..30}; do
        if mysqladmin ping -h "$host" -uroot -proot_password --silent 2>/dev/null; then
            echo "MySQL is ready!"
            return 0
        fi
        echo "MySQL not ready yet, waiting... ($i/30)"
        sleep 2
    done
    echo "ERROR: MySQL failed to start in time"
    return 1
}

# Function to wait for Redis
wait_for_redis() {
    local host="${REDIS_HOST:-tinyim_redis}"
    echo "Waiting for Redis to be ready at $host..."
    for i in {1..30}; do
        if redis-cli -h "$host" ping 2>/dev/null | grep -q PONG; then
            echo "Redis is ready!"
            return 0
        fi
        echo "Redis not ready yet, waiting... ($i/30)"
        sleep 2
    done
    echo "ERROR: Redis failed to start in time"
    return 1
}

# Wait for dependencies
wait_for_mysql || exit 1
wait_for_redis || exit 1

echo ""
echo "All dependencies are ready. Starting services..."
echo ""

# Kill existing processes
pkill -f auth_server 2>/dev/null
pkill -f chat_server 2>/dev/null
pkill -f gateway_server 2>/dev/null
pkill -f status_server 2>/dev/null

sleep 1

# Start services with nohup
echo "Starting Auth Server..."
nohup /app/build/services/auth/auth_server > /app/auth.log 2>&1 &
AUTH_PID=$!

sleep 1

echo "Starting Chat Server..."
nohup /app/build/services/chat/chat_server > /app/chat.log 2>&1 &
CHAT_PID=$!

sleep 1

echo "Starting Gateway Server..."
nohup /app/build/services/gateway/gateway_server > /app/gateway.log 2>&1 &
GATEWAY_PID=$!

sleep 1

echo "Starting Status Server..."
nohup /app/build/services/status/status_server > /app/status.log 2>&1 &
STATUS_PID=$!

sleep 2

# Check if services are running
echo ""
echo "=== Service Status ==="
ps aux | grep -E "auth_server|chat_server|gateway_server" | grep -v grep

# Verify processes
if ps -p $AUTH_PID > /dev/null 2>&1; then
    echo "[✓] Auth Server started (PID: $AUTH_PID)"
else
    echo "[✗] Auth Server failed to start. Check /app/auth.log"
fi

if ps -p $CHAT_PID > /dev/null 2>&1; then
    echo "[✓] Chat Server started (PID: $CHAT_PID)"
else
    echo "[✗] Chat Server failed to start. Check /app/chat.log"
fi

if ps -p $GATEWAY_PID > /dev/null 2>&1; then
    echo "[✓] Gateway Server started (PID: $GATEWAY_PID)"
else
    echo "[✗] Gateway Server failed to start. Check /app/gateway.log"
fi

if ps -p $STATUS_PID > /dev/null 2>&1; then
    echo "[✓] Status Server started (PID: $STATUS_PID)"
else
    echo "[✗] Status Server failed to start. Check /app/status.log"
fi

echo ""
echo "=== Services Started ==="
echo "Auth Server:    localhost:50051"
echo "Chat Server:    localhost:50052"
echo "Gateway Server: localhost:8080"
echo "Status Server:  localhost:50053"
echo ""
echo "Logs:"
echo "  - /app/auth.log"
echo "  - /app/chat.log"
echo "  - /app/gateway.log"
echo "  - /app/status.log"
echo ""
