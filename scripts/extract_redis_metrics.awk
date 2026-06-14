BEGIN {
    FS = ","
    OFS = ","
    print "test,rps,avg_latency_ms,p95_latency_ms,p99_latency_ms,max_latency_ms"
}
NR == 1 { next }
{
    gsub(/"/, "", $1)
    gsub(/"/, "", $2)
    gsub(/"/, "", $3)
    gsub(/"/, "", $6)
    gsub(/"/, "", $7)
    gsub(/"/, "", $8)

    if ($1 == "PING_INLINE" || $1 == "GET" || $1 == "SET" || $1 == "INCR") {
        print $1, $2, $3, $6, $7, $8
    }
}
