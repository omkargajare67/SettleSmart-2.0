ALTER TABLE entities MODIFY status ENUM('Active', 'Inactive') NOT NULL DEFAULT 'Active';
ALTER TABLE entities ADD COLUMN created_by_user_id BIGINT NULL AFTER status;
ALTER TABLE entities ADD COLUMN updated_by_user_id BIGINT NULL AFTER created_by_user_id;
ALTER TABLE entities ADD COLUMN updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP AFTER created_at;
ALTER TABLE entities ADD CONSTRAINT fk_entities_created_by FOREIGN KEY (organization_id, created_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT;
ALTER TABLE entities ADD CONSTRAINT fk_entities_updated_by FOREIGN KEY (organization_id, updated_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT;
