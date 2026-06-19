awk '
/VALUE js_.*_method\(.*\) \{/ || /VALUE js_.*\(.*\) \{/ {
    func_name = $2;
    getline;
    if ($0 ~ /return VAL_UNDEFINED;/) {
        print func_name;
    } else {
        getline;
        if ($0 ~ /return VAL_UNDEFINED;/) {
            print func_name;
        }
    }
}
' src/builtins.c
