#include "../src/entities/entity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define EXPECT_TRUE(cond, name) do { \
    if (cond) { \
        printf("PASS: %s\n", name); \
        g_tests_passed++; \
    } else { \
        printf("FAIL: %s\n", name); \
        g_tests_failed++; \
    } \
} while(0)

int main(void) {
    printf("Starting Entity Validation Tests...\n");

    char cleaned[151];

    EXPECT_TRUE(entity_validate_name("", cleaned, sizeof(cleaned)) != 0,
                "Empty name rejected");

    EXPECT_TRUE(entity_validate_name("   ", cleaned, sizeof(cleaned)) != 0,
                "Whitespace only name rejected");

    EXPECT_TRUE(entity_validate_name("\t  \n  ", cleaned, sizeof(cleaned)) != 0,
                "Tab and newline only name rejected");

    EXPECT_TRUE(entity_validate_name(NULL, cleaned, sizeof(cleaned)) != 0,
                "NULL name rejected");

    EXPECT_TRUE(entity_validate_name("  Alpha Corp  ", cleaned, sizeof(cleaned)) == 0,
                "Padded name accepted");
    EXPECT_TRUE(strcmp(cleaned, "Alpha Corp") == 0,
                "Leading and trailing whitespace trimmed");

    char exact_150[151];
    memset(exact_150, 'A', 150);
    exact_150[150] = '\0';
    EXPECT_TRUE(entity_validate_name(exact_150, cleaned, sizeof(cleaned)) == 0,
                "150 character name accepted");
    EXPECT_TRUE(strlen(cleaned) == 150,
                "Cleaned 150 char name length matches");

    char exact_151[152];
    memset(exact_151, 'B', 151);
    exact_151[151] = '\0';
    EXPECT_TRUE(entity_validate_name(exact_151, cleaned, sizeof(cleaned)) != 0,
                "151 character name rejected");

    EXPECT_TRUE(entity_validate_type(ENTITY_TYPE_DEPARTMENT) == 0,
                "Type Department valid");
    EXPECT_TRUE(entity_validate_type(ENTITY_TYPE_VENDOR) == 0,
                "Type Vendor valid");
    EXPECT_TRUE(entity_validate_type(ENTITY_TYPE_SUBSIDIARY) == 0,
                "Type Subsidiary valid");
    EXPECT_TRUE(entity_validate_type(ENTITY_TYPE_CLIENT) == 0,
                "Type Client valid");
    EXPECT_TRUE(entity_validate_type(ENTITY_TYPE_PARTNER) == 0,
                "Type Partner valid");
    EXPECT_TRUE(entity_validate_type(ENTITY_TYPE_UNKNOWN) != 0,
                "Type Unknown rejected");
    EXPECT_TRUE(entity_validate_type((EntityType)999) != 0,
                "Out of range type rejected");

    EXPECT_TRUE(entity_validate_status(ENTITY_STATUS_ACTIVE) == 0,
                "Status Active valid");
    EXPECT_TRUE(entity_validate_status(ENTITY_STATUS_INACTIVE) == 0,
                "Status Inactive valid");
    EXPECT_TRUE(entity_validate_status(ENTITY_STATUS_UNKNOWN) != 0,
                "Status Unknown rejected");
    EXPECT_TRUE(entity_validate_status((EntityStatus)999) != 0,
                "Out of range status rejected");

    EXPECT_TRUE(entity_type_from_string("Department") == ENTITY_TYPE_DEPARTMENT,
                "Parse type Department");
    EXPECT_TRUE(entity_type_from_string("Vendor") == ENTITY_TYPE_VENDOR,
                "Parse type Vendor");
    EXPECT_TRUE(entity_type_from_string("Subsidiary") == ENTITY_TYPE_SUBSIDIARY,
                "Parse type Subsidiary");
    EXPECT_TRUE(entity_type_from_string("Client") == ENTITY_TYPE_CLIENT,
                "Parse type Client");
    EXPECT_TRUE(entity_type_from_string("Partner") == ENTITY_TYPE_PARTNER,
                "Parse type Partner");
    EXPECT_TRUE(entity_type_from_string("InvalidType") == ENTITY_TYPE_UNKNOWN,
                "Parse invalid type returns Unknown");

    EXPECT_TRUE(strcmp(entity_type_to_string(ENTITY_TYPE_DEPARTMENT), "Department") == 0,
                "Convert Department to string");
    EXPECT_TRUE(strcmp(entity_type_to_string(ENTITY_TYPE_VENDOR), "Vendor") == 0,
                "Convert Vendor to string");
    EXPECT_TRUE(strcmp(entity_type_to_string(ENTITY_TYPE_SUBSIDIARY), "Subsidiary") == 0,
                "Convert Subsidiary to string");
    EXPECT_TRUE(strcmp(entity_type_to_string(ENTITY_TYPE_CLIENT), "Client") == 0,
                "Convert Client to string");
    EXPECT_TRUE(strcmp(entity_type_to_string(ENTITY_TYPE_PARTNER), "Partner") == 0,
                "Convert Partner to string");

    EXPECT_TRUE(entity_status_from_string("Active") == ENTITY_STATUS_ACTIVE,
                "Parse status Active");
    EXPECT_TRUE(entity_status_from_string("Inactive") == ENTITY_STATUS_INACTIVE,
                "Parse status Inactive");
    EXPECT_TRUE(entity_status_from_string("Unknown") == ENTITY_STATUS_UNKNOWN,
                "Parse invalid status returns Unknown");

    EXPECT_TRUE(strcmp(entity_status_to_string(ENTITY_STATUS_ACTIVE), "Active") == 0,
                "Convert Active to string");
    EXPECT_TRUE(strcmp(entity_status_to_string(ENTITY_STATUS_INACTIVE), "Inactive") == 0,
                "Convert Inactive to string");

    printf("\nEntity Validation Tests Completed. Passed: %d, Failed: %d\n",
           g_tests_passed, g_tests_failed);

    return g_tests_failed == 0 ? 0 : 1;
}
