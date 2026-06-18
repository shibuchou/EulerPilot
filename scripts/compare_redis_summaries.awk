BEGIN {
    FS = ","
    OFS = ","
}

FNR == 1 { next }

ARGIND == 1 {
    base_rps[$1] = $2
    base_p99[$1] = $5
    tests[$1] = 1
    next
}

ARGIND == 2 {
    noisy_rps[$1] = $2
    noisy_p99[$1] = $5
    tests[$1] = 1
    next
}

ARGIND == 3 {
    active_rps[$1] = $2
    active_p99[$1] = $5
    tests[$1] = 1
    next
}

END {
    print "test,baseline_rps,default_noisy_rps,active_noisy_rps,baseline_p99_ms,default_noisy_p99_ms,active_noisy_p99_ms"
    for (t in tests) {
        print t, base_rps[t], noisy_rps[t], active_rps[t], base_p99[t], noisy_p99[t], active_p99[t]
    }
}
