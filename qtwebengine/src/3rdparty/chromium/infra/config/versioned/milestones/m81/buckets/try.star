load('//lib/builders.star', 'cpu', 'defaults', 'goma', 'os')
load('//lib/try.star', 'try_')
# Load this using relative path so that the load statement doesn't
# need to be changed when making a new milestone
load('../vars.star', 'vars')

defaults.pool.set('luci.chromium.try')

luci.bucket(
    name = vars.try_bucket,
    acls = [
        acl.entry(
            roles = acl.BUILDBUCKET_READER,
            groups = 'all',
        ),
        acl.entry(
            roles = acl.BUILDBUCKET_TRIGGERER,
            users = [
                'findit-for-me@appspot.gserviceaccount.com',
                'tricium-prod@appspot.gserviceaccount.com',
            ],
            groups = [
                'project-chromium-tryjob-access',
                # Allow Pinpoint to trigger builds for bisection
                'service-account-chromeperf',
                'service-account-cq',
            ],
        ),
        acl.entry(
            roles = acl.BUILDBUCKET_OWNER,
            groups = 'service-account-chromium-tryserver',
        ),
    ],
)

luci.cq_group(
    name = vars.cq_group,
    cancel_stale_tryjobs = True,
    retry_config = cq.RETRY_ALL_FAILURES,
    watch = cq.refset(
        repo = 'https://chromium.googlesource.com/chromium/src',
        refs = [vars.cq_ref_regexp],
    ),
    acls = [
        acl.entry(
            acl.CQ_COMMITTER,
            groups = 'project-chromium-committers',
        ),
        acl.entry(
            acl.CQ_DRY_RUNNER,
            groups = 'project-chromium-tryjob-access',
        ),
    ],
)

try_.defaults.bucket.set(vars.try_bucket)
try_.defaults.cq_group.set(vars.cq_group)


# Builders are sorted first lexicographically by the function used to define
# them, then lexicographically by their name


try_.chromium_android_builder(
    name = 'android-binary-size',
    executable = 'recipe:binary_size_trybot',
    goma_jobs = goma.jobs.J150,
    tryjob = try_.job(),
)

try_.chromium_android_builder(
    name = 'android-kitkat-arm-rel',
    goma_jobs = goma.jobs.J150,
    tryjob = try_.job(),
    should_exonerate_flaky_failures = True,
)

try_.chromium_android_builder(
    name = 'android-marshmallow-arm64-rel',
    cores = 16,
    goma_jobs = goma.jobs.J300,
    ssd = True,
    use_java_coverage = True,
    tryjob = try_.job(),
)

try_.chromium_android_builder(
    name = 'android_compile_dbg',
    goma_jobs = goma.jobs.J150,
    tryjob = try_.job(),
)

try_.chromium_android_builder(
    name = 'android_cronet',
    tryjob = try_.job(),
)

try_.chromium_android_builder(
    name = 'cast_shell_android',
    tryjob = try_.job(),
)


try_.chromium_chromiumos_builder(
    name = 'chromeos-arm-generic-rel',
    tryjob = try_.job(),
)

try_.chromium_chromiumos_builder(
    name = 'chromeos-amd64-generic-rel',
    tryjob = try_.job(),
)

try_.chromium_chromiumos_builder(
    name = 'linux-chromeos-compile-dbg',
    tryjob = try_.job(),
)

try_.chromium_chromiumos_builder(
    name = 'linux-chromeos-rel',
    goma_jobs = goma.jobs.J150,
    tryjob = try_.job(),
    use_clang_coverage = True,
)


try_.chromium_linux_builder(
    name = 'cast_shell_linux',
    tryjob = try_.job(),
)

try_.chromium_linux_builder(
    name = 'chromium_presubmit',
    executable = 'recipe:presubmit',
    goma_backend = None,
    properties = {
        '$depot_tools/presubmit': {
            'runhooks': True,
            'timeout_s': 480,
        },
        'repo_name': 'chromium',
    },
    tryjob = try_.job(
        disable_reuse = True,
    ),
)

try_.chromium_linux_builder(
    name = 'fuchsia_arm64',
    tryjob = try_.job(),
)

try_.chromium_linux_builder(
    name = 'fuchsia_x64',
    tryjob = try_.job(),
)

try_.chromium_linux_builder(
    name = 'linux-libfuzzer-asan-rel',
    executable = 'recipe:chromium_libfuzzer_trybot',
    tryjob = try_.job(),
)

try_.chromium_linux_builder(
    name = 'linux-ozone-rel',
    tryjob = try_.job(),
)

try_.chromium_linux_builder(
    name = 'linux-rel',
    goma_jobs = goma.jobs.J150,
    tryjob = try_.job(),
    use_clang_coverage = True,
    should_exonerate_flaky_failures = True,
)

try_.chromium_linux_builder(
    name = 'linux_chromium_asan_rel_ng',
    goma_jobs = goma.jobs.J150,
    ssd = True,
    tryjob = try_.job(),
)

try_.chromium_linux_builder(
    name = 'linux_chromium_compile_dbg_ng',
    caches = [
        swarming.cache(
            name = 'builder',
            path = 'linux_debug',
        ),
    ],
    goma_jobs = goma.jobs.J150,
    tryjob = try_.job(),
)

try_.chromium_linux_builder(
    name = 'linux_chromium_tsan_rel_ng',
    goma_jobs = goma.jobs.J150,
    tryjob = try_.job(),
)


try_.chromium_mac_builder(
    name = 'mac-rel',
    goma_jobs = goma.jobs.J150,
    tryjob = try_.job(),
    should_exonerate_flaky_failures = True,
)

try_.chromium_mac_builder(
    name = 'mac_chromium_compile_dbg_ng',
    goma_jobs = goma.jobs.J150,
    os = os.MAC_10_13,
    tryjob = try_.job(),
)


try_.chromium_mac_ios_builder(
    name = 'ios-simulator',
    goma_backend = None,
    tryjob = try_.job(),
)


try_.chromium_win_builder(
    name = 'win-libfuzzer-asan-rel',
    builderless = False,
    executable = 'recipe:chromium_libfuzzer_trybot',
    os = os.WINDOWS_ANY,
    tryjob = try_.job(),
)

try_.chromium_win_builder(
    name = 'win_chromium_compile_dbg_ng',
    goma_jobs = goma.jobs.J150,
    tryjob = try_.job(),
)

try_.chromium_win_builder(
    name = 'win10_chromium_x64_rel_ng',
    goma_jobs = goma.jobs.J150,
    os = os.WINDOWS_10,
    ssd = True,
    tryjob = try_.job(),
    should_exonerate_flaky_failures = True,
)
