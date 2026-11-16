#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int execute_schema(DBClient *client) {
    const char *statements[] = {
        "DROP TABLE IF EXISTS outbox_consumer_cursors;",
        "DROP TABLE IF EXISTS login_rate_limits;",
        "DROP TABLE IF EXISTS event_outbox;",
        "DROP TABLE IF EXISTS sessions;",
        "DROP TABLE IF EXISTS transactions;",
        "DROP TABLE IF EXISTS entities;",
        "DROP TABLE IF EXISTS users;",
        "DROP TABLE IF EXISTS organizations;",
        "CREATE TABLE organizations ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  name VARCHAR(150) NOT NULL UNIQUE,"
        "  status ENUM('Active', 'Suspended') NOT NULL DEFAULT 'Active',"
        "  event_sequence_counter BIGINT NOT NULL DEFAULT 0,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB;",
        "CREATE TABLE users ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  organization_id BIGINT NOT NULL,"
        "  email VARCHAR(255) NOT NULL,"
        "  password_hash VARCHAR(255) NOT NULL,"
        "  role ENUM('Admin', 'Finance_Manager', 'Viewer') NOT NULL DEFAULT 'Viewer',"
        "  status ENUM('Active', 'Deactivated') NOT NULL DEFAULT 'Active',"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  CONSTRAINT fk_users_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT,"
        "  CONSTRAINT uq_users_org_id UNIQUE (organization_id, id),"
        "  CONSTRAINT uq_users_org_email UNIQUE (organization_id, email)"
        ") ENGINE=InnoDB;",
        "CREATE TABLE entities ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  organization_id BIGINT NOT NULL,"
        "  name VARCHAR(150) NOT NULL,"
        "  entity_type ENUM('Department', 'Vendor', 'Subsidiary', 'Client', 'Partner') NOT NULL DEFAULT 'Department',"
        "  status ENUM('Active', 'Inactive') NOT NULL DEFAULT 'Active',"
        "  created_by_user_id BIGINT NULL,"
        "  updated_by_user_id BIGINT NULL,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "  CONSTRAINT fk_entities_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT,"
        "  CONSTRAINT uq_entities_org_id UNIQUE (organization_id, id),"
        "  CONSTRAINT uq_entities_org_name UNIQUE (organization_id, name),"
        "  CONSTRAINT fk_entities_created_by FOREIGN KEY (organization_id, created_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT,"
        "  CONSTRAINT fk_entities_updated_by FOREIGN KEY (organization_id, updated_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT"
        ") ENGINE=InnoDB;",
        "CREATE TABLE transactions ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  organization_id BIGINT NOT NULL,"
        "  sender_entity_id BIGINT NOT NULL,"
        "  receiver_entity_id BIGINT NOT NULL,"
        "  amount DECIMAL(15, 2) NOT NULL,"
        "  currency CHAR(3) NOT NULL DEFAULT 'INR',"
        "  status ENUM('Pending', 'Completed', 'Reversed', 'Disputed') NOT NULL DEFAULT 'Pending',"
        "  transaction_type ENUM('Standard', 'Reversal', 'Settlement_Adjustment') NOT NULL DEFAULT 'Standard',"
        "  idempotency_key VARCHAR(64) NOT NULL,"
        "  request_fingerprint CHAR(64) NOT NULL,"
        "  reversal_of_id BIGINT NULL,"
        "  created_by_user_id BIGINT NULL,"
        "  description VARCHAR(255) NULL,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  CONSTRAINT chk_diff_entities CHECK (sender_entity_id <> receiver_entity_id),"
        "  CONSTRAINT chk_positive_amount CHECK (amount > 0.00),"
        "  CONSTRAINT uq_trans_org_id UNIQUE (organization_id, id),"
        "  CONSTRAINT uq_trans_id_org UNIQUE (id, organization_id),"
        "  CONSTRAINT uq_trans_org_idempotency UNIQUE (organization_id, idempotency_key),"
        "  CONSTRAINT uq_trans_reversal_single UNIQUE (organization_id, reversal_of_id),"
        "  CONSTRAINT fk_trans_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT,"
        "  CONSTRAINT fk_trans_sender_comp FOREIGN KEY (organization_id, sender_entity_id) REFERENCES entities(organization_id, id) ON DELETE RESTRICT,"
        "  CONSTRAINT fk_trans_receiver_comp FOREIGN KEY (organization_id, receiver_entity_id) REFERENCES entities(organization_id, id) ON DELETE RESTRICT,"
        "  CONSTRAINT fk_trans_user_comp FOREIGN KEY (organization_id, created_by_user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT,"
        "  CONSTRAINT fk_trans_reversal_comp FOREIGN KEY (reversal_of_id, organization_id) REFERENCES transactions(id, organization_id) ON DELETE RESTRICT,"
        "  INDEX idx_trans_org_created (organization_id, created_at),"
        "  INDEX idx_trans_sender (organization_id, sender_entity_id, created_at),"
        "  INDEX idx_trans_receiver (organization_id, receiver_entity_id, created_at),"
        "  INDEX idx_trans_org_status (organization_id, status, created_at),"
        "  INDEX idx_trans_org_amount (organization_id, amount, created_at)"
        ") ENGINE=InnoDB;",
        "CREATE TABLE sessions ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  organization_id BIGINT NOT NULL,"
        "  user_id BIGINT NOT NULL,"
        "  token_hash VARCHAR(64) NOT NULL UNIQUE,"
        "  csrf_token_hash VARCHAR(64) NOT NULL,"
        "  prev_csrf_token_hash VARCHAR(64) NULL,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  expires_at TIMESTAMP NOT NULL,"
        "  last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  revoked_at TIMESTAMP NULL,"
        "  CONSTRAINT fk_sessions_user_comp FOREIGN KEY (organization_id, user_id) REFERENCES users(organization_id, id) ON DELETE RESTRICT,"
        "  INDEX idx_sessions_org_user (organization_id, user_id),"
        "  INDEX idx_sessions_expires (expires_at)"
        ") ENGINE=InnoDB;",
        "CREATE TABLE event_outbox ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  organization_id BIGINT NOT NULL,"
        "  event_sequence BIGINT NOT NULL,"
        "  event_type ENUM('TRANSACTION_CREATED', 'TRANSACTION_STATUS_CHANGED', 'TRANSACTION_REVERSED') NOT NULL,"
        "  transaction_id BIGINT NOT NULL,"
        "  payload JSON NOT NULL,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  CONSTRAINT uq_outbox_org_seq UNIQUE (organization_id, event_sequence),"
        "  CONSTRAINT fk_outbox_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE RESTRICT,"
        "  CONSTRAINT fk_outbox_trans FOREIGN KEY (organization_id, transaction_id) REFERENCES transactions(organization_id, id) ON DELETE RESTRICT,"
        "  INDEX idx_outbox_org_seq (organization_id, event_sequence)"
        ") ENGINE=InnoDB;",
        "CREATE TABLE login_rate_limits ("
        "  organization_id BIGINT NOT NULL,"
        "  email VARCHAR(255) NOT NULL,"
        "  failed_count INT NOT NULL DEFAULT 0,"
        "  last_failed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "  locked_until TIMESTAMP NULL,"
        "  PRIMARY KEY (organization_id, email),"
        "  CONSTRAINT fk_ratelimit_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE CASCADE"
        ") ENGINE=InnoDB;",
        "CREATE TABLE outbox_consumer_cursors ("
        "  instance_id VARCHAR(64) NOT NULL,"
        "  organization_id BIGINT NOT NULL,"
        "  last_consumed_sequence BIGINT NOT NULL DEFAULT 0,"
        "  last_heartbeat TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (instance_id, organization_id),"
        "  CONSTRAINT fk_consumer_org FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE CASCADE"
        ") ENGINE=InnoDB;"
    };

    size_t count = sizeof(statements) / sizeof(statements[0]);
    for (size_t i = 0; i < count; i++) {
        if (db_client_query(client, statements[i]) != 0) {
            printf("Schema creation error on statement %zu: %s\n", i, db_client_get_error(client));
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *env_file = ".env";
    DBConfig config;
    DBClient client;
    MYSQL_RES *res;
    MYSQL_ROW row;
    uint64_t org_a;
    uint64_t org_b;
    uint64_t user_a;
    uint64_t user_b;
    uint64_t ent_a1;
    uint64_t ent_a2;
    uint64_t ent_b1;
    uint64_t ent_b2;
    uint64_t trans_orig_id;
    uint64_t trans_b_orig;
    uint64_t trans_c_id;
    uint64_t trans_d_id;
    char sql_buf[1024];

    if (argc > 1) {
        env_file = argv[1];
    }

    load_env_file(env_file);

    config.host = get_env_value("DB_HOST", "127.0.0.1");
    config.port = get_env_int("DB_PORT", 3307);
    config.database = get_env_value("DB_NAME", "settlesmart_test");
    config.user = get_env_value("DB_USER", "settlesmart_user");
    config.password = get_env_value("DB_PASSWORD", "dev_password_123");
    config.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    if (db_client_init(&client) != 0) {
        printf("FAILED: db_client_init\n");
        return 1;
    }

    if (db_client_connect(&client, &config) != 0) {
        printf("FAILED: db_client_connect: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }

    if (execute_schema(&client) != 0) {
        db_client_close(&client);
        return 1;
    }
    printf("Schema initialized successfully in database.\n\n");

    if (db_client_query(&client, "INSERT INTO organizations (name) VALUES ('Organization Alpha');") != 0) {
        printf("FAILED: Insert Org A\n");
        return 1;
    }
    org_a = db_client_last_insert_id(&client);

    if (db_client_query(&client, "INSERT INTO organizations (name) VALUES ('Organization Beta');") != 0) {
        printf("FAILED: Insert Org B\n");
        return 1;
    }
    org_b = db_client_last_insert_id(&client);

    snprintf(sql_buf, sizeof(sql_buf),
        "INSERT INTO users (organization_id, email, password_hash, role) VALUES (%llu, 'admin@alpha.org', 'argon2id_hash_sample_a', 'Admin');",
        (unsigned long long)org_a);
    db_client_query(&client, sql_buf);
    user_a = db_client_last_insert_id(&client);

    snprintf(sql_buf, sizeof(sql_buf),
        "INSERT INTO users (organization_id, email, password_hash, role) VALUES (%llu, 'admin@beta.org', 'argon2id_hash_sample_b', 'Admin');",
        (unsigned long long)org_b);
    db_client_query(&client, sql_buf);
    user_b = db_client_last_insert_id(&client);

    snprintf(sql_buf, sizeof(sql_buf), "INSERT INTO entities (organization_id, name, entity_type) VALUES (%llu, 'Entity A1', 'Department');", (unsigned long long)org_a);
    db_client_query(&client, sql_buf);
    ent_a1 = db_client_last_insert_id(&client);

    snprintf(sql_buf, sizeof(sql_buf), "INSERT INTO entities (organization_id, name, entity_type) VALUES (%llu, 'Entity A2', 'Vendor');", (unsigned long long)org_a);
    db_client_query(&client, sql_buf);
    ent_a2 = db_client_last_insert_id(&client);

    snprintf(sql_buf, sizeof(sql_buf), "INSERT INTO entities (organization_id, name, entity_type) VALUES (%llu, 'Entity B1', 'Department');", (unsigned long long)org_b);
    db_client_query(&client, sql_buf);
    ent_b1 = db_client_last_insert_id(&client);

    snprintf(sql_buf, sizeof(sql_buf), "INSERT INTO entities (organization_id, name, entity_type) VALUES (%llu, 'Entity B2', 'Vendor');", (unsigned long long)org_b);
    db_client_query(&client, sql_buf);
    ent_b2 = db_client_last_insert_id(&client);

    if (org_a > 0 && org_b > 0 && user_a > 0 && user_b > 0 && ent_a1 > 0 && ent_a2 > 0 && ent_b1 > 0 && ent_b2 > 0) {
        printf("TEST 1 PASSED: Automatic IDs generated: Org A=%llu, Org B=%llu, User A=%llu, User B=%llu, Ent A1=%llu, Ent A2=%llu, Ent B1=%llu, Ent B2=%llu\n",
               (unsigned long long)org_a, (unsigned long long)org_b, (unsigned long long)user_a, (unsigned long long)user_b,
               (unsigned long long)ent_a1, (unsigned long long)ent_a2, (unsigned long long)ent_b1, (unsigned long long)ent_b2);
    } else {
        printf("TEST 1 FAILED: Non-positive IDs returned\n");
        return 1;
    }

    snprintf(sql_buf, sizeof(sql_buf),
        "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, amount, idempotency_key, request_fingerprint) "
        "VALUES (%llu, %llu, %llu, 500.00, 'cross_org_key_001', 'fingerprint_test_1');",
        (unsigned long long)org_a, (unsigned long long)ent_a1, (unsigned long long)ent_b1);

    if (db_client_query(&client, sql_buf) != 0) {
        printf("TEST 2 PASSED: Cross-organization entity reference rejected by composite foreign key constraint: %s\n", db_client_get_error(&client));
    } else {
        printf("TEST 2 FAILED: Cross-organization entity reference was accepted\n");
        return 1;
    }

    snprintf(sql_buf, sizeof(sql_buf),
        "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, amount, idempotency_key, request_fingerprint) "
        "VALUES (%llu, %llu, %llu, 500.25, 'idemp_key_unique_1', 'fp_valid_1');",
        (unsigned long long)org_a, (unsigned long long)ent_a1, (unsigned long long)ent_a2);

    if (db_client_query(&client, sql_buf) != 0) {
        printf("FAILED: Valid transaction insertion failed: %s\n", db_client_get_error(&client));
        return 1;
    }
    trans_orig_id = db_client_last_insert_id(&client);

    if (db_client_query(&client, sql_buf) != 0) {
        printf("TEST 3 PASSED: Duplicate idempotency key rejected by unique constraint: %s\n", db_client_get_error(&client));
    } else {
        printf("TEST 3 FAILED: Duplicate idempotency key was accepted\n");
        return 1;
    }

    snprintf(sql_buf, sizeof(sql_buf), "SELECT amount FROM transactions WHERE id = %llu;", (unsigned long long)trans_orig_id);
    db_client_query(&client, sql_buf);
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    if (row && strcmp(row[0], "500.25") == 0) {
        printf("TEST 4 PASSED: Exact financial precision verified: retrieved %s\n", row[0]);
    } else {
        printf("TEST 4 FAILED: Exact precision mismatch: got %s\n", row ? row[0] : "NULL");
        return 1;
    }
    db_client_free_result(res);

    snprintf(sql_buf, sizeof(sql_buf),
        "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, amount, idempotency_key, request_fingerprint) "
        "VALUES (%llu, 999999, %llu, 100.00, 'nonexistent_key_1', 'fp_nonexistent_1');",
        (unsigned long long)org_a, (unsigned long long)ent_a2);

    if (db_client_query(&client, sql_buf) != 0) {
        printf("TEST 5 PASSED: Nonexistent sender entity rejected by foreign key constraint: %s\n", db_client_get_error(&client));
    } else {
        printf("TEST 5 FAILED: Nonexistent sender entity was accepted\n");
        return 1;
    }

    snprintf(sql_buf, sizeof(sql_buf),
        "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, amount, idempotency_key, request_fingerprint) "
        "VALUES (%llu, %llu, %llu, 700.00, 'org_b_trans_key_1', 'fp_b_1');",
        (unsigned long long)org_b, (unsigned long long)ent_b1, (unsigned long long)ent_b2);
    if (db_client_query(&client, sql_buf) != 0) {
        printf("FAILED: Org B transaction failed: %s\n", db_client_get_error(&client));
        return 1;
    }
    trans_b_orig = db_client_last_insert_id(&client);

    snprintf(sql_buf, sizeof(sql_buf),
        "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, created_by_user_id, amount, idempotency_key, request_fingerprint) "
        "VALUES (%llu, %llu, %llu, %llu, 250.00, 'test_a_cross_creator_key', 'fp_a');",
        (unsigned long long)org_a, (unsigned long long)ent_a1, (unsigned long long)ent_a2, (unsigned long long)user_b);

    if (db_client_query(&client, sql_buf) != 0) {
        printf("TEST A PASSED: Cross-organization creator rejected by composite foreign key: %s\n", db_client_get_error(&client));
    } else {
        printf("TEST A FAILED: Cross-organization creator was accepted\n");
        return 1;
    }

    snprintf(sql_buf, sizeof(sql_buf),
        "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, amount, transaction_type, reversal_of_id, idempotency_key, request_fingerprint) "
        "VALUES (%llu, %llu, %llu, 700.00, 'Reversal', %llu, 'test_b_cross_reversal_key', 'fp_b');",
        (unsigned long long)org_a, (unsigned long long)ent_a2, (unsigned long long)ent_a1, (unsigned long long)trans_b_orig);

    if (db_client_query(&client, sql_buf) != 0) {
        printf("TEST B PASSED: Cross-organization reversal reference rejected by composite foreign key: %s\n", db_client_get_error(&client));
    } else {
        printf("TEST B FAILED: Cross-organization reversal was accepted\n");
        return 1;
    }

    snprintf(sql_buf, sizeof(sql_buf),
        "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, created_by_user_id, amount, idempotency_key, request_fingerprint) "
        "VALUES (%llu, %llu, %llu, %llu, 350.00, 'test_c_valid_creator_key', 'fp_c');",
        (unsigned long long)org_a, (unsigned long long)ent_a1, (unsigned long long)ent_a2, (unsigned long long)user_a);

    if (db_client_query(&client, sql_buf) == 0) {
        trans_c_id = db_client_last_insert_id(&client);
        printf("TEST C PASSED: Valid transaction with same-organization creator succeeded: ID %llu, User %llu\n",
               (unsigned long long)trans_c_id, (unsigned long long)user_a);
    } else {
        printf("TEST C FAILED: Valid transaction with same-organization creator failed: %s\n", db_client_get_error(&client));
        return 1;
    }

    snprintf(sql_buf, sizeof(sql_buf),
        "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, amount, transaction_type, reversal_of_id, idempotency_key, request_fingerprint) "
        "VALUES (%llu, %llu, %llu, 350.00, 'Reversal', %llu, 'test_d_valid_reversal_key', 'fp_d');",
        (unsigned long long)org_a, (unsigned long long)ent_a2, (unsigned long long)ent_a1, (unsigned long long)trans_c_id);

    if (db_client_query(&client, sql_buf) == 0) {
        trans_d_id = db_client_last_insert_id(&client);
        printf("TEST D PASSED: Valid reversal reference within same organization succeeded: Reversal ID %llu -> Original ID %llu\n",
               (unsigned long long)trans_d_id, (unsigned long long)trans_c_id);
    } else {
        printf("TEST D FAILED: Valid reversal within same organization failed: %s\n", db_client_get_error(&client));
        return 1;
    }

    db_client_begin(&client);
    snprintf(sql_buf, sizeof(sql_buf),
        "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, amount, idempotency_key, request_fingerprint) "
        "VALUES (%llu, %llu, %llu, 9999.00, 'rollback_test_key_1', 'fp_rollback_1');",
        (unsigned long long)org_a, (unsigned long long)ent_a1, (unsigned long long)ent_a2);
    db_client_query(&client, sql_buf);
    db_client_rollback(&client);

    db_client_query(&client, "SELECT COUNT(*) FROM transactions WHERE idempotency_key = 'rollback_test_key_1';");
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    if (row && strcmp(row[0], "0") == 0) {
        printf("TEST 7 PASSED: Transaction rollback integrity verified: rolled-back record does not persist in table\n");
    } else {
        printf("TEST 7 FAILED: Rolled-back record persisted in database\n");
        return 1;
    }
    db_client_free_result(res);

    db_client_close(&client);
    printf("\nALL DATABASE INTEGRITY TESTS (INCLUDING TESTS A, B, C, D) PASSED SUCCESSFULLY.\n");
    return 0;
}
