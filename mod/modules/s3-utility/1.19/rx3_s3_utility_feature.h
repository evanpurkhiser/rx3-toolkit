/* SPDX-License-Identifier: MPL-2.0
 * ESP32-S3 rows contributed to the core utility-menu broker.
 */

#ifndef RX3_S3_UTILITY_FEATURE_H
#define RX3_S3_UTILITY_FEATURE_H

static int s3_utility_configured(void)
{
    const char *enabled = getenv("RX3_S3_UTILITY");
    return enabled && enabled[0] == '1';
}

static const char *s3_utility_proof_status(void)
{
    return "PROOF OF CONCEPT";
}

static const struct rx3_utility_item s3_utility_items[] = {
    {RX3_UTILITY_SECTION, "ESP32-S3 INTEGRATION", 0},
    {RX3_UTILITY_VALUE, "      WI-FI STATUS", s3_utility_proof_status},
};

static const struct rx3_utility_extension s3_utility_extension = {
    "s3-utility",
    s3_utility_configured,
    sizeof(s3_utility_items) / sizeof(s3_utility_items[0]),
    s3_utility_items,
};

#endif /* RX3_S3_UTILITY_FEATURE_H */
