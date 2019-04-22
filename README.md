# What's Here

This is a fork of [Zephyr](https://github.com/zephyrproject-rtos/zephyr)
that I'm using to provide functionality I need without incurring the
overhead of complying with upstream architectural assumptions and
requirements for cross-platform compatibility.  Generally these changes
are targeted to Nordic nRF5-based platforms.

Commit message summaries include the following tags:
* Commits that include the prefix `[DNM]` are not suitable for upstream
  integration for a variety of reasons including lack of consensus of
  generic value or failure to comply with upstream architectural
  decisions and policies.
* Commits that include the prefix `[WIP]` are works-in-progress.  Any
  aspect of such work, including interfaces and overall architecture, is
  subject to change.  `[WIP]` should follow `[DNM]` in the commit
  summary.  The body of the commit message should end with a section
  describing what aspects of the commit motivate use of this tag.
* Commits that include the prefix `[EVAL]` are changes made to demonstrate
  functionality (or lack thereof) or to add instrumentation such as GPIO
  changes around critical sections.  `[EVAL]` should follow `[DNM]` and
  `[WIP]` in the commit summary.
* Commits without any of the above prefixes are theoretically stable and
  suitable for upstream integration, but may not meet the expectations
  or desires of some reviewers without rework.

Generally commits should not use github features like "Closes #XXX" as
this produces notice of the commit in the upstream repository even when
upstream has expressed discontent with the solution.  References to
upstream issues and PRs can be expressed with Yocto-style
`Upstream-Status` tags.  This tag is required in any patch that is
back-ported, and is recommended in all commits.

In all other ways commit messages should conform to upstream standards.

The following branches may be available:
* `pabigot/master` would be created in a situation where `pabigot/next`
  reaches a stability that warrants a release.  We're not there yet, and
  probably never will be.
* `pabigot/next` is a nominally stable branch containing only patches
  that are not `[DNM]`.  It is based on a stable Zephyr release, with
  commits ordered by increasing risk or dependency.  It may be rebased
  to insert new commits at an appropriate position.
* `pabigot/dev` is a periodically-updated extension of a previously
  extant `pabigot/next` with additional material that is still marked
  `DNM`.  Material destined for `pabigot/next` will appear here first.
  This branch is frequently rebased.

Material on the `pabigot/` branches is expected to work on any Nordic
nRF5-based platform, but not all such platforms are tested.  Material on
`pabigot/next` is expected to build on all platforms but may lack
support for the capabilities added on the branch.
