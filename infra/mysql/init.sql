CREATE DATABASE IF NOT EXISTS tinyim_db;
USE tinyim_db;

-- Users table
CREATE TABLE IF NOT EXISTS users (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    salt VARCHAR(255) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Messages table
CREATE TABLE IF NOT EXISTS messages (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    from_id BIGINT NOT NULL,
    to_id BIGINT NOT NULL,
    content TEXT NOT NULL,
    type INT DEFAULT 1, -- 1: Text, 2: Image, etc.
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_chat (from_id, to_id),
    FOREIGN KEY (from_id) REFERENCES users(id),
    FOREIGN KEY (to_id) REFERENCES users(id)
);

-- Friends table (Optional for now, but good to have)
-- Friends table (Established relationships)
CREATE TABLE IF NOT EXISTS friends (
    user_id BIGINT NOT NULL,
    friend_id BIGINT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, friend_id),
    FOREIGN KEY (user_id) REFERENCES users(id),
    FOREIGN KEY (friend_id) REFERENCES users(id)
);

-- Friend Requests table
CREATE TABLE IF NOT EXISTS friend_requests (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    sender_id BIGINT NOT NULL,
    receiver_id BIGINT NOT NULL,
    status INT DEFAULT 0, -- 0: Pending, 1: Accepted, 2: Rejected
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (sender_id) REFERENCES users(id),
    FOREIGN KEY (receiver_id) REFERENCES users(id)
);

-- Sessions table (Recent conversations)
CREATE TABLE IF NOT EXISTS sessions (
    user_id BIGINT NOT NULL,
    peer_id BIGINT NOT NULL,
    last_msg_content TEXT,
    last_msg_timestamp BIGINT,
    unread_count INT DEFAULT 0,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, peer_id),
    FOREIGN KEY (user_id) REFERENCES users(id),
    FOREIGN KEY (peer_id) REFERENCES users(id)
);
