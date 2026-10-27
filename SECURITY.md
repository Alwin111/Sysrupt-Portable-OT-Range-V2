# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| `main` branch | ✅ |

## Two very different kinds of "vulnerability"

SYSRUPT OT Range V2 is a **training / CTF simulator**. Please distinguish:

1. **Intended CTF vulnerabilities (not security bugs).** The injectable historian query, the
   unauthenticated GOOSE stream, the weak safety‑controller lock, the plaintext/default credentials in
   the simulated plants, and similar weaknesses are **deliberate challenge content**. They exist on
   purpose, are confined to the simulated plants on the badge, and should **not** be reported as
   security issues. Fixing them would remove the challenges.
2. **Real platform vulnerabilities (please report).** A memory‑safety bug, a crash, a flaw in the
   scoring/persistence logic, a defect in the `sysrupt` toolkit, or anything that undermines the badge
   *outside* the intended challenge surface — these are real issues we want to hear about.

## Platform context (inherited from Tactility)

This project is built on **Tactility OS**, which is primarily a tinkering platform, not a hardened user
platform. It does not provide desktop‑class protections such as user/access management, filesystem
protections, memory protections, or app permissions. [ESP Privilege
Separation](https://github.com/espressif/esp-privilege-separation) is not used. Keep this in mind when
assessing impact.

## Authorized testing only

The offensive tooling and techniques here are for use **only** against the authorized badge/lab or
systems you have explicit permission to test. **Do not** point them at production OT/ICS networks or any
equipment you are not authorized to test. See the authorization notice in the
[`README.md`](README.md#security--authorization).

## Reporting a real vulnerability

Please report privately rather than opening a public issue, using GitHub's built-in **private
vulnerability reporting** for this repository:

**<https://github.com/Alwin111/Sysrupt-Portable-OT-Range-V2/security/advisories/new>**

(In the repository: the **Security** tab → **Report a vulnerability**.) This keeps the report private
between you and the maintainer until a fix is ready. No email address is published here by design — the
GitHub channel is the single reporting path.

> **Maintainer action required before publishing:** turn on private vulnerability reporting for this
> repository under **Settings → Code security and analysis → Private vulnerability reporting**. Until it
> is enabled, the link above will not accept reports.

You can expect an acknowledgement and, where applicable, updates on remediation. For issues in
**third‑party dependencies** (see [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md) and
[`SUBMODULES.md`](SUBMODULES.md)), please also report to that component's upstream maintainers. Report
**upstream Tactility** issues to the [Tactility project](https://tactilityproject.org/#/support).
