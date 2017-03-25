# Gluster FS Performance Check

Gluster FS Performance Check is a small utility that reads the JSON latency dump generated by GlusterFS (from version 3.8) and outputs a Nagios compliant status (OK, CRITICAL, WANRING, UNKNOWN) and performance data.

### Prerequisites
- GlusterFS 3.8+
- UNIX system

### Build Dependencies
-GCC 6.2+

### Building

    git clone https://github.com/alexculea/check_gluster_perf.git
    make 

or

    make static

to bundle stdlibc++ (the executable could get quite big though). 

### Installation

Copy the generated binary to a sensible location on your system. Usually /usr/lib/nagios/plugins.
Make sure it has execution rights for the nagios user eg:

    chown nagios:nagios check_gluster_perf
    chmod u+x check_gluster_perf

If you're using Icinga2, see the util directory for the command file.

## Known issue

The program was designed to check time-based values. The GlusterFS dump (3.8 at the time of the writing) also has several integer based statistics. **It's strongly recommended** that a filter (-f) is used to take into account only the metrics that hold such values. An efective filter could be "\*.usec" because of how the metrics are reported by Gluster. The default value of -f is in fact "\*.usec".

Not setting the said filter has unexpected results. A fix is scheduled for the next release.

### Prepare GlusterFS

To enable JSON dumps with GlusterFS (3.8 or newer) do:

    gluster volume set <volname> diagnostics.stats-dump-interval 300 # every 5 min
    gluster volume set <volname> diagnostics.latency-measurement 1

It's a good idea to check /var/lib/glusterd/\<volname\>/stats if the dump is being generated.

### Quick start

    check_gluster_perf -w 100 -c 120 -vol glusterfs_volume
    # Checks all time-based metrics in the dump file for the volume <glusterfs_volume>. Reports OK if all are under 100 warn, 120 crit microseconds (usec)

    check_gluster_perf -w 40 -c 50 -u ms -vol glusterfs_volume -f .*aggr.*latency_ave.*usec
    # Checks all aggregated average latency metrics. Reports OK if all are under 40 warn, 50 crit; miliseconds

    check_gluster_perf -w 40 -c 50 -u ms -vol glusterfs_volume -f .*aggr.*latency_ave.*usec -apply-on-total-avg	1
    # Makes a total average of all aggregated latency average metrics and applies the warning/critical thresholds to that total average only.

    check_gluster_perf -w 40 -c 50 -u ms -vol glusterfs_volume -f .*aggr.*latency_ave.* -dump-max-age-seconds 600
    # Complains CRITICAL'lly only if the dump file is older than 10 minutes. 5 minutes is the default.

### Supported Parameters

        Available parameters:

        -h	--help
        
        This parameter is optional. The default value is ''.

        -w	--warning	(required)
        Warning threshold in -u units or in microseconds if -u is not specified.

        -c	--critical	(required)
        Critical threshold in -u units or in microseconds if -u is not specified.

        -vol	--volume	(required)
        GlusterFS Volume name.

        -u	--unit
        Time measurement unit used to interpret input arguments -w and -c. Possible values: 'us': microseconds, 'ms': miliseconds, 's': seconds
        This parameter is optional. The default value is 'us'.

        -ou	--out-unit
        Time measurement unit used to output the key performance indicators read. Possible values: 'us': microseconds, 'ms': miliseconds, 's': seconds
        This parameter is optional. The default value is 'us'.

        -f	--filter
        Regular expression (ECMAScript grammar, case insesitive) filter. If given, only the metrics that fully match the pattern will be considered for evaluation and reporting.
        This parameter is optional. The default value is '.*usec'.

        -apply-on-total-avg	
        If set to true, the thresholds are applied to the total average of all metrics instead of each metric.
        This parameter is optional. The default value is '0'.

        -v	--verbose
        Verbose output.
        This parameter is optional. The default value is '0'.

        -override-stats-file	
        If given, this file will be read instead of the default GlusterFS dump file.
        This parameter is optional. The default value is ''.

        -gluster-src-unit	
        The time unit dumped by GlusterFS.
        This parameter is optional. The default value is 'us'.

        -dump-max-age-seconds	
        Maximum dump age allowed. If the file is older, a CRITICAL will be reported.
        This parameter is optional. The default value is '300'.

        -exceeded-metrics-report-count	
        The maximum number of metrics to report over the threshold. Only affects check output, not performance data.
        This parameter is optional. The default value is '1000'.

        -V	--version
        Show program version.
        This parameter is optional. The default value is '0'.
        
# License

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>