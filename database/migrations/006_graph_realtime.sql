ALTER TABLE organizations ADD COLUMN event_sequence_counter BIGINT NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS event_outbox (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    organization_id BIGINT NOT NULL,
    event_sequence BIGINT NOT NULL,
    event_type ENUM('TRANSACTION_CREATED', 'TRANSACTION_STATUS_CHANGED') NOT NULL,
    transaction_id BIGINT NOT NULL,
    payload TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_outbox_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT,
    CONSTRAINT uq_outbox_org_seq UNIQUE (organization_id, event_sequence),
    INDEX idx_outbox_org_seq (organization_id, event_sequence),
    INDEX idx_outbox_tx (organization_id, transaction_id)
) ENGINE=InnoDB;
