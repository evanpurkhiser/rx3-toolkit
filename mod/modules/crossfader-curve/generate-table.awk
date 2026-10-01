# SPDX-License-Identifier: MPL-2.0
# Convert normalized curve points into escaped little-endian Q15 word pairs.

NR == 1 {
    target = $1
    expected = $2 + 0
    next
}

{
    position[count] = $1 + 0
    gain[count] = $2 + 0
    count++
}

END {
    if (count != expected || count < 2) exit 1

    segment = count - 2
    for (sample_index = 0; sample_index < 1024; sample_index++) {
        input = 1 - sample_index / 1023
        while (segment > 0 && input < position[segment]) segment--

        width = position[segment + 1] - position[segment]
        amount = (input - position[segment]) / width
        value = gain[segment] + amount * (gain[segment + 1] - gain[segment])
        sample = int(value * 32767 + 0.5)
        if (sample < 0 || sample > 32767) exit 1

        low[sample_index % 2] = sample % 256
        high[sample_index % 2] = int(sample / 256)
        if (sample_index % 2 == 1)
            printf "\\%03o\\%03o\\%03o\\%03o\n", \
                low[0], high[0], low[1], high[1]
    }
}
