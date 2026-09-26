/* SPDX-License-Identifier: MPL-2.0
 * Process identity checks for libraries inherited through LD_PRELOAD.
 */

#ifndef RX3_PROCESS_H
#define RX3_PROCESS_H

static int rx3_running_in_rbp(void)
{
    static const char expected[] = "/root/pdj/rbp";
    char executable[sizeof(expected)];
    ssize_t length = readlink("/proc/self/exe", executable,
                              sizeof(executable));

    return length == (ssize_t)(sizeof(expected) - 1u) &&
           !memcmp(executable, expected, sizeof(expected) - 1u);
}

#endif /* RX3_PROCESS_H */
