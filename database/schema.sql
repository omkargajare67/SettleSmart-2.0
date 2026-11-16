CREATE TABLE IF NOT EXISTS organizations (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(150) NOT NULL UNIQUE,
    status ENUM('Active', 'Suspended') NOT NULL DEFAULT 'Active',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    event_sequence_counter BIGINT NOT NULL DEFAULT 0
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS users (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    organization_id BIGINT NOT NULL,
    email VARCHAR(255) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    role ENUM('Admin', 'Finance_Manager', 'Viewer') NOT NULL DEFAULT 'Viewer',
    status ENUM('Active', 'Deactivated') NOT NULL DEFAULT 'Active',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_users_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT,
    CONSTRAINT uq_users_org_id UNIQUE (organization_id, id),
    CONSTRAINT uq_users_org_email UNIQUE (organization_id, email)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS entities (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    organization_id BIGINT NOT NULL,
    name VARCHAR(150) NOT NULL,
    entity_type ENUM('Department', 'Vendor', 'Subsidiary', 'Client', 'Partner') NOT NULL DEFAULT 'Department',
    status ENUM('Active', 'Inactive') NOT NULL DEFAULT 'Active',
    created_by_user_id BIGINT NULL,
    updated_by_user_id BIGINT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT fk_entities_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT,
    CONSTRAINT uq_entities_org_id UNIQUE (organization_id, id),
    CONSTRAINT uq_entities_org_name UNIQUE (organization_id, name),
    CONSTRAINT fk_entities_created_by FOREIGN KEY (organization_id, created_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT,
    CONSTRAINT fk_entities_updated_by FOREIGN KEY (organization_id, updated_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS transactions (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    organization_id BIGINT NOT NULL,
    sender_entity_id BIGINT NOT NULL,
    receiver_entity_id BIGINT NOT NULL,
    amount DECIMAL(15, 2) NOT NULL,
    currency CHAR(3) NOT NULL DEFAULT 'INR',
    status ENUM('Pending', 'Completed', 'Reversed', 'Disputed') NOT NULL DEFAULT 'Pending',
    transaction_type ENUM('Standard', 'Reversal', 'Settlement_Adjustment') NOT NULL DEFAULT 'Standard',
    idempotency_key VARCHAR(64) NOT NULL,
    request_fingerprint CHAR(64) NOT NULL,
    reversal_of_id BIGINT NULL,
    created_by_user_id BIGINT NULL,
    description VARCHAR(255) NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT chk_diff_entities CHECK (sender_entity_id <> receiver_entity_id),
    CONSTRAINT chk_positive_amount CHECK (amount > 0.00),
    CONSTRAINT uq_trans_org_id UNIQUE (organization_id, id),
    CONSTRAINT uq_trans_id_org UNIQUE (id, organization_id),
    CONSTRAINT uq_trans_org_idempotency UNIQUE (organization_id, idempotency_key),
    CONSTRAINT uq_trans_reversal_single UNIQUE (organization_id, reversal_of_id),
    CONSTRAINT fk_trans_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT,
    CONSTRAINT fk_trans_sender_comp FOREIGN KEY (organization_id, sender_entity_id) REFERENCES entities(organization_id, id) ON DELETE RESTRICT,
    CONSTRAINT fk_trans_receiver_comp FOREIGN KEY (organization_id, receiver_entity_id) REFERENCES entities(organization_id, id) ON DELETE RESTRICT,
    CONSTRAINT fk_trans_user_comp FOREIGN KEY (organization_id, created_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT,
    CONSTRAINT fk_trans_reversal_comp FOREIGN KEY (reversal_of_id, organization_id) REFERENCES transactions(id, organization_id) ON DELETE RESTRICT,
    INDEX idx_trans_org_created (organization_id, created_at),
    INDEX idx_trans_sender (organization_id, sender_entity_id, created_at),
    INDEX idx_trans_receiver (organization_id, receiver_entity_id, created_at),
    INDEX idx_trans_org_status (organization_id, status, created_at),
    INDEX idx_trans_org_amount (organization_id, amount, created_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS sessions (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    organization_id BIGINT NOT NULL,
    user_id BIGINT NOT NULL,
    token_hash VARCHAR(64) NOT NULL UNIQUE,
    csrf_token_hash VARCHAR(64) NOT NULL,
    prev_csrf_token_hash VARCHAR(64) NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    revoked_at TIMESTAMP NULL,
    CONSTRAINT fk_sessions_user_comp FOREIGN KEY (organization_id, user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT,
    INDEX idx_sessions_org_user (organization_id, user_id),
    INDEX idx_sessions_expires (expires_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS event_outbox (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    organization_id BIGINT NOT NULL,
    event_sequence BIGINT NOT NULL,
    event_type ENUM('TRANSACTION_CREATED', 'TRANSACTION_STATUS_CHANGED', 'TRANSACTION_REVERSED') NOT NULL,
    transaction_id BIGINT NOT NULL,
    payload TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_outbox_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT,
    CONSTRAINT uq_outbox_org_seq UNIQUE (organization_id, event_sequence),
    INDEX idx_outbox_org_seq (organization_id, event_sequence),
    INDEX idx_outbox_tx (organization_id, transaction_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS login_rate_limits (
    organization_id BIGINT NOT NULL,
    email VARCHAR(255) NOT NULL,
    failed_count INT NOT NULL DEFAULT 0,
    last_failed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    locked_until TIMESTAMP NULL,
    PRIMARY KEY (organization_id, email),
    CONSTRAINT fk_ratelimit_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS outbox_consumer_cursors (
    instance_id VARCHAR(64) NOT NULL,
    organization_id BIGINT NOT NULL,
    last_consumed_sequence BIGINT NOT NULL DEFAULT 0,
    last_heartbeat TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (instance_id, organization_id),
    CONSTRAINT fk_consumer_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE CASCADE
) ENGINE=InnoDB;


