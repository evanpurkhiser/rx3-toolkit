BEGIN { text = "" }
{ text = text " " $0 }
END {
    if (text !~ /^[[:space:]]*[{].*[}][[:space:]]*$/) exit 1
    sub(/^[[:space:]]*[{]/, "", text)
    sub(/[}][[:space:]]*$/, "", text)
    count = split(text, parts, ",")
    if (count != 3) exit 1
    for (item = 1; item <= count; item++) {
        field = parts[item]
        if (field ~ /^[[:space:]]*\"version\"[[:space:]]*:[[:space:]]*1[[:space:]]*$/) {
            if (version_seen++) exit 1
        } else if (field ~ /^[[:space:]]*\"target\"[[:space:]]*:[[:space:]]*\"(mid|mid-half|sharp)\"[[:space:]]*$/) {
            if (target_seen++) exit 1
            target = field
            sub(/^.*:[[:space:]]*\"/, "", target)
            sub(/\"[[:space:]]*$/, "", target)
        } else if (field ~ /^[[:space:]]*\"midpoint_db\"[[:space:]]*:[[:space:]]*-([0-9]+)([.][0-9]+)?[[:space:]]*$/) {
            if (midpoint_seen++) exit 1
            midpoint = field
            sub(/^.*:[[:space:]]*/, "", midpoint)
            sub(/[[:space:]]*$/, "", midpoint)
            if ((midpoint + 0) < -48 || (midpoint + 0) > -3.0103) exit 1
        } else {
            exit 1
        }
    }
    if (!version_seen || !target_seen || !midpoint_seen) exit 1
    print target, midpoint
}
