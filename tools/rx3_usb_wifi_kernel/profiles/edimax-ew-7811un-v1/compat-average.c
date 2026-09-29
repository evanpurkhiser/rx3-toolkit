// SPDX-License-Identifier: GPL-2.0
#include <linux/average.h>
#include <linux/bug.h>
#include <linux/log2.h>
#include <linux/module.h>

void ewma_init(struct ewma *average, unsigned long factor, unsigned long weight)
{
    WARN_ON(!is_power_of_2(weight) || !is_power_of_2(factor));

    average->weight = ilog2(weight);
    average->factor = ilog2(factor);
    average->internal = 0;
}
EXPORT_SYMBOL(ewma_init);

struct ewma *ewma_add(struct ewma *average, unsigned long value)
{
    average->internal = average->internal
        ? (((average->internal << average->weight) - average->internal) +
           (value << average->factor)) >> average->weight
        : value << average->factor;
    return average;
}
EXPORT_SYMBOL(ewma_add);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EWMA compatibility exports for RX3 mac80211");
