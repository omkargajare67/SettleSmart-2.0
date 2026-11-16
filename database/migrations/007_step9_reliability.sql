CREATE TABLE IF NOT EXISTS login_rate_limits (
    organization_id BIGINT NOT NULL,
    email VARCHAR(255) NOT NULL,
    failed_count INT NOT NULL DEFAULT 0,
    last_failed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    locked_until TIMESTAMP NULL,
    PRIMARY KEY (organization_id, email),
    CONSTRAINT fk_ratelimit_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE CASCADE
) ENGINE=InnoDB;

ALTER TABLE sessions ADD COLUMN prev_csrf_token_hash VARCHAR(64) NULL AFTER csrf_token_hash;

CREATE TABLE IF NOT EXISTS outbox_consumer_cursors (
    instance_id VARCHAR(64) NOT NULL,
    organization_id BIGINT NOT NULL,
    last_consumed_sequence BIGINT NOT NULL DEFAULT 0,
    last_heartbeat TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (instance_id, organization_id),
    CONSTRAINT fk_consumer_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE CASCADE
) ENGINE=InnoDB;
