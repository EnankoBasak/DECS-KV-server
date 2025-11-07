-- Create the database
CREATE DATABASE IF NOT EXISTS kvstore;

-- Switch to the new database
USE kvstore;

-- Create the key-value table
CREATE TABLE IF NOT EXISTS kv (
    k VARCHAR(64) PRIMARY KEY,      -- Key (string, unique)
    value VARCHAR(512) NOT NULL     -- Value (string payload, up to 512 chars)
);

-- Optional: Index on value (for faster searches, if ever needed)
CREATE INDEX idx_value ON kv(value);

-- Verify structure
DESCRIBE kv;


