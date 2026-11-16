#include "../src/auth/auth_service.h"
#include "../src/auth/authorization.h"
#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cleanup_test_data(DBClient *client) {
    db_client_query(client, "DELETE FROM sessions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'AuthzTest%');");
    db_client_query(client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'AuthzTest%');");
    db_client_query(client, "DELETE FROM organizations WHERE name LIKE 'AuthzTest%';");
}

int main(int argc, char **argv) {
    const char *env_file = argc > 1 ? argv[1] : ".env";
    load_env_file(env_file);

    DBConfig config;
    config.host = get_env_value("DB_HOST", "127.0.0.1");
    config.port = get_env_int("DB_PORT", 3307);
    config.database = get_env_value("DB_NAME", "settlesmart_test");
    config.user = get_env_value("DB_USER", "settlesmart_user");
    config.password = get_env_value("DB_PASSWORD", "dev_password_123");
    config.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &config) != 0) {
        printf("FAILED: Database connection failed\n");
        return 1;
    }

    cleanup_test_data(&client);
    printf("Running Authorization, Isolation & Last Admin Tests (Groups E, F, G)...\n");

    long long org_a = 0, admin_a1 = 0;
    long long org_b = 0, admin_b = 0;
    char err[256] = {0};

    if (auth_register_organization(&client, "AuthzTest Org Alpha", "admin@orgalpha.com", "MasterPass#2026A",
                                  &org_a, &admin_a1, err, sizeof(err)) != 0) {
        printf("FAILED: Registration Org Alpha failed: %s\n", err);
        db_client_close(&client);
        return 1;
    }

    if (auth_register_organization(&client, "AuthzTest Org Beta", "admin@orgbeta.com", "MasterPass#2026B",
                                  &org_b, &admin_b, err, sizeof(err)) != 0) {
        printf("FAILED: Registration Org Beta failed: %s\n", err);
        db_client_close(&client);
        return 1;
    }

    long long fin_mgr_a = 0;
    int res = auth_create_user(&client, admin_a1, ROLE_ADMIN, org_a,
                              "finance@orgalpha.com", "FinancePass#2026",
                              ROLE_FINANCE_MANAGER, &fin_mgr_a, err, sizeof(err));
    if (res != 0) {
        printf("FAILED: Admin failed to create Finance Manager: %s\n", err);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Admin created Finance Manager with ID: %lld\n", fin_mgr_a);

    long long viewer_a = 0;
    res = auth_create_user(&client, admin_a1, ROLE_ADMIN, org_a,
                          "viewer@orgalpha.com", "ViewerPass#2026",
                          ROLE_VIEWER, &viewer_a, err, sizeof(err));
    if (res != 0) {
        printf("FAILED: Admin failed to create Viewer: %s\n", err);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Admin created Viewer with ID: %lld\n", viewer_a);

    long long unauth_user = 0;
    res = auth_create_user(&client, fin_mgr_a, ROLE_FINANCE_MANAGER, org_a,
                          "hacker@orgalpha.com", "HackerPass#2026",
                          ROLE_VIEWER, &unauth_user, err, sizeof(err));
    if (res == 0) {
        printf("FAILED: Finance Manager was allowed to create a user\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Finance Manager creation attempt rejected: %s\n", err);

    res = auth_create_user(&client, viewer_a, ROLE_VIEWER, org_a,
                          "hacker2@orgalpha.com", "HackerPass#2026",
                          ROLE_VIEWER, &unauth_user, err, sizeof(err));
    if (res == 0) {
        printf("FAILED: Viewer was allowed to create a user\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Viewer creation attempt rejected: %s\n", err);

    res = auth_update_user_status(&client, admin_a1, ROLE_ADMIN, org_a,
                                  admin_a1, "Deactivated", err, sizeof(err));
    if (res != -2) {
        printf("FAILED: Last active Admin deactivation was NOT rejected. Got: %d, err: %s\n", res, err);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Last active Admin deactivation rejected: %s\n", err);

    res = auth_update_user_role(&client, admin_a1, ROLE_ADMIN, org_a,
                                admin_a1, ROLE_VIEWER, err, sizeof(err));
    if (res != -2) {
        printf("FAILED: Last active Admin demotion was NOT rejected. Got: %d, err: %s\n", res, err);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Last active Admin demotion rejected: %s\n", err);

    long long admin_a2 = 0;
    res = auth_create_user(&client, admin_a1, ROLE_ADMIN, org_a,
                          "admin2@orgalpha.com", "MasterPass#2026A2",
                          ROLE_ADMIN, &admin_a2, err, sizeof(err));
    if (res != 0) {
        printf("FAILED: Failed to create second Admin: %s\n", err);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Second Admin created with ID: %lld\n", admin_a2);

    res = auth_update_user_status(&client, admin_a2, ROLE_ADMIN, org_a,
                                  admin_a1, "Deactivated", err, sizeof(err));
    if (res != 0) {
        printf("FAILED: Deactivating first Admin failed when second Admin exists: %s\n", err);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: First Admin deactivation succeeded when second Admin exists\n");

    res = auth_update_user_status(&client, admin_a2, ROLE_ADMIN, org_a,
                                  admin_a2, "Deactivated", err, sizeof(err));
    if (res != -2) {
        printf("FAILED: Deactivating remaining second Admin was NOT rejected\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Deactivating remaining second Admin rejected as expected\n");

    AuthUserInfo b_user_from_a;
    res = auth_get_user_by_id(&client, org_a, admin_b, &b_user_from_a);
    if (res == 0) {
        printf("FAILED: Organization isolation failed: Org A query retrieved Org B user\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Organization isolation verified: Org A cannot query Org B user\n");

    res = auth_update_user_status(&client, admin_a2, ROLE_ADMIN, org_a,
                                  admin_b, "Deactivated", err, sizeof(err));
    if (res == 0) {
        printf("FAILED: Organization isolation failed: Org A admin modified Org B user\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Cross-organization user update rejected: %s\n", err);

    cleanup_test_data(&client);
    db_client_close(&client);
    printf("\nALL AUTHORIZATION, ISOLATION & LAST ADMIN TESTS SUCCEEDED.\n");
    return 0;
}
