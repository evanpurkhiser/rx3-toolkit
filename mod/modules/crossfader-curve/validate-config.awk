BEGIN { raw = "" }
{ raw = raw "\n" $0 }

function fail() { invalid = 1; exit 1 }
function precision_valid(value, decimal_at) {
    decimal_at = index(value, ".")
    return !decimal_at || length(value) - decimal_at <= 9
}

END {
    if (invalid) exit 1
    compact = ""; quoted = 0
    for (cursor = 1; cursor <= length(raw); cursor++) {
        byte = substr(raw, cursor, 1)
        if (byte == "\\") fail()
        if (byte == "\"") { quoted = !quoted; compact = compact byte; continue }
        if (!quoted && byte ~ /[[:space:]]/) continue
        compact = compact byte
    }
    if (quoted || compact !~ /^[{].*[}]$/) fail()
    compact = substr(compact, 2, length(compact) - 2)

    depth = 0; fields_count = 1; fields[1] = ""
    for (cursor = 1; cursor <= length(compact); cursor++) {
        byte = substr(compact, cursor, 1)
        if (byte == "[") depth++
        if (byte == "]") { depth--; if (depth < 0) fail() }
        if (byte == "," && depth == 0) { fields_count++; fields[fields_count] = ""; continue }
        fields[fields_count] = fields[fields_count] byte
    }
    if (depth != 0 || fields_count != 3) fail()

    for (field_index = 1; field_index <= fields_count; field_index++) {
        field = fields[field_index]
        if (field == "\"version\":1") {
            if (version_seen++) fail()
        } else if (field ~ /^\"target\":\"(mid|mid-half|sharp)\"$/) {
            if (target_seen++) fail()
            target = field
            sub(/^\"target\":\"/, "", target); sub(/\"$/, "", target)
        } else if (field ~ /^\"points\":\[.*\]$/) {
            if (points_seen++) fail()
            points_text = field
            sub(/^\"points\":\[/, "", points_text); sub(/\]$/, "", points_text)
        } else {
            fail()
        }
    }
    if (!version_seen || !target_seen || !points_seen) fail()

    if (points_text !~ /^\[[^][]+\](,\[[^][]+\])*$/) fail()
    points_count = split(points_text, encoded_points, /\],\[/)
    if (points_count < 2 || points_count > 32) fail()
    number = "(0([.][0-9]+)?|1([.]0+)?)"
    for (point_index = 1; point_index <= points_count; point_index++) {
        point = encoded_points[point_index]
        gsub(/^\[/, "", point); gsub(/\]$/, "", point)
        if (point !~ ("^" number "," number "$")) fail()
        if (split(point, coordinates, ",") != 2) fail()
        if (!precision_valid(coordinates[1]) || !precision_valid(coordinates[2])) fail()
        x[point_index] = coordinates[1] + 0
        y[point_index] = coordinates[2] + 0
        if (point_index > 1 && x[point_index] <= x[point_index - 1]) fail()
        if (point_index > 1 && y[point_index] < y[point_index - 1]) fail()
    }
    if (x[1] != 0 || y[1] != 0 || x[points_count] != 1 || y[points_count] != 1) fail()

    print target, points_count
    for (point_index = 1; point_index <= points_count; point_index++)
        printf "%.9f %.9f\n", x[point_index], y[point_index]
}
