Set-StrictMode -Version Latest

function Assert-TomCatReleaseProvenance {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Tag,

        [Parameter(Mandatory)]
        [string]$Version,

        [Parameter(Mandatory)]
        [string]$CommitSha,

        [Parameter(Mandatory)]
        [string]$TagTargetSha,

        [Parameter(Mandatory)]
        [string]$DefaultBranch,

        [Parameter(Mandatory)]
        [bool]$TagRefProtected,

        [Parameter(Mandatory)]
        [bool]$DefaultBranchProtected,

        [Parameter(Mandatory)]
        [ValidateSet('ahead', 'behind', 'diverged', 'identical')]
        [string]$DefaultBranchComparisonStatus,

        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [object[]]$RegressionRuns
    )

    if ($Tag -ne "v$Version") {
        throw "Release tag '$Tag' does not match product version '$Version'."
    }
    foreach ($candidate in @($CommitSha, $TagTargetSha)) {
        if ($candidate -notmatch '^[a-fA-F0-9]{40}$') {
            throw "Release provenance contains an invalid Git commit SHA: '$candidate'."
        }
    }
    if (-not $CommitSha.Equals($TagTargetSha, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Protected tag '$Tag' resolves to $TagTargetSha, not workflow SHA $CommitSha."
    }
    if (-not $TagRefProtected) {
        throw "Release tag '$Tag' is not covered by a GitHub tag protection rule or ruleset."
    }
    if (-not $DefaultBranchProtected) {
        throw "Default branch '$DefaultBranch' is not protected."
    }
    if ($DefaultBranchComparisonStatus -notin @('ahead', 'identical')) {
        throw "Release commit $CommitSha is not in the history of default branch '$DefaultBranch'."
    }

    $successfulRuns = @($RegressionRuns | Where-Object {
        ([string]$_.head_sha).Equals($CommitSha, [System.StringComparison]::OrdinalIgnoreCase) -and
        ([string]$_.head_branch).Equals($DefaultBranch, [System.StringComparison]::Ordinal) -and
        ([string]$_.event).Equals('push', [System.StringComparison]::Ordinal) -and
        ([string]$_.status).Equals('completed', [System.StringComparison]::Ordinal) -and
        ([string]$_.conclusion).Equals('success', [System.StringComparison]::Ordinal)
    })
    if ($successfulRuns.Count -eq 0) {
        throw "No successful full Regressions workflow run exists for default-branch SHA $CommitSha."
    }

    return $successfulRuns |
        Sort-Object -Property run_number -Descending |
        Select-Object -First 1
}
