ALTER TABLE transactions MODIFY status ENUM('Pending', 'Completed', 'Reversed', 'Disputed') NOT NULL DEFAULT 'Pending';
ALTER TABLE transactions ADD CONSTRAINT uq_trans_reversal_single UNIQUE (organization_id, reversal_of_id);
ALTER TABLE transactions ADD INDEX idx_trans_org_status (organization_id, status, created_at);
ALTER TABLE transactions ADD INDEX idx_trans_org_amount (organization_id, amount, created_at);
